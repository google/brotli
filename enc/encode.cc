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

static const double kMinUTF8Ratio = 0.75;
static const int kMinQualityForBlockSplit = 4;
static const int kMinQualityForContextModeling = 5;
static const int kMinQualityForOptimizeHistograms = 4;

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

void RecomputeDistancePrefixes(Command* cmds,
                               size_t num_commands,
                               int num_direct_distance_codes,
                               int distance_postfix_bits) {
  if (num_direct_distance_codes == 0 && distance_postfix_bits == 0) {
    return;
  }
  for (int i = 0; i < num_commands; ++i) {
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
      input_pos_(0),
      num_commands_(0),
      num_literals_(0),
      last_insert_len_(0),
      last_flush_pos_(0),
      last_processed_pos_(0),
      prev_byte_(0),
      prev_byte2_(0),
      storage_size_(0) {
  // Sanitize params.
  params_.quality = std::max(1, params_.quality);
  if (params_.lgwin < kMinWindowBits) {
    params_.lgwin = kMinWindowBits;
  } else if (params_.lgwin > kMaxWindowBits) {
    params_.lgwin = kMaxWindowBits;
  }
  if (params_.lgblock == 0) {
    params_.lgblock = params_.quality < kMinQualityForBlockSplit ? 14 : 16;
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
  if (params_.quality > 9) {
    literal_cost_mask_ = (1 << params_.lgblock) - 1;
    literal_cost_.reset(new float[literal_cost_mask_ + 1]);
  }

  // Allocate command buffer.
  cmd_buffer_size_ = std::max(1 << 18, 1 << params_.lgblock);
  commands_.reset(new brotli::Command[cmd_buffer_size_]);

  // Initialize last byte with stream header.
  if (params_.lgwin == 16) {
    last_byte_ = 0;
    last_byte_bits_ = 1;
  } else if (params_.lgwin == 17) {
    last_byte_ = 1;
    last_byte_bits_ = 7;
  } else if (params_.lgwin > 17) {
    last_byte_ = ((params_.lgwin - 17) << 1) | 1;
    last_byte_bits_ = 4;
  } else {
    last_byte_ = ((params_.lgwin - 8) << 4) | 1;
    last_byte_bits_ = 7;
  }

  // Initialize distance cache.
  dist_cache_[0] = 4;
  dist_cache_[1] = 11;
  dist_cache_[2] = 15;
  dist_cache_[3] = 16;
  // Save the state of the distance cache in case we need to restore it for
  // emitting an uncompressed block.
  memcpy(saved_dist_cache_, dist_cache_, sizeof(dist_cache_));

  // Initialize hashers.
  hash_type_ = std::min(9, params_.quality);
  hashers_->Init(hash_type_);
}

BrotliCompressor::~BrotliCompressor() {
}

void BrotliCompressor::CopyInputToRingBuffer(const size_t input_size,
                                             const uint8_t* input_buffer) {
  ringbuffer_->Write(input_buffer, input_size);
  input_pos_ += input_size;

  // Erase a few more bytes in the ring buffer to make hashing not
  // depend on uninitialized data. This makes compression deterministic
  // and it prevents uninitialized memory warnings in Valgrind. Even
  // without erasing, the output would be valid (but nondeterministic).
  //
  // Background information: The compressor stores short (at most 8 bytes)
  // substrings of the input already read in a hash table, and detects
  // repetitions by looking up such substrings in the hash table. If it
  // can find a substring, it checks whether the substring is really there
  // in the ring buffer (or it's just a hash collision). Should the hash
  // table become corrupt, this check makes sure that the output is
  // still valid, albeit the compression ratio would be bad.
  //
  // The compressor populates the hash table from the ring buffer as it's
  // reading new bytes from the input. However, at the last few indexes of
  // the ring buffer, there are not enough bytes to build full-length
  // substrings from. Since the hash table always contains full-length
  // substrings, we erase with dummy 0s here to make sure that those
  // substrings will contain 0s at the end instead of uninitialized
  // data.
  //
  // Please note that erasing is not necessary (because the
  // memory region is already initialized since he ring buffer
  // has a `tail' that holds a copy of the beginning,) so we
  // skip erasing if we have already gone around at least once in
  // the ring buffer.
  size_t pos = ringbuffer_->position();
  // Only clear during the first round of ringbuffer writes. On
  // subsequent rounds data in the ringbuffer would be affected.
  if (pos <= ringbuffer_->mask()) {
    // This is the first time when the ring buffer is being written.
    // We clear 3 bytes just after the bytes that have been copied from
    // the input buffer.
    //
    // The ringbuffer has a "tail" that holds a copy of the beginning,
    // but only once the ring buffer has been fully written once, i.e.,
    // pos <= mask. For the first time, we need to write values
    // in this tail (where index may be larger than mask), so that
    // we have exactly defined behavior and don't read un-initialized
    // memory. Due to performance reasons, hashing reads data using a
    // LOAD32, which can go 3 bytes beyond the bytes written in the
    // ringbuffer.
    memset(ringbuffer_->start() + pos, 0, 3);
  }
}

void BrotliCompressor::BrotliSetCustomDictionary(
    const size_t size, const uint8_t* dict) {
  CopyInputToRingBuffer(size, dict);
  last_flush_pos_ = size;
  last_processed_pos_ = size;
  if (size > 0) {
    prev_byte_ = dict[size - 1];
  }
  if (size > 1) {
    prev_byte2_ = dict[size - 2];
  }
  hashers_->PrependCustomDictionary(hash_type_, size, dict);
}

bool BrotliCompressor::WriteBrotliData(const bool is_last,
                                       const bool force_flush,
                                       size_t* out_size,
                                       uint8_t** output) {
  const size_t bytes = input_pos_ - last_processed_pos_;
  const uint8_t* data = ringbuffer_->start();
  const size_t mask = ringbuffer_->mask();

  if (bytes > input_block_size()) {
    return false;
  }

  bool utf8_mode =
      params_.quality >= 9 &&
      IsMostlyUTF8(&data[last_processed_pos_ & mask], bytes, kMinUTF8Ratio);

  if (literal_cost_.get()) {
    if (utf8_mode) {
      EstimateBitCostsForLiteralsUTF8(last_processed_pos_, bytes, mask,
                                      literal_cost_mask_, data,
                                      literal_cost_.get());
    } else {
      EstimateBitCostsForLiterals(last_processed_pos_, bytes, mask,
                                  literal_cost_mask_,
                                  data, literal_cost_.get());
    }
  }
  CreateBackwardReferences(bytes, last_processed_pos_, data, mask,
                           literal_cost_.get(),
                           literal_cost_mask_,
                           max_backward_distance_,
                           params_.quality,
                           hashers_.get(),
                           hash_type_,
                           dist_cache_,
                           &last_insert_len_,
                           &commands_[num_commands_],
                           &num_commands_,
                           &num_literals_);

  // For quality 1 there is no block splitting, so we buffer at most this much
  // literals and commands.
  static const int kMaxNumDelayedSymbols = 0x2fff;
  int max_length = std::min<int>(mask + 1, 1 << kMaxInputBlockBits);
  if (!is_last && !force_flush &&
      (params_.quality >= kMinQualityForBlockSplit ||
       (num_literals_ + num_commands_ < kMaxNumDelayedSymbols)) &&
      num_commands_ + (input_block_size() >> 1) < cmd_buffer_size_ &&
      input_pos_ + input_block_size() <= last_flush_pos_ + max_length) {
    // Everything will happen later.
    last_processed_pos_ = input_pos_;
    *out_size = 0;
    return true;
  }

  // Create the last insert-only command.
  if (last_insert_len_ > 0) {
    brotli::Command cmd(last_insert_len_);
    commands_[num_commands_++] = cmd;
    num_literals_ += last_insert_len_;
    last_insert_len_ = 0;
  }

  return WriteMetaBlockInternal(is_last, utf8_mode, out_size, output);
}

// Decide about the context map based on the ability of the prediction
// ability of the previous byte UTF8-prefix on the next byte. The
// prediction ability is calculated as shannon entropy. Here we need
// shannon entropy instead of 'BitsEntropy' since the prefix will be
// encoded with the remaining 6 bits of the following byte, and
// BitsEntropy will assume that symbol to be stored alone using Huffman
// coding.
void ChooseContextMap(int quality,
                      int* bigram_histo,
                      int* num_literal_contexts,
                      const int** literal_context_map) {
  int monogram_histo[3] = { 0 };
  int two_prefix_histo[6] = { 0 };
  int total = 0;
  for (int i = 0; i < 9; ++i) {
    total += bigram_histo[i];
    monogram_histo[i % 3] += bigram_histo[i];
    int j = i;
    if (j >= 6) {
      j -= 6;
    }
    two_prefix_histo[j] += bigram_histo[i];
  }
  int dummy;
  double entropy1 = ShannonEntropy(monogram_histo, 3, &dummy);
  double entropy2 = (ShannonEntropy(two_prefix_histo, 3, &dummy) +
                     ShannonEntropy(two_prefix_histo + 3, 3, &dummy));
  double entropy3 = 0;
  for (int k = 0; k < 3; ++k) {
    entropy3 += ShannonEntropy(bigram_histo + 3 * k, 3, &dummy);
  }
  entropy1 *= (1.0 / total);
  entropy2 *= (1.0 / total);
  entropy3 *= (1.0 / total);

  static const int kStaticContextMapContinuation[64] = {
    1, 1, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  };
  static const int kStaticContextMapSimpleUTF8[64] = {
    0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  };
  if (quality < 7) {
    // 3 context models is a bit slower, don't use it at lower qualities.
    entropy3 = entropy1 * 10;
  }
  // If expected savings by symbol are less than 0.2 bits, skip the
  // context modeling -- in exchange for faster decoding speed.
  if (entropy1 - entropy2 < 0.2 &&
      entropy1 - entropy3 < 0.2) {
    *num_literal_contexts = 1;
  } else if (entropy2 - entropy3 < 0.02) {
    *num_literal_contexts = 2;
    *literal_context_map = kStaticContextMapSimpleUTF8;
  } else {
    *num_literal_contexts = 3;
    *literal_context_map = kStaticContextMapContinuation;
  }
}

void DecideOverLiteralContextModeling(const uint8_t* input,
                                      size_t start_pos,
                                      size_t length,
                                      size_t mask,
                                      int quality,
                                      int* literal_context_mode,
                                      int* num_literal_contexts,
                                      const int** literal_context_map) {
  if (quality < kMinQualityForContextModeling || length < 64) {
    return;
  }
  // Gather bigram data of the UTF8 byte prefixes. To make the analysis of
  // UTF8 data faster we only examine 64 byte long strides at every 4kB
  // intervals.
  const size_t end_pos = start_pos + length;
  int bigram_prefix_histo[9] = { 0 };
  for (; start_pos + 64 < end_pos; start_pos += 4096) {
      static const int lut[4] = { 0, 0, 1, 2 };
    const size_t stride_end_pos = start_pos + 64;
    int prev = lut[input[start_pos & mask] >> 6] * 3;
    for (size_t pos = start_pos + 1; pos < stride_end_pos; ++pos) {
      const uint8_t literal = input[pos & mask];
      ++bigram_prefix_histo[prev + lut[literal >> 6]];
      prev = lut[literal >> 6] * 3;
    }
  }
  *literal_context_mode = CONTEXT_UTF8;
  ChooseContextMap(quality, &bigram_prefix_histo[0], num_literal_contexts,
                   literal_context_map);
}

bool BrotliCompressor::WriteMetaBlockInternal(const bool is_last,
                                              const bool utf8_mode,
                                              size_t* out_size,
                                              uint8_t** output) {
  const size_t bytes = input_pos_ - last_flush_pos_;
  const uint8_t* data = ringbuffer_->start();
  const size_t mask = ringbuffer_->mask();
  const size_t max_out_size = 2 * bytes + 500;
  uint8_t* storage = GetBrotliStorage(max_out_size);
  storage[0] = last_byte_;
  int storage_ix = last_byte_bits_;

  bool uncompressed = false;
  if (num_commands_ < (bytes >> 8) + 2) {
    if (num_literals_ > 0.99 * bytes) {
      int literal_histo[256] = { 0 };
      static const int kSampleRate = 13;
      static const double kMinEntropy = 7.92;
      static const double kBitCostThreshold = bytes * kMinEntropy / kSampleRate;
      for (int i = last_flush_pos_; i < input_pos_; i += kSampleRate) {
        ++literal_histo[data[i & mask]];
      }
      if (BitsEntropy(literal_histo, 256) > kBitCostThreshold) {
        uncompressed = true;
      }
    }
  }

  if (bytes == 0) {
    if (!StoreCompressedMetaBlockHeader(is_last, 0, &storage_ix, &storage[0])) {
      return false;
    }
    storage_ix = (storage_ix + 7) & ~7;
  } else if (uncompressed) {
    // Restore the distance cache, as its last update by
    // CreateBackwardReferences is now unused.
    memcpy(dist_cache_, saved_dist_cache_, sizeof(dist_cache_));
    if (!StoreUncompressedMetaBlock(is_last,
                                    data, last_flush_pos_, mask, bytes,
                                    &storage_ix,
                                    &storage[0])) {
      return false;
    }
  } else {
    int num_direct_distance_codes = 0;
    int distance_postfix_bits = 0;
    if (params_.quality > 9 && params_.mode == BrotliParams::MODE_FONT) {
      num_direct_distance_codes = 12;
      distance_postfix_bits = 1;
      RecomputeDistancePrefixes(commands_.get(),
                                num_commands_,
                                num_direct_distance_codes,
                                distance_postfix_bits);
    }
    if (params_.quality < kMinQualityForBlockSplit) {
      if (!StoreMetaBlockTrivial(data, last_flush_pos_, bytes, mask, is_last,
                                 commands_.get(), num_commands_,
                                 &storage_ix,
                                 &storage[0])) {
        return false;
      }
    } else {
      MetaBlockSplit mb;
      int literal_context_mode = utf8_mode ? CONTEXT_UTF8 : CONTEXT_SIGNED;
      if (params_.quality <= 9) {
        int num_literal_contexts = 1;
        const int* literal_context_map = NULL;
        DecideOverLiteralContextModeling(data, last_flush_pos_, bytes, mask,
                                         params_.quality,
                                         &literal_context_mode,
                                         &num_literal_contexts,
                                         &literal_context_map);
        if (literal_context_map == NULL) {
          BuildMetaBlockGreedy(data, last_flush_pos_, mask,
                               commands_.get(), num_commands_,
                               &mb);
        } else {
          BuildMetaBlockGreedyWithContexts(data, last_flush_pos_, mask,
                                           prev_byte_, prev_byte2_,
                                           literal_context_mode,
                                           num_literal_contexts,
                                           literal_context_map,
                                           commands_.get(), num_commands_,
                                           &mb);
        }
      } else {
        BuildMetaBlock(data, last_flush_pos_, mask,
                       prev_byte_, prev_byte2_,
                       commands_.get(), num_commands_,
                       literal_context_mode,
                       &mb);
      }
      if (params_.quality >= kMinQualityForOptimizeHistograms) {
        OptimizeHistograms(num_direct_distance_codes,
                           distance_postfix_bits,
                           &mb);
      }
      if (!StoreMetaBlock(data, last_flush_pos_, bytes, mask,
                          prev_byte_, prev_byte2_,
                          is_last,
                          num_direct_distance_codes,
                          distance_postfix_bits,
                          literal_context_mode,
                          commands_.get(), num_commands_,
                          mb,
                          &storage_ix,
                          &storage[0])) {
        return false;
      }
    }
    if (bytes + 4 < (storage_ix >> 3)) {
      // Restore the distance cache and last byte.
      memcpy(dist_cache_, saved_dist_cache_, sizeof(dist_cache_));
      storage[0] = last_byte_;
      storage_ix = last_byte_bits_;
      if (!StoreUncompressedMetaBlock(is_last, data, last_flush_pos_, mask,
                                      bytes, &storage_ix, &storage[0])) {
        return false;
      }
    }
  }
  last_byte_ = storage[storage_ix >> 3];
  last_byte_bits_ = storage_ix & 7;
  last_flush_pos_ = input_pos_;
  last_processed_pos_ = input_pos_;
  prev_byte_ = data[(last_flush_pos_ - 1) & mask];
  prev_byte2_ = data[(last_flush_pos_ - 2) & mask];
  num_commands_ = 0;
  num_literals_ = 0;
  // Save the state of the distance cache in case we need to restore it for
  // emitting an uncompressed block.
  memcpy(saved_dist_cache_, dist_cache_, sizeof(dist_cache_));
  *output = &storage[0];
  *out_size = storage_ix >> 3;
  return true;
}

bool BrotliCompressor::WriteMetaBlock(const size_t input_size,
                                      const uint8_t* input_buffer,
                                      const bool is_last,
                                      size_t* encoded_size,
                                      uint8_t* encoded_buffer) {
  CopyInputToRingBuffer(input_size, input_buffer);
  size_t out_size = 0;
  uint8_t* output;
  if (!WriteBrotliData(is_last, /* force_flush = */ true, &out_size, &output) ||
      out_size > *encoded_size) {
    return false;
  }
  if (out_size > 0) {
    memcpy(encoded_buffer, output, out_size);
  }
  *encoded_size = out_size;
  return true;
}

bool BrotliCompressor::WriteMetadata(const size_t input_size,
                                     const uint8_t* input_buffer,
                                     const bool is_last,
                                     size_t* encoded_size,
                                     uint8_t* encoded_buffer) {
  if (input_size > (1 << 24) || input_size + 6 > *encoded_size) {
    return false;
  }
  int storage_ix = last_byte_bits_;
  encoded_buffer[0] = last_byte_;
  WriteBits(1, 0, &storage_ix, encoded_buffer);
  WriteBits(2, 3, &storage_ix, encoded_buffer);
  WriteBits(1, 0, &storage_ix, encoded_buffer);
  if (input_size == 0) {
    WriteBits(2, 0, &storage_ix, encoded_buffer);
    *encoded_size = (storage_ix + 7) >> 3;
  } else {
    size_t nbits = Log2Floor(input_size - 1) + 1;
    size_t nbytes = (nbits + 7) / 8;
    WriteBits(2, nbytes, &storage_ix, encoded_buffer);
    WriteBits(8 * nbytes, input_size - 1, &storage_ix, encoded_buffer);
    size_t hdr_size = (storage_ix + 7) >> 3;
    memcpy(&encoded_buffer[hdr_size], input_buffer, input_size);
    *encoded_size = hdr_size + input_size;
  }
  if (is_last) {
    encoded_buffer[(*encoded_size)++] = 3;
  }
  last_byte_ = 0;
  last_byte_bits_ = 0;
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
  }
  BrotliCompressor compressor(params);
  BrotliMemIn in(input_buffer, input_size);
  BrotliMemOut out(encoded_buffer, *encoded_size);
  if (!BrotliCompress(params, &in, &out)) {
    return 0;
  }
  *encoded_size = out.position();
  return 1;
}

size_t CopyOneBlockToRingBuffer(BrotliIn* r, BrotliCompressor* compressor) {
  const size_t block_size = compressor->input_block_size();
  size_t bytes_read = 0;
  const uint8_t* data = reinterpret_cast<const uint8_t*>(
      r->Read(block_size, &bytes_read));
  if (data == NULL) {
    return 0;
  }
  compressor->CopyInputToRingBuffer(bytes_read, data);

  // Read more bytes until block_size is filled or an EOF (data == NULL) is
  // received. This is useful to get deterministic compressed output for the
  // same input no matter how r->Read splits the input to chunks.
  for (size_t remaining = block_size - bytes_read; remaining > 0; ) {
    size_t more_bytes_read = 0;
    data = reinterpret_cast<const uint8_t*>(
        r->Read(remaining, &more_bytes_read));
    if (data == NULL) {
      break;
    }
    compressor->CopyInputToRingBuffer(more_bytes_read, data);
    bytes_read += more_bytes_read;
    remaining -= more_bytes_read;
  }
  return bytes_read;
}

bool BrotliInIsFinished(BrotliIn* r) {
  size_t read_bytes;
  return r->Read(0, &read_bytes) == NULL;
}

int BrotliCompress(BrotliParams params, BrotliIn* in, BrotliOut* out) {
  return BrotliCompressWithCustomDictionary(0, nullptr, params, in, out);
}

int BrotliCompressWithCustomDictionary(size_t dictsize, const uint8_t* dict,
                                       BrotliParams params,
                                       BrotliIn* in, BrotliOut* out) {
  size_t in_bytes = 0;
  size_t out_bytes = 0;
  uint8_t* output;
  bool final_block = false;
  BrotliCompressor compressor(params);
  if (dictsize != 0) compressor.BrotliSetCustomDictionary(dictsize, dict);
  while (!final_block) {
    in_bytes = CopyOneBlockToRingBuffer(in, &compressor);
    final_block = in_bytes == 0 || BrotliInIsFinished(in);
    out_bytes = 0;
    if (!compressor.WriteBrotliData(final_block,
                                    /* force_flush = */ false,
                                    &out_bytes, &output)) {
      return false;
    }
    if (out_bytes > 0 && !out->Write(output, out_bytes)) {
      return false;
    }
  }
  return true;
}

}  // namespace brotli
