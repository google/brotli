/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

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
#include "./prefix.h"
#include "./utf8_util.h"
#include "./write_bits.h"

namespace brotli {

namespace {

void RecomputeDistancePrefixes(Command* cmds, size_t num_commands,
                               uint32_t num_direct_distance_codes,
                               uint32_t distance_postfix_bits) {
  if (num_direct_distance_codes == 0 &&
      distance_postfix_bits == 0) {
    return;
  }
  for (size_t i = 0; i < num_commands; ++i) {
    Command* cmd = &cmds[i];
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
                            const uint32_t input_size,
                            const uint8_t* input_buffer,
                            const uint32_t prefix_size,
                            const uint8_t* prefix_buffer,
                            const bool is_first,
                            const bool is_last,
                            size_t* encoded_size,
                            uint8_t* encoded_buffer) {
  if (input_size == 0) {
    return false;
  }

  // Copy prefix + next input block into a continuous area.
  uint32_t input_pos = prefix_size;
  // CreateBackwardReferences reads up to 3 bytes past the end of input if the
  // mask points past the end of input.
  // FindMatchLengthWithLimit could do another 8 bytes look-forward.
  std::vector<uint8_t> input(prefix_size + input_size + 4 + 8);
  memcpy(&input[0], prefix_buffer, prefix_size);
  memcpy(&input[input_pos], input_buffer, input_size);
  // Since we don't have a ringbuffer, masking is a no-op.
  // We use one less bit than the full range because some of the code uses
  // mask + 1 as the size of the ringbuffer.
  const uint32_t mask = std::numeric_limits<uint32_t>::max() >> 1;

  uint8_t prev_byte = input_pos > 0 ? input[(input_pos - 1) & mask] : 0;
  uint8_t prev_byte2 = input_pos > 1 ? input[(input_pos - 2) & mask] : 0;

  // Decide about UTF8 mode.
  static const double kMinUTF8Ratio = 0.75;
  bool utf8_mode = IsMostlyUTF8(&input[0], input_pos, mask, input_size,
                                kMinUTF8Ratio);

  // Initialize hashers.
  int hash_type = std::min(10, params.quality);
  Hashers* hashers = new Hashers();
  hashers->Init(hash_type);

  // Compute backward references.
  size_t last_insert_len = 0;
  size_t num_commands = 0;
  size_t num_literals = 0;
  int dist_cache[4] = { -4, -4, -4, -4 };
  Command* commands = static_cast<Command*>(
      malloc(sizeof(Command) * ((input_size + 1) >> 1)));
  if (commands == 0) {
    delete hashers;
    return false;
  }
  CreateBackwardReferences(
      input_size, input_pos, is_last,
      &input[0], mask,
      params.quality,
      params.lgwin,
      hashers,
      hash_type,
      dist_cache,
      &last_insert_len,
      commands,
      &num_commands,
      &num_literals);
  delete hashers;
  if (last_insert_len > 0) {
    commands[num_commands++] = Command(last_insert_len);
    num_literals += last_insert_len;
  }
  assert(num_commands != 0);

  // Build the meta-block.
  MetaBlockSplit mb;
  uint32_t num_direct_distance_codes =
      params.mode == BrotliParams::MODE_FONT ? 12 : 0;
  uint32_t distance_postfix_bits =
      params.mode == BrotliParams::MODE_FONT ? 1 : 0;
  ContextType literal_context_mode = utf8_mode ? CONTEXT_UTF8 : CONTEXT_SIGNED;
  RecomputeDistancePrefixes(commands, num_commands,
                            num_direct_distance_codes,
                            distance_postfix_bits);
  if (params.quality <= 9) {
    BuildMetaBlockGreedy(&input[0], input_pos, mask,
                         commands, num_commands,
                         &mb);
  } else {
    BuildMetaBlock(&input[0], input_pos, mask,
                   prev_byte, prev_byte2,
                   commands, num_commands,
                   literal_context_mode,
                   &mb);
  }

  // Set up the temporary output storage.
  const size_t max_out_size = 2 * input_size + 500;
  std::vector<uint8_t> storage(max_out_size);
  uint8_t first_byte = 0;
  size_t first_byte_bits = 0;
  if (is_first) {
    if (params.lgwin == 16) {
      first_byte = 0;
      first_byte_bits = 1;
    } else if (params.lgwin == 17) {
      first_byte = 1;
      first_byte_bits = 7;
    } else {
      first_byte = static_cast<uint8_t>(((params.lgwin - 17) << 1) | 1);
      first_byte_bits = 4;
    }
  }
  storage[0] = static_cast<uint8_t>(first_byte);
  size_t storage_ix = first_byte_bits;

  // Store the meta-block to the temporary output.
  StoreMetaBlock(&input[0], input_pos, input_size, mask,
                 prev_byte, prev_byte2,
                 is_last,
                 num_direct_distance_codes,
                 distance_postfix_bits,
                 literal_context_mode,
                 commands, num_commands,
                 mb,
                 &storage_ix, &storage[0]);
  free(commands);

  // If this is not the last meta-block, store an empty metadata
  // meta-block so that the meta-block will end at a byte boundary.
  if (!is_last) {
    StoreSyncMetaBlock(&storage_ix, &storage[0]);
  }

  // If the compressed data is too large, fall back to an uncompressed
  // meta-block.
  size_t output_size = storage_ix >> 3;
  if (input_size + 4 < output_size) {
    storage[0] = static_cast<uint8_t>(first_byte);
    storage_ix = first_byte_bits;
    StoreUncompressedMetaBlock(is_last, &input[0], input_pos, mask,
                               input_size,
                               &storage_ix, &storage[0]);
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
  size_t max_prefix_size = 1u << params.lgwin;

  std::vector<std::vector<uint8_t> > compressed_pieces;

  // Compress block-by-block independently.
  for (size_t pos = 0; pos < input_size; ) {
    uint32_t input_block_size =
        static_cast<uint32_t>(std::min(max_input_block_size, input_size - pos));
    uint32_t prefix_size =
        static_cast<uint32_t>(std::min(max_prefix_size, pos));
    size_t out_size = input_block_size + (input_block_size >> 3) + 1024;
    std::vector<uint8_t> out(out_size);
    if (!WriteMetaBlockParallel(params,
                                input_block_size,
                                &input_buffer[pos],
                                prefix_size,
                                &input_buffer[pos - prefix_size],
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
  for (size_t i = 0; i < compressed_pieces.size(); ++i) {
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
