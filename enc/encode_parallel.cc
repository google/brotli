// Copyright 2013 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Implementation of parallel Brotli compressor.

#include "./encode_parallel.h"

#include <algorithm>
#include <limits>

#include "./backward_references.h"
#include "./bit_cost.h"
#include "./block_splitter.h"
#include "./brotli_bit_stream.h"
#include "./cluster.h"
#include "./context.h"
#include "./metablock.h"
#include "./transform.h"
#include "./entropy_encode.h"
#include "./fast_log.h"
#include "./hash.h"
#include "./histogram.h"
#include "./literal_cost.h"
#include "./prefix.h"
#include "./write_bits.h"

namespace brotli {

namespace {

int ParseAsUTF8(int* symbol, const uint8_t* input, int size) {
  // ASCII
  if ((input[0] & 0x80) == 0) {
    *symbol = input[0];
    if (*symbol > 0) {
      return 1;
    }
  }
  // 2-byte UTF8
  if (size > 1 &&
      (input[0] & 0xe0) == 0xc0 &&
      (input[1] & 0xc0) == 0x80) {
    *symbol = (((input[0] & 0x1f) << 6) |
               (input[1] & 0x3f));
    if (*symbol > 0x7f) {
      return 2;
    }
  }
  // 3-byte UFT8
  if (size > 2 &&
      (input[0] & 0xf0) == 0xe0 &&
      (input[1] & 0xc0) == 0x80 &&
      (input[2] & 0xc0) == 0x80) {
    *symbol = (((input[0] & 0x0f) << 12) |
               ((input[1] & 0x3f) << 6) |
               (input[2] & 0x3f));
    if (*symbol > 0x7ff) {
      return 3;
    }
  }
  // 4-byte UFT8
  if (size > 3 &&
      (input[0] & 0xf8) == 0xf0 &&
      (input[1] & 0xc0) == 0x80 &&
      (input[2] & 0xc0) == 0x80 &&
      (input[3] & 0xc0) == 0x80) {
    *symbol = (((input[0] & 0x07) << 18) |
               ((input[1] & 0x3f) << 12) |
               ((input[2] & 0x3f) << 6) |
               (input[3] & 0x3f));
    if (*symbol > 0xffff && *symbol <= 0x10ffff) {
      return 4;
    }
  }
  // Not UTF8, emit a special symbol above the UTF8-code space
  *symbol = 0x110000 | input[0];
  return 1;
}

// Returns true if at least min_fraction of the data is UTF8-encoded.
bool IsMostlyUTF8(const uint8_t* data, size_t length, double min_fraction) {
  size_t size_utf8 = 0;
  for (size_t pos = 0; pos < length; ) {
    int symbol;
    int bytes_read = ParseAsUTF8(&symbol, data + pos, length - pos);
    pos += bytes_read;
    if (symbol < 0x110000) size_utf8 += bytes_read;
  }
  return size_utf8 > min_fraction * length;
}

void RecomputeDistancePrefixes(std::vector<Command>* cmds,
                               int num_direct_distance_codes,
                               int distance_postfix_bits) {
  if (num_direct_distance_codes == 0 &&
      distance_postfix_bits == 0) {
    return;
  }
  for (int i = 0; i < cmds->size(); ++i) {
    Command* cmd = &(*cmds)[i];
    if (cmd->copy_len_ > 0 && cmd->cmd_prefix_ >= 128) {
      PrefixEncodeCopyDistance(cmd->DistanceCode(),
                               num_direct_distance_codes,
                               distance_postfix_bits,
                               &cmd->dist_prefix_,
                               &cmd->dist_extra_);
    }
  }
}

bool WriteMetaBlockParallel(const BrotliParams& params,
                            const size_t block_size,
                            const uint8_t* input_buffer,
                            const size_t prefix_size,
                            const uint8_t* prefix_buffer,
                            const StaticDictionary* static_dict,
                            const bool is_first,
                            const bool is_last,
                            size_t* encoded_size,
                            uint8_t* encoded_buffer) {
  if (block_size == 0) {
    return false;
  }
  const size_t input_size = block_size;

  // Copy prefix + next input block into a continuous area.
  size_t input_pos = prefix_size;
  std::vector<uint8_t> input(prefix_size + input_size);
  memcpy(&input[0], prefix_buffer, prefix_size);
  memcpy(&input[input_pos], input_buffer, input_size);
  // Since we don't have a ringbuffer, masking is a no-op.
  // We use one less bit than the full range because some of the code uses
  // mask + 1 as the size of the ringbuffer.
  const size_t mask = std::numeric_limits<size_t>::max() >> 1;

  uint8_t prev_byte = input_pos > 0 ? input[(input_pos - 1) & mask] : 0;
  uint8_t prev_byte2 = input_pos > 1 ? input[(input_pos - 2) & mask] : 0;

  // Decide about UTF8 mode.
  static const double kMinUTF8Ratio = 0.75;
  bool utf8_mode = IsMostlyUTF8(&input[input_pos], input_size, kMinUTF8Ratio);

  // Compute literal costs.
  std::vector<float> literal_cost(prefix_size + input_size);
  if (utf8_mode) {
    EstimateBitCostsForLiteralsUTF8(input_pos, input_size, mask, mask,
                                    &input[0], &literal_cost[0]);
  } else {
    EstimateBitCostsForLiterals(input_pos, input_size, mask, mask,
                                &input[0], &literal_cost[0]);
  }

  // Initialize hashers.
  int hash_type = 9;
  switch (params.mode) {
    case BrotliParams::MODE_TEXT: hash_type = 8; break;
    case BrotliParams::MODE_FONT: hash_type = 9; break;
    default: break;
  }
  std::unique_ptr<Hashers> hashers(new Hashers());
  hashers->Init(hash_type);
  hashers->SetStaticDictionary(static_dict);

  // Compute backward references.
  int last_insert_len = 0;
  int num_commands = 0;
  double base_min_score = 8.115;
  int max_backward_distance = (1 << params.lgwin) - 16;
  int dist_cache[4] = { -4, -4, -4, -4 };
  std::vector<Command> commands((input_size + 1) >> 1);
  CreateBackwardReferences(
      input_size, input_pos,
      &input[0], mask,
      &literal_cost[0], mask,
      max_backward_distance,
      base_min_score,
      params.quality,
      hashers.get(),
      hash_type,
      dist_cache,
      &last_insert_len,
      &commands[0],
      &num_commands);
  commands.resize(num_commands);
  if (last_insert_len > 0) {
    commands.push_back(Command(last_insert_len));
  }

  // Build the meta-block.
  MetaBlockSplit mb;
  int num_direct_distance_codes =
      params.mode == BrotliParams::MODE_FONT ? 12 : 0;
  int distance_postfix_bits = params.mode == BrotliParams::MODE_FONT ? 1 : 0;
  int literal_context_mode = utf8_mode ? CONTEXT_UTF8 : CONTEXT_SIGNED;
  RecomputeDistancePrefixes(&commands,
                            num_direct_distance_codes,
                            distance_postfix_bits);
  if (params.greedy_block_split) {
    BuildMetaBlockGreedy(&input[0], input_pos, mask,
                         commands.data(), commands.size(),
                         &mb);
  } else {
    BuildMetaBlock(&input[0], input_pos, mask,
                   prev_byte, prev_byte2,
                   commands.data(), commands.size(),
                   literal_context_mode,
                   true,
                   &mb);
  }

  // Set up the temporary output storage.
  const size_t max_out_size = 2 * input_size + 500;
  std::vector<uint8_t> storage(max_out_size);
  int first_byte = 0;
  int first_byte_bits = 0;
  if (is_first) {
    if (params.lgwin == 16) {
      first_byte = 0;
      first_byte_bits = 1;
    } else {
      first_byte = ((params.lgwin - 17) << 1) | 1;
      first_byte_bits = 4;
    }
  }
  storage[0] = first_byte;
  int storage_ix = first_byte_bits;

  // Store the meta-block to the temporary output.
  if (!StoreMetaBlock(&input[0], input_pos, input_size, mask,
                      prev_byte, prev_byte2,
                      is_last,
                      num_direct_distance_codes,
                      distance_postfix_bits,
                      literal_context_mode,
                      commands.data(), commands.size(),
                      mb,
                      &storage_ix, &storage[0])) {
    return false;
  }

  // If this is not the last meta-block, store an empty metadata
  // meta-block so that the meta-block will end at a byte boundary.
  if (!is_last) {
    StoreSyncMetaBlock(&storage_ix, &storage[0]);
  }

  // If the compressed data is too large, fall back to an uncompressed
  // meta-block.
  size_t output_size = storage_ix >> 3;
  if (input_size + 4 < output_size) {
    storage[0] = first_byte;
    storage_ix = first_byte_bits;
    if (!StoreUncompressedMetaBlock(is_last, &input[0], input_pos, mask,
                                    input_size,
                                    &storage_ix, &storage[0])) {
      return false;
    }
    output_size = storage_ix >> 3;
  }

  // Copy the temporary output with size-check to the output.
  if (output_size > *encoded_size) {
    return false;
  }
  memcpy(encoded_buffer, &storage[0], output_size);
  *encoded_size = output_size;
  return true;
}

}  // namespace

int BrotliCompressBufferParallel(BrotliParams params,
                                 size_t input_size,
                                 const uint8_t* input_buffer,
                                 size_t* encoded_size,
                                 uint8_t* encoded_buffer) {
  if (*encoded_size == 0) {
    // Output buffer needs at least one byte.
    return 0;
  } else  if (input_size == 0) {
    encoded_buffer[0] = 6;
    *encoded_size = 1;
    return 1;
  }

  // Sanitize params.
  if (params.lgwin < kMinWindowBits) {
    params.lgwin = kMinWindowBits;
  } else if (params.lgwin > kMaxWindowBits) {
    params.lgwin = kMaxWindowBits;
  }
  if (params.lgblock == 0) {
    params.lgblock = 16;
    if (params.quality >= 9 && params.lgwin > params.lgblock) {
      params.lgblock = std::min(21, params.lgwin);
    }
  } else if (params.lgblock < kMinInputBlockBits) {
    params.lgblock = kMinInputBlockBits;
  } else if (params.lgblock > kMaxInputBlockBits) {
    params.lgblock = kMaxInputBlockBits;
  }
  size_t max_input_block_size = 1 << params.lgblock;

  std::vector<std::vector<uint8_t> > compressed_pieces;
  StaticDictionary dict;
  dict.Fill(params.enable_transforms);

  // Compress block-by-block independently.
  for (size_t pos = 0; pos < input_size; ) {
    size_t input_block_size = std::min(max_input_block_size, input_size - pos);
    size_t out_size = 1.2 * input_block_size + 1024;
    std::vector<uint8_t> out(out_size);
    if (!WriteMetaBlockParallel(params,
                                input_block_size,
                                &input_buffer[pos],
                                pos,
                                input_buffer,
                                &dict,
                                pos == 0,
                                pos + input_block_size == input_size,
                                &out_size,
                                &out[0])) {
      return false;
    }
    out.resize(out_size);
    compressed_pieces.push_back(out);
    pos += input_block_size;
  }

  // Piece together the output.
  size_t out_pos = 0;
  for (int i = 0; i < compressed_pieces.size(); ++i) {
    const std::vector<uint8_t>& out = compressed_pieces[i];
    if (out_pos + out.size() > *encoded_size) {
      return false;
    }
    memcpy(&encoded_buffer[out_pos], &out[0], out.size());
    out_pos += out.size();
  }
  *encoded_size = out_pos;

  return true;
}

}  // namespace brotli
