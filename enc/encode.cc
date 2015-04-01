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
// Implementation of Brotli compressor.

#include "./encode.h"

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
  size_t pos = 0;
  while (pos < length) {
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

size_t MetaBlockLength(const std::vector<Command>& cmds) {
  size_t length = 0;
  for (int i = 0; i < cmds.size(); ++i) {
    const Command& cmd = cmds[i];
    length += cmd.insert_len_ + cmd.copy_len_;
  }
  return length;
}

uint8_t* BrotliCompressor::GetBrotliStorage(size_t size) {
  if (storage_size_ < size) {
    storage_.reset(new uint8_t[size]);
    storage_size_ = size;
  }
  return &storage_[0];
}

BrotliCompressor::BrotliCompressor(BrotliParams params)
    : params_(params),
      hashers_(new Hashers()),
      input_pos_(0) {
  // Sanitize params.
  if (params_.lgwin < kMinWindowBits) {
    params_.lgwin = kMinWindowBits;
  } else if (params_.lgwin > kMaxWindowBits) {
    params_.lgwin = kMaxWindowBits;
  }
  if (params_.lgblock == 0) {
    params_.lgblock = 16;
    if (params_.quality >= 9 && params_.lgwin > params_.lgblock) {
      params_.lgblock = std::min(21, params_.lgwin);
    }
  } else {
    params_.lgblock = std::min(kMaxInputBlockBits,
                               std::max(kMinInputBlockBits, params_.lgblock));
  }

  // Set maximum distance, see section 9.1. of the spec.
  max_backward_distance_ = (1 << params_.lgwin) - 16;

  // Initialize input and literal cost ring buffers.
  // We allocate at least lgwin + 1 bits for the ring buffer so that the newly
  // added block fits there completely and we still get lgwin bits and at least
  // read_block_size_bits + 1 bits because the copy tail length needs to be
  // smaller than ringbuffer size.
  int ringbuffer_bits = std::max(params_.lgwin + 1, params_.lgblock + 1);
  ringbuffer_.reset(new RingBuffer(ringbuffer_bits, params_.lgblock));
  literal_cost_.resize(1 << ringbuffer_bits);

  // Initialize storage.
  storage_size_ = 1 << 16;
  storage_.reset(new uint8_t[storage_size_]);
  if (params_.lgwin == 16) {
    last_byte_ = 0;
    last_byte_bits_ = 1;
  } else {
    last_byte_ = ((params_.lgwin - 17) << 1) | 1;
    last_byte_bits_ = 4;
  }

  // Initialize distance cache.
  dist_cache_[0] = 4;
  dist_cache_[1] = 11;
  dist_cache_[2] = 15;
  dist_cache_[3] = 16;

  // Initialize hashers.
  switch (params.mode) {
    case BrotliParams::MODE_TEXT: hash_type_ = 8; break;
    case BrotliParams::MODE_FONT: hash_type_ = 9; break;
    default: break;
  }
  hashers_->Init(hash_type_);
  if (params.mode == BrotliParams::MODE_TEXT) {
    StoreDictionaryWordHashes(params.enable_transforms);
  }

}

BrotliCompressor::~BrotliCompressor() {
}

StaticDictionary *BrotliCompressor::static_dictionary_ = NULL;

void BrotliCompressor::StoreDictionaryWordHashes(bool enable_transforms) {
  if (static_dictionary_ == NULL) {
    static_dictionary_ = new StaticDictionary;
    static_dictionary_->Fill(enable_transforms);
  }
  hashers_->SetStaticDictionary(static_dictionary_);
}

bool BrotliCompressor::WriteMetaBlock(const size_t input_size,
                                      const uint8_t* input_buffer,
                                      const bool is_last,
                                      size_t* encoded_size,
                                      uint8_t* encoded_buffer) {
  if (input_size > input_block_size()) {
    return false;
  }
  static const double kMinUTF8Ratio = 0.75;
  bool utf8_mode = false;
  std::vector<Command> commands((input_size + 1) >> 1);
  // Save the state of the distance cache in case we need to restore it for
  // emitting an uncompressed block.
  int saved_dist_cache[4];
  memcpy(saved_dist_cache, dist_cache_, sizeof(dist_cache_));
  if (input_size > 0) {
    ringbuffer_->Write(input_buffer, input_size);
    utf8_mode = IsMostlyUTF8(
        &ringbuffer_->start()[input_pos_ & ringbuffer_->mask()],
        input_size, kMinUTF8Ratio);
    if (utf8_mode) {
      EstimateBitCostsForLiteralsUTF8(input_pos_, input_size,
                                      ringbuffer_->mask(), ringbuffer_->mask(),
                                      ringbuffer_->start(), &literal_cost_[0]);
    } else {
      EstimateBitCostsForLiterals(input_pos_, input_size,
                                  ringbuffer_->mask(), ringbuffer_->mask(),
                                  ringbuffer_->start(), &literal_cost_[0]);
    }
    int last_insert_len = 0;
    int num_commands = 0;
    double base_min_score = 8.115;
    CreateBackwardReferences(
        input_size, input_pos_,
        ringbuffer_->start(), ringbuffer_->mask(),
        &literal_cost_[0], ringbuffer_->mask(),
        max_backward_distance_,
        base_min_score,
        params_.quality,
        hashers_.get(),
        hash_type_,
        dist_cache_,
        &last_insert_len,
        &commands[0],
        &num_commands);
    commands.resize(num_commands);
    if (last_insert_len > 0) {
      commands.push_back(Command(last_insert_len));
    }
  }
  int num_direct_distance_codes =
      params_.mode == BrotliParams::MODE_FONT ? 12 : 0;
  int distance_postfix_bits = params_.mode == BrotliParams::MODE_FONT ? 1 : 0;
  int literal_context_mode = CONTEXT_SIGNED;
  const size_t max_out_size = 2 * input_size + 500;
  uint8_t* storage = GetBrotliStorage(max_out_size);
  storage[0] = last_byte_;
  int storage_ix = last_byte_bits_;

  MetaBlockSplit mb;
  size_t len = MetaBlockLength(commands);
  if (!commands.empty()) {
    if (params_.greedy_block_split) {
      BuildMetaBlockGreedy(ringbuffer_->start(), input_pos_,
                           ringbuffer_->mask(),
                           commands.data(), commands.size(), params_.quality,
                           &mb);
    } else {
      RecomputeDistancePrefixes(&commands,
                                num_direct_distance_codes,
                                distance_postfix_bits);
      BuildMetaBlock(ringbuffer_->start(), input_pos_, ringbuffer_->mask(),
                     commands,
                     num_direct_distance_codes,
                     distance_postfix_bits,
                     literal_context_mode,
                     &mb);
    }
  }
  if (!StoreMetaBlock(ringbuffer_->start(), input_pos_, len,
                      ringbuffer_->mask(),
                      is_last, params_.quality,
                      num_direct_distance_codes,
                      distance_postfix_bits,
                      literal_context_mode,
                      commands.data(), commands.size(),
                      mb,
                      &storage_ix, storage)) {
    return false;
  }
  size_t output_size = storage_ix >> 3;
  if (input_size + 4 < output_size) {
    // Restore the distance cache and last byte.
    memcpy(dist_cache_, saved_dist_cache, sizeof(dist_cache_));
    storage[0] = last_byte_;
    storage_ix = last_byte_bits_;
    if (!StoreUncompressedMetaBlock(is_last,
                                    ringbuffer_->start(), input_pos_,
                                    ringbuffer_->mask(), len,
                                    &storage_ix, storage)) {
      return false;
    }
    output_size = storage_ix >> 3;
  }
  if (output_size > *encoded_size) {
    return false;
  }
  memcpy(encoded_buffer, storage, output_size);
  *encoded_size = output_size;
  last_byte_ = storage[output_size];
  last_byte_bits_ = storage_ix & 7;
  input_pos_ += len;
  return true;
}

bool BrotliCompressor::FinishStream(
    size_t* encoded_size, uint8_t* encoded_buffer) {
  return WriteMetaBlock(0, NULL, true, encoded_size, encoded_buffer);
}

int BrotliCompressBuffer(BrotliParams params,
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

  BrotliCompressor compressor(params);
  const int max_block_size = compressor.input_block_size();
  size_t max_output_size = *encoded_size;
  const uint8_t* input_end = input_buffer + input_size;
  *encoded_size = 0;

  while (input_buffer < input_end) {
    int block_size = max_block_size;
    bool is_last = false;
    if (block_size >= input_end - input_buffer) {
      block_size = input_end - input_buffer;
      is_last = true;
    }
    size_t output_size = max_output_size;
    if (!compressor.WriteMetaBlock(block_size, input_buffer,
                                   is_last, &output_size,
                                   &encoded_buffer[*encoded_size])) {
      return 0;
    }
    input_buffer += block_size;
    *encoded_size += output_size;
    max_output_size -= output_size;
  }
  return 1;
}

}  // namespace brotli
