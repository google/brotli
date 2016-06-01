/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

// Implementation of Brotli compressor.

#include "./encode.h"

#include <algorithm>
#include <cstdlib>  /* free, malloc */
#include <cstring>  /* memset */
#include <limits>

#include "./backward_references.h"
#include "./bit_cost.h"
#include "./block_splitter.h"
#include "./brotli_bit_stream.h"
#include "./cluster.h"
#include "./context.h"
#include "./metablock.h"
#include "./transform.h"
#include "./compress_fragment.h"
#include "./compress_fragment_two_pass.h"
#include "./entropy_encode.h"
#include "./fast_log.h"
#include "./hash.h"
#include "./histogram.h"
#include "./prefix.h"
#include "./utf8_util.h"
#include "./write_bits.h"

namespace brotli {

static const int kMinQualityForBlockSplit = 4;
static const int kMinQualityForContextModeling = 5;
static const int kMinQualityForOptimizeHistograms = 4;
// For quality 2 there is no block splitting, so we buffer at most this much
// literals and commands.
static const size_t kMaxNumDelayedSymbols = 0x2fff;

#define COPY_ARRAY(dst, src) memcpy(dst, src, sizeof(src));

static void RecomputeDistancePrefixes(Command* cmds,
                                      size_t num_commands,
                                      uint32_t num_direct_distance_codes,
                                      uint32_t distance_postfix_bits) {
  if (num_direct_distance_codes == 0 && distance_postfix_bits == 0) {
    return;
  }
  for (size_t i = 0; i < num_commands; ++i) {
    Command* cmd = &cmds[i];
    if (cmd->copy_len() && cmd->cmd_prefix_ >= 128) {
      PrefixEncodeCopyDistance(cmd->DistanceCode(),
                               num_direct_distance_codes,
                               distance_postfix_bits,
                               &cmd->dist_prefix_,
                               &cmd->dist_extra_);
    }
  }
}

/* Wraps 64-bit input position to 32-bit ringbuffer position preserving
   "not-a-first-lap" feature. */
static uint32_t WrapPosition(uint64_t position) {
  uint32_t result = static_cast<uint32_t>(position);
  if (position > (1u << 30)) {
    result = (result & ((1u << 30) - 1)) | (1u << 30);
  }
  return result;
}

uint8_t* BrotliCompressor::GetBrotliStorage(size_t size) {
  if (storage_size_ < size) {
    delete[] storage_;
    storage_ = new uint8_t[size];
    storage_size_ = size;
  }
  return storage_;
}

static size_t MaxHashTableSize(int quality) {
  return quality == 0 ? 1 << 15 : 1 << 17;
}

static size_t HashTableSize(size_t max_table_size, size_t input_size) {
  size_t htsize = 256;
  while (htsize < max_table_size && htsize < input_size) {
    htsize <<= 1;
  }
  return htsize;
}

int* BrotliCompressor::GetHashTable(int quality,
                                    size_t input_size,
                                    size_t* table_size) {
  // Use smaller hash table when input.size() is smaller, since we
  // fill the table, incurring O(hash table size) overhead for
  // compression, and if the input is short, we won't need that
  // many hash table entries anyway.
  const size_t max_table_size = MaxHashTableSize(quality);
  assert(max_table_size >= 256);
  size_t htsize = HashTableSize(max_table_size, input_size);

  int* table;
  if (htsize <= sizeof(small_table_) / sizeof(small_table_[0])) {
    table = small_table_;
  } else {
    if (large_table_ == NULL) {
      large_table_ = new int[max_table_size];
    }
    table = large_table_;
  }

  *table_size = htsize;
  memset(table, 0, htsize * sizeof(*table));
  return table;
}

static void EncodeWindowBits(int lgwin, uint8_t* last_byte,
                             uint8_t* last_byte_bits) {
  if (lgwin == 16) {
    *last_byte = 0;
    *last_byte_bits = 1;
  } else if (lgwin == 17) {
    *last_byte = 1;
    *last_byte_bits = 7;
  } else if (lgwin > 17) {
    *last_byte = static_cast<uint8_t>(((lgwin - 17) << 1) | 1);
    *last_byte_bits = 4;
  } else {
    *last_byte = static_cast<uint8_t>(((lgwin - 8) << 4) | 1);
    *last_byte_bits = 7;
  }
}

// Initializes the command and distance prefix codes for the first block.
static void InitCommandPrefixCodes(uint8_t cmd_depths[128],
                                   uint16_t cmd_bits[128],
                                   uint8_t cmd_code[512],
                                   size_t* cmd_code_numbits) {
  static const uint8_t kDefaultCommandDepths[128] = {
    0, 4, 4, 5, 6, 6, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8,
    0, 0, 0, 4, 4, 4, 4, 4, 5, 5, 6, 6, 6, 6, 7, 7,
    7, 7, 10, 10, 10, 10, 10, 10, 0, 4, 4, 5, 5, 5, 6, 6,
    7, 8, 8, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    6, 6, 6, 6, 6, 6, 5, 5, 5, 5, 5, 5, 4, 4, 4, 4,
    4, 4, 4, 5, 5, 5, 5, 5, 5, 6, 6, 7, 7, 7, 8, 10,
    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
  };
  static const uint16_t kDefaultCommandBits[128] = {
    0,   0,   8,   9,   3,  35,   7,   71,
    39, 103,  23,  47, 175, 111, 239,   31,
    0,   0,   0,   4,  12,   2,  10,    6,
    13,  29,  11,  43,  27,  59,  87,   55,
    15,  79, 319, 831, 191, 703, 447,  959,
    0,  14,   1,  25,   5,  21,  19,   51,
    119, 159,  95, 223, 479, 991,  63,  575,
    127, 639, 383, 895, 255, 767, 511, 1023,
    14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    27, 59, 7, 39, 23, 55, 30, 1, 17, 9, 25, 5, 0, 8, 4, 12,
    2, 10, 6, 21, 13, 29, 3, 19, 11, 15, 47, 31, 95, 63, 127, 255,
    767, 2815, 1791, 3839, 511, 2559, 1535, 3583, 1023, 3071, 2047, 4095,
  };
  COPY_ARRAY(cmd_depths, kDefaultCommandDepths);
  COPY_ARRAY(cmd_bits, kDefaultCommandBits);

  // Initialize the pre-compressed form of the command and distance prefix
  // codes.
  static const uint8_t kDefaultCommandCode[] = {
    0xff, 0x77, 0xd5, 0xbf, 0xe7, 0xde, 0xea, 0x9e, 0x51, 0x5d, 0xde, 0xc6,
    0x70, 0x57, 0xbc, 0x58, 0x58, 0x58, 0xd8, 0xd8, 0x58, 0xd5, 0xcb, 0x8c,
    0xea, 0xe0, 0xc3, 0x87, 0x1f, 0x83, 0xc1, 0x60, 0x1c, 0x67, 0xb2, 0xaa,
    0x06, 0x83, 0xc1, 0x60, 0x30, 0x18, 0xcc, 0xa1, 0xce, 0x88, 0x54, 0x94,
    0x46, 0xe1, 0xb0, 0xd0, 0x4e, 0xb2, 0xf7, 0x04, 0x00,
  };
  static const int kDefaultCommandCodeNumBits = 448;
  COPY_ARRAY(cmd_code, kDefaultCommandCode);
  *cmd_code_numbits = kDefaultCommandCodeNumBits;
}

// Decide about the context map based on the ability of the prediction
// ability of the previous byte UTF8-prefix on the next byte. The
// prediction ability is calculated as shannon entropy. Here we need
// shannon entropy instead of 'BitsEntropy' since the prefix will be
// encoded with the remaining 6 bits of the following byte, and
// BitsEntropy will assume that symbol to be stored alone using Huffman
// coding.
static void ChooseContextMap(int quality,
                             uint32_t* bigram_histo,
                             size_t* num_literal_contexts,
                             const uint32_t** literal_context_map) {
  uint32_t monogram_histo[3] = { 0 };
  uint32_t two_prefix_histo[6] = { 0 };
  size_t total = 0;
  for (size_t i = 0; i < 9; ++i) {
    total += bigram_histo[i];
    monogram_histo[i % 3] += bigram_histo[i];
    size_t j = i;
    if (j >= 6) {
      j -= 6;
    }
    two_prefix_histo[j] += bigram_histo[i];
  }
  size_t dummy;
  double entropy1 = ShannonEntropy(monogram_histo, 3, &dummy);
  double entropy2 = (ShannonEntropy(two_prefix_histo, 3, &dummy) +
                     ShannonEntropy(two_prefix_histo + 3, 3, &dummy));
  double entropy3 = 0;
  for (size_t k = 0; k < 3; ++k) {
    entropy3 += ShannonEntropy(bigram_histo + 3 * k, 3, &dummy);
  }

  assert(total != 0);
  double scale = 1.0 / static_cast<double>(total);
  entropy1 *= scale;
  entropy2 *= scale;
  entropy3 *= scale;

  static const uint32_t kStaticContextMapContinuation[64] = {
    1, 1, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  };
  static const uint32_t kStaticContextMapSimpleUTF8[64] = {
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

static void DecideOverLiteralContextModeling(
    const uint8_t* input,
    size_t start_pos,
    size_t length,
    size_t mask,
    int quality,
    ContextType* literal_context_mode,
    size_t* num_literal_contexts,
    const uint32_t** literal_context_map) {
  if (quality < kMinQualityForContextModeling || length < 64) {
    return;
  }
  // Gather bigram data of the UTF8 byte prefixes. To make the analysis of
  // UTF8 data faster we only examine 64 byte long strides at every 4kB
  // intervals.
  const size_t end_pos = start_pos + length;
  uint32_t bigram_prefix_histo[9] = { 0 };
  for (; start_pos + 64 <= end_pos; start_pos += 4096) {
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

static bool ShouldCompress(const uint8_t* data,
                           const size_t mask,
                           const uint64_t last_flush_pos,
                           const size_t bytes,
                           const size_t num_literals,
                           const size_t num_commands) {
  if (num_commands < (bytes >> 8) + 2) {
    if (num_literals > 0.99 * static_cast<double>(bytes)) {
      uint32_t literal_histo[256] = { 0 };
      static const uint32_t kSampleRate = 13;
      static const double kMinEntropy = 7.92;
      const double bit_cost_threshold =
          static_cast<double>(bytes) * kMinEntropy / kSampleRate;
      size_t t = (bytes + kSampleRate - 1) / kSampleRate;
      uint32_t pos = static_cast<uint32_t>(last_flush_pos);
      for (size_t i = 0; i < t; i++) {
        ++literal_histo[data[pos & mask]];
        pos += kSampleRate;
      }
      if (BitsEntropy(literal_histo, 256) > bit_cost_threshold) {
        return false;
      }
    }
  }
  return true;
}

static void WriteMetaBlockInternal(const uint8_t* data,
                                   const size_t mask,
                                   const uint64_t last_flush_pos,
                                   const size_t bytes,
                                   const bool is_last,
                                   const int quality,
                                   const bool font_mode,
                                   const uint8_t prev_byte,
                                   const uint8_t prev_byte2,
                                   const size_t num_literals,
                                   const size_t num_commands,
                                   Command* commands,
                                   const int* saved_dist_cache,
                                   int* dist_cache,
                                   size_t* storage_ix,
                                   uint8_t* storage) {
  if (bytes == 0) {
    // Write the ISLAST and ISEMPTY bits.
    WriteBits(2, 3, storage_ix, storage);
    *storage_ix = (*storage_ix + 7u) & ~7u;
    return;
  }

  if (!ShouldCompress(data, mask, last_flush_pos, bytes,
                      num_literals, num_commands)) {
    // Restore the distance cache, as its last update by
    // CreateBackwardReferences is now unused.
    memcpy(dist_cache, saved_dist_cache, 4 * sizeof(dist_cache[0]));
    StoreUncompressedMetaBlock(is_last, data,
                               WrapPosition(last_flush_pos), mask, bytes,
                               storage_ix, storage);
    return;
  }

  const uint8_t last_byte = storage[0];
  const uint8_t last_byte_bits = static_cast<uint8_t>(*storage_ix & 0xff);
  uint32_t num_direct_distance_codes = 0;
  uint32_t distance_postfix_bits = 0;
  if (quality > 9 && font_mode) {
    num_direct_distance_codes = 12;
    distance_postfix_bits = 1;
    RecomputeDistancePrefixes(commands,
                              num_commands,
                              num_direct_distance_codes,
                              distance_postfix_bits);
  }
  if (quality == 2) {
    StoreMetaBlockFast(data, WrapPosition(last_flush_pos),
                       bytes, mask, is_last,
                       commands, num_commands,
                       storage_ix, storage);
  } else if (quality < kMinQualityForBlockSplit) {
    StoreMetaBlockTrivial(data, WrapPosition(last_flush_pos),
                          bytes, mask, is_last,
                          commands, num_commands,
                          storage_ix, storage);
  } else {
    MetaBlockSplit mb;
    ContextType literal_context_mode = CONTEXT_UTF8;
    if (quality <= 9) {
      size_t num_literal_contexts = 1;
      const uint32_t* literal_context_map = NULL;
      DecideOverLiteralContextModeling(data, WrapPosition(last_flush_pos),
                                       bytes, mask,
                                       quality,
                                       &literal_context_mode,
                                       &num_literal_contexts,
                                       &literal_context_map);
      if (literal_context_map == NULL) {
        BuildMetaBlockGreedy(data, WrapPosition(last_flush_pos), mask,
                             commands, num_commands, &mb);
      } else {
        BuildMetaBlockGreedyWithContexts(data, WrapPosition(last_flush_pos),
                                         mask,
                                         prev_byte, prev_byte2,
                                         literal_context_mode,
                                         num_literal_contexts,
                                         literal_context_map,
                                         commands, num_commands,
                                         &mb);
      }
    } else {
      if (!IsMostlyUTF8(data, WrapPosition(last_flush_pos), mask, bytes,
                        kMinUTF8Ratio)) {
        literal_context_mode = CONTEXT_SIGNED;
      }
      BuildMetaBlock(data, WrapPosition(last_flush_pos), mask,
                     prev_byte, prev_byte2,
                     commands, num_commands,
                     literal_context_mode,
                     &mb);
    }
    if (quality >= kMinQualityForOptimizeHistograms) {
      OptimizeHistograms(num_direct_distance_codes,
                         distance_postfix_bits,
                         &mb);
    }
    StoreMetaBlock(data, WrapPosition(last_flush_pos), bytes, mask,
                   prev_byte, prev_byte2,
                   is_last,
                   num_direct_distance_codes,
                   distance_postfix_bits,
                   literal_context_mode,
                   commands, num_commands,
                   mb,
                   storage_ix, storage);
  }
  if (bytes + 4 < (*storage_ix >> 3)) {
    // Restore the distance cache and last byte.
    memcpy(dist_cache, saved_dist_cache, 4 * sizeof(dist_cache[0]));
    storage[0] = last_byte;
    *storage_ix = last_byte_bits;
    StoreUncompressedMetaBlock(is_last, data,
                               WrapPosition(last_flush_pos), mask,
                               bytes, storage_ix, storage);
  }
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
      storage_size_(0),
      storage_(0),
      large_table_(NULL),
      cmd_code_numbits_(0),
      command_buf_(NULL),
      literal_buf_(NULL),
      is_last_block_emitted_(0) {
  // Sanitize params.
  params_.quality = std::max(0, params_.quality);
  if (params_.lgwin < kMinWindowBits) {
    params_.lgwin = kMinWindowBits;
  } else if (params_.lgwin > kMaxWindowBits) {
    params_.lgwin = kMaxWindowBits;
  }
  if (params_.quality <= 1) {
    params_.lgblock = params_.lgwin;
  } else if (params_.quality < kMinQualityForBlockSplit) {
    params_.lgblock = 14;
  } else if (params_.lgblock == 0) {
    params_.lgblock = 16;
    if (params_.quality >= 9 && params_.lgwin > params_.lgblock) {
      params_.lgblock = std::min(18, params_.lgwin);
    }
  } else {
    params_.lgblock = std::min(kMaxInputBlockBits,
                               std::max(kMinInputBlockBits, params_.lgblock));
  }

  // Initialize input and literal cost ring buffers.
  // We allocate at least lgwin + 1 bits for the ring buffer so that the newly
  // added block fits there completely and we still get lgwin bits and at least
  // read_block_size_bits + 1 bits because the copy tail length needs to be
  // smaller than ringbuffer size.
  int ringbuffer_bits = std::max(params_.lgwin + 1, params_.lgblock + 1);
  ringbuffer_ = new RingBuffer(ringbuffer_bits, params_.lgblock);

  commands_ = 0;
  cmd_alloc_size_ = 0;

  // Initialize last byte with stream header.
  EncodeWindowBits(params_.lgwin, &last_byte_, &last_byte_bits_);

  // Initialize distance cache.
  dist_cache_[0] = 4;
  dist_cache_[1] = 11;
  dist_cache_[2] = 15;
  dist_cache_[3] = 16;
  // Save the state of the distance cache in case we need to restore it for
  // emitting an uncompressed block.
  memcpy(saved_dist_cache_, dist_cache_, sizeof(dist_cache_));

  if (params_.quality == 0) {
    InitCommandPrefixCodes(cmd_depths_, cmd_bits_,
                           cmd_code_, &cmd_code_numbits_);
  } else if (params_.quality == 1) {
    command_buf_ = new uint32_t[kCompressFragmentTwoPassBlockSize];
    literal_buf_ = new uint8_t[kCompressFragmentTwoPassBlockSize];
  }

  // Initialize hashers.
  hash_type_ = std::min(10, params_.quality);
  hashers_->Init(hash_type_);
}

BrotliCompressor::~BrotliCompressor(void) {
  delete[] storage_;
  free(commands_);
  delete ringbuffer_;
  delete hashers_;
  delete[] large_table_;
  delete[] command_buf_;
  delete[] literal_buf_;
}

void BrotliCompressor::CopyInputToRingBuffer(const size_t input_size,
                                             const uint8_t* input_buffer) {
  ringbuffer_->Write(input_buffer, input_size);
  input_pos_ += input_size;

  // TL;DR: If needed, initialize 7 more bytes in the ring buffer to make the
  // hashing not depend on uninitialized data. This makes compression
  // deterministic and it prevents uninitialized memory warnings in Valgrind.
  // Even without erasing, the output would be valid (but nondeterministic).
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
    // We clear 7 bytes just after the bytes that have been copied from
    // the input buffer.
    //
    // The ringbuffer has a "tail" that holds a copy of the beginning,
    // but only once the ring buffer has been fully written once, i.e.,
    // pos <= mask. For the first time, we need to write values
    // in this tail (where index may be larger than mask), so that
    // we have exactly defined behavior and don't read un-initialized
    // memory. Due to performance reasons, hashing reads data using a
    // LOAD64, which can go 7 bytes beyond the bytes written in the
    // ringbuffer.
    memset(ringbuffer_->start() + pos, 0, 7);
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
  hashers_->PrependCustomDictionary(hash_type_, params_.lgwin, size, dict);
}

bool BrotliCompressor::WriteBrotliData(const bool is_last,
                                       const bool force_flush,
                                       size_t* out_size,
                                       uint8_t** output) {
  const uint64_t delta = input_pos_ - last_processed_pos_;
  const uint8_t* data = ringbuffer_->start();
  const uint32_t mask = ringbuffer_->mask();

   /* Adding more blocks after "last" block is forbidden. */
  if (is_last_block_emitted_) return false;
  if (is_last) is_last_block_emitted_ = 1;

  if (delta > input_block_size()) {
    return false;
  }
  const uint32_t bytes = static_cast<uint32_t>(delta);

  if (params_.quality <= 1) {
    if (delta == 0 && !is_last) {
      // We have no new input data and we don't have to finish the stream, so
      // nothing to do.
      *out_size = 0;
      return true;
    }
    const size_t max_out_size = 2 * bytes + 500;
    uint8_t* storage = GetBrotliStorage(max_out_size);
    storage[0] = last_byte_;
    size_t storage_ix = last_byte_bits_;
    size_t table_size;
    int* table = GetHashTable(params_.quality, bytes, &table_size);
    if (params_.quality == 0) {
      BrotliCompressFragmentFast(
          &data[WrapPosition(last_processed_pos_) & mask],
          bytes, is_last,
          table, table_size,
          cmd_depths_, cmd_bits_,
          &cmd_code_numbits_, cmd_code_,
          &storage_ix, storage);
    } else {
      BrotliCompressFragmentTwoPass(
          &data[WrapPosition(last_processed_pos_) & mask],
          bytes, is_last,
          command_buf_, literal_buf_,
          table, table_size,
          &storage_ix, storage);
    }
    last_byte_ = storage[storage_ix >> 3];
    last_byte_bits_ = storage_ix & 7u;
    last_processed_pos_ = input_pos_;
    *output = &storage[0];
    *out_size = storage_ix >> 3;
    return true;
  }

  // Theoretical max number of commands is 1 per 2 bytes.
  size_t newsize = num_commands_ + bytes / 2 + 1;
  if (newsize > cmd_alloc_size_) {
    // Reserve a bit more memory to allow merging with a next block
    // without realloc: that would impact speed.
    newsize += (bytes / 4) + 16;
    cmd_alloc_size_ = newsize;
    commands_ =
        static_cast<Command*>(realloc(commands_, sizeof(Command) * newsize));
  }

  CreateBackwardReferences(bytes, WrapPosition(last_processed_pos_),
                           is_last, data, mask,
                           params_.quality,
                           params_.lgwin,
                           hashers_,
                           hash_type_,
                           dist_cache_,
                           &last_insert_len_,
                           &commands_[num_commands_],
                           &num_commands_,
                           &num_literals_);

  size_t max_length = std::min<size_t>(mask + 1, 1u << kMaxInputBlockBits);
  const size_t max_literals = max_length / 8;
  const size_t max_commands = max_length / 8;
  if (!is_last && !force_flush &&
      (params_.quality >= kMinQualityForBlockSplit ||
       (num_literals_ + num_commands_ < kMaxNumDelayedSymbols)) &&
      num_literals_ < max_literals &&
      num_commands_ < max_commands &&
      input_pos_ + input_block_size() <= last_flush_pos_ + max_length) {
    // Merge with next input block. Everything will happen later.
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

  if (!is_last && input_pos_ == last_flush_pos_) {
    // We have no new input data and we don't have to finish the stream, so
    // nothing to do.
    *out_size = 0;
    return true;
  }
  assert(input_pos_ >= last_flush_pos_);
  assert(input_pos_ > last_flush_pos_ || is_last);
  assert(input_pos_ - last_flush_pos_ <= 1u << 24);
  const uint32_t metablock_size =
      static_cast<uint32_t>(input_pos_ - last_flush_pos_);
  const size_t max_out_size = 2 * metablock_size + 500;
  uint8_t* storage = GetBrotliStorage(max_out_size);
  storage[0] = last_byte_;
  size_t storage_ix = last_byte_bits_;
  bool font_mode = params_.mode == BrotliParams::MODE_FONT;
  WriteMetaBlockInternal(
      data, mask, last_flush_pos_, metablock_size, is_last, params_.quality,
      font_mode, prev_byte_, prev_byte2_, num_literals_, num_commands_,
      commands_, saved_dist_cache_, dist_cache_, &storage_ix, storage);
  last_byte_ = storage[storage_ix >> 3];
  last_byte_bits_ = storage_ix & 7u;
  last_flush_pos_ = input_pos_;
  last_processed_pos_ = input_pos_;
  if (last_flush_pos_ > 0) {
    prev_byte_ = data[(static_cast<uint32_t>(last_flush_pos_) - 1) & mask];
  }
  if (last_flush_pos_ > 1) {
    prev_byte2_ = data[(static_cast<uint32_t>(last_flush_pos_) - 2) & mask];
  }
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
  uint64_t hdr_buffer_data[2];
  uint8_t* hdr_buffer = reinterpret_cast<uint8_t*>(&hdr_buffer_data[0]);
  size_t storage_ix = last_byte_bits_;
  hdr_buffer[0] = last_byte_;
  WriteBits(1, 0, &storage_ix, hdr_buffer);
  WriteBits(2, 3, &storage_ix, hdr_buffer);
  WriteBits(1, 0, &storage_ix, hdr_buffer);
  if (input_size == 0) {
    WriteBits(2, 0, &storage_ix, hdr_buffer);
    *encoded_size = (storage_ix + 7u) >> 3;
    memcpy(encoded_buffer, hdr_buffer, *encoded_size);
  } else {
    uint32_t nbits = (input_size == 1) ? 0 : (Log2FloorNonZero(
        static_cast<uint32_t>(input_size) - 1) + 1);
    uint32_t nbytes = (nbits + 7) / 8;
    WriteBits(2, nbytes, &storage_ix, hdr_buffer);
    WriteBits(8 * nbytes, input_size - 1, &storage_ix, hdr_buffer);
    size_t hdr_size = (storage_ix + 7u) >> 3;
    memcpy(encoded_buffer, hdr_buffer, hdr_size);
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

static int BrotliCompressBufferQuality10(int lgwin,
                                         size_t input_size,
                                         const uint8_t* input_buffer,
                                         size_t* encoded_size,
                                         uint8_t* encoded_buffer) {
  const size_t mask = std::numeric_limits<size_t>::max() >> 1;
  assert(input_size <= mask + 1);
  const size_t max_backward_limit = (1 << lgwin) - 16;
  int dist_cache[4] = { 4, 11, 15, 16 };
  int saved_dist_cache[4] = { 4, 11, 15, 16 };
  int ok = 1;
  const size_t max_out_size = *encoded_size;
  size_t total_out_size = 0;
  uint8_t last_byte;
  uint8_t last_byte_bits;
  EncodeWindowBits(lgwin, &last_byte, &last_byte_bits);

  Hashers::H10* hasher = new Hashers::H10;
  const size_t hasher_eff_size = std::min(input_size, max_backward_limit + 16);
  hasher->Init(lgwin, 0, hasher_eff_size, true);

  const int lgblock = std::min(18, lgwin);
  const int lgmetablock = std::min(24, lgwin + 1);
  const size_t max_block_size = static_cast<size_t>(1) << lgblock;
  const size_t max_metablock_size = static_cast<size_t>(1) << lgmetablock;
  const size_t max_literals_per_metablock = max_metablock_size / 8;
  const size_t max_commands_per_metablock = max_metablock_size / 8;
  size_t metablock_start = 0;
  uint8_t prev_byte = 0;
  uint8_t prev_byte2 = 0;
  while (ok && metablock_start < input_size) {
    const size_t metablock_end =
        std::min(input_size, metablock_start + max_metablock_size);
    const size_t expected_num_commands =
        (metablock_end - metablock_start) / 12 + 16;
    Command* commands = 0;
    size_t num_commands = 0;
    size_t last_insert_len = 0;
    size_t num_literals = 0;
    size_t metablock_size = 0;
    size_t cmd_alloc_size = 0;

    for (size_t block_start = metablock_start; block_start < metablock_end; ) {
      size_t block_size = std::min(metablock_end - block_start, max_block_size);
      ZopfliNode* nodes = new ZopfliNode[block_size + 1];
      std::vector<uint32_t> path;
      hasher->StitchToPreviousBlock(block_size, block_start,
                                    input_buffer, mask);
      ZopfliComputeShortestPath(block_size, block_start, input_buffer, mask,
                                max_backward_limit, dist_cache,
                                hasher, nodes, &path);
      // We allocate a command buffer in the first iteration of this loop that
      // will be likely big enough for the whole metablock, so that for most
      // inputs we will not have to reallocate in later iterations. We do the
      // allocation here and not before the loop, because if the input is small,
      // this will be allocated after the zopfli cost model is freed, so this
      // will not increase peak memory usage.
      // TODO: If the first allocation is too small, increase command
      // buffer size exponentially.
      size_t new_cmd_alloc_size = std::max(expected_num_commands,
                                           num_commands + path.size() + 1);
      if (cmd_alloc_size != new_cmd_alloc_size) {
        cmd_alloc_size = new_cmd_alloc_size;
        commands = static_cast<Command*>(
            realloc(commands, cmd_alloc_size * sizeof(Command)));
      }
      ZopfliCreateCommands(block_size, block_start, max_backward_limit, path,
                           &nodes[0], dist_cache, &last_insert_len,
                           &commands[num_commands], &num_literals);
      num_commands += path.size();
      block_start += block_size;
      metablock_size += block_size;
      delete[] nodes;
      if (num_literals > max_literals_per_metablock ||
          num_commands > max_commands_per_metablock) {
        break;
      }
    }

    if (last_insert_len > 0) {
      Command cmd(last_insert_len);
      commands[num_commands++] = cmd;
      num_literals += last_insert_len;
    }

    const bool is_last = (metablock_start + metablock_size == input_size);
    uint8_t* storage = NULL;
    size_t storage_ix = last_byte_bits;

    if (metablock_size == 0) {
      // Write the ISLAST and ISEMPTY bits.
      storage = new uint8_t[16];
      storage[0] = last_byte;
      WriteBits(2, 3, &storage_ix, storage);
      storage_ix = (storage_ix + 7u) & ~7u;
    } else if (!ShouldCompress(input_buffer, mask, metablock_start,
                               metablock_size, num_literals, num_commands)) {
      // Restore the distance cache, as its last update by
      // CreateBackwardReferences is now unused.
      memcpy(dist_cache, saved_dist_cache, 4 * sizeof(dist_cache[0]));
      storage = new uint8_t[metablock_size + 16];
      storage[0] = last_byte;
      StoreUncompressedMetaBlock(is_last, input_buffer,
                                 metablock_start, mask, metablock_size,
                                 &storage_ix, storage);
    } else {
      uint32_t num_direct_distance_codes = 0;
      uint32_t distance_postfix_bits = 0;
      MetaBlockSplit mb;
      ContextType literal_context_mode = CONTEXT_UTF8;
      if (!IsMostlyUTF8(
              input_buffer, metablock_start, mask, metablock_size,
              kMinUTF8Ratio)) {
        literal_context_mode = CONTEXT_SIGNED;
      }
      BuildMetaBlock(input_buffer, metablock_start, mask,
                     prev_byte, prev_byte2,
                     commands, num_commands,
                     literal_context_mode,
                     &mb);
      OptimizeHistograms(num_direct_distance_codes,
                         distance_postfix_bits,
                         &mb);
      const size_t max_out_metablock_size = 2 * metablock_size + 500;
      storage = new uint8_t[max_out_metablock_size];
      storage[0] = last_byte;
      StoreMetaBlock(input_buffer, metablock_start, metablock_size, mask,
                     prev_byte, prev_byte2,
                     is_last,
                     num_direct_distance_codes,
                     distance_postfix_bits,
                     literal_context_mode,
                     commands, num_commands,
                     mb,
                     &storage_ix, storage);
      if (metablock_size + 4 < (storage_ix >> 3)) {
        // Restore the distance cache and last byte.
        memcpy(dist_cache, saved_dist_cache, 4 * sizeof(dist_cache[0]));
        storage[0] = last_byte;
        storage_ix = last_byte_bits;
        StoreUncompressedMetaBlock(is_last, input_buffer,
                                   metablock_start, mask,
                                   metablock_size, &storage_ix, storage);
      }
    }
    last_byte = storage[storage_ix >> 3];
    last_byte_bits = storage_ix & 7u;
    metablock_start += metablock_size;
    prev_byte = input_buffer[metablock_start - 1];
    prev_byte2 = input_buffer[metablock_start - 2];
    // Save the state of the distance cache in case we need to restore it for
    // emitting an uncompressed block.
    memcpy(saved_dist_cache, dist_cache, 4 * sizeof(dist_cache[0]));

    const size_t out_size = storage_ix >> 3;
    total_out_size += out_size;
    if (total_out_size <= max_out_size) {
      memcpy(encoded_buffer, storage, out_size);
      encoded_buffer += out_size;
    } else {
      ok = 0;
    }
    delete[] storage;
    free(commands);
  }

  *encoded_size = total_out_size;
  delete hasher;
  return ok;
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
  if (input_size == 0) {
    // Handle the special case of empty input.
    *encoded_size = 1;
    *encoded_buffer = 6;
    return 1;
  }
  if (params.quality == 10) {
    // TODO: Implement this direct path for all quality levels.
    const int lgwin = std::min(24, std::max(16, params.lgwin));
    return BrotliCompressBufferQuality10(lgwin, input_size, input_buffer,
                                         encoded_size, encoded_buffer);
  }
  BrotliMemIn in(input_buffer, input_size);
  BrotliMemOut out(encoded_buffer, *encoded_size);
  if (!BrotliCompress(params, &in, &out)) {
    return 0;
  }
  *encoded_size = out.position();
  return 1;
}

static bool BrotliInIsFinished(BrotliIn* r) {
  size_t read_bytes;
  return r->Read(0, &read_bytes) == NULL;
}

static const uint8_t* BrotliInReadAndCheckEnd(const size_t block_size,
                                              BrotliIn* r,
                                              size_t* bytes_read,
                                              bool* is_last) {
  *bytes_read = 0;
  const uint8_t* data = reinterpret_cast<const uint8_t*>(
      r->Read(block_size, bytes_read));
  assert((data == NULL) == (*bytes_read == 0));
  *is_last = BrotliInIsFinished(r);
  return data;
}

static bool CopyOneBlockToRingBuffer(BrotliIn* r,
                                     BrotliCompressor* compressor,
                                     size_t* bytes_read,
                                     bool* is_last) {
  const size_t block_size = compressor->input_block_size();
  const uint8_t* data = BrotliInReadAndCheckEnd(block_size, r,
                                                bytes_read, is_last);
  if (data == NULL) {
    return *is_last;
  }
  compressor->CopyInputToRingBuffer(*bytes_read, data);

  // Read more bytes until block_size is filled or an EOF (data == NULL) is
  // received. This is useful to get deterministic compressed output for the
  // same input no matter how r->Read splits the input to chunks.
  for (size_t remaining = block_size - *bytes_read; remaining > 0; ) {
    size_t more_bytes_read = 0;
    data = BrotliInReadAndCheckEnd(remaining, r, &more_bytes_read, is_last);
    if (data == NULL) {
      return *is_last;
    }
    compressor->CopyInputToRingBuffer(more_bytes_read, data);
    *bytes_read += more_bytes_read;
    remaining -= more_bytes_read;
  }
  return true;
}


int BrotliCompress(BrotliParams params, BrotliIn* in, BrotliOut* out) {
  return BrotliCompressWithCustomDictionary(0, 0, params, in, out);
}

// Reads the provided input in 'block_size' blocks. Only the last read can be
// smaller than 'block_size'.
class BrotliBlockReader {
 public:
  explicit BrotliBlockReader(size_t block_size)
      : block_size_(block_size), buf_(NULL) {}
  ~BrotliBlockReader(void) { delete[] buf_; }

  const uint8_t* Read(BrotliIn* in, size_t* bytes_read, bool* is_last) {
    *bytes_read = 0;
    const uint8_t* data = BrotliInReadAndCheckEnd(block_size_, in,
                                                  bytes_read, is_last);
    if (data == NULL || *bytes_read == block_size_ || *is_last) {
      // If we could get the whole block in one read, or it is the last block,
      // we just return the pointer to the data without copying.
      return data;
    }
    // If the data comes in smaller chunks, we need to copy it into an internal
    // buffer until we get a whole block or reach the last chunk.
    if (buf_ == NULL) {
      buf_ = new uint8_t[block_size_];
    }
    memcpy(buf_, data, *bytes_read);
    do {
      size_t cur_bytes_read = 0;
      data = BrotliInReadAndCheckEnd(block_size_ - *bytes_read, in,
                                     &cur_bytes_read, is_last);
      if (data == NULL) {
        return *is_last ? buf_ : NULL;
      }
      memcpy(&buf_[*bytes_read], data, cur_bytes_read);
      *bytes_read += cur_bytes_read;
    } while (*bytes_read < block_size_ && !*is_last);
    return buf_;
  }

 private:
  const size_t block_size_;
  uint8_t* buf_;
};

int BrotliCompressWithCustomDictionary(size_t dictsize, const uint8_t* dict,
                                       BrotliParams params,
                                       BrotliIn* in, BrotliOut* out) {
  if (params.quality <= 1) {
    const int quality = std::max(0, params.quality);
    const int lgwin = std::min(kMaxWindowBits,
                               std::max(kMinWindowBits, params.lgwin));
    uint8_t* storage = NULL;
    int* table = NULL;
    uint32_t* command_buf = NULL;
    uint8_t* literal_buf = NULL;
    uint8_t cmd_depths[128];
    uint16_t cmd_bits[128];
    uint8_t cmd_code[512];
    size_t cmd_code_numbits;
    if (quality == 0) {
      InitCommandPrefixCodes(cmd_depths, cmd_bits, cmd_code, &cmd_code_numbits);
    }
    uint8_t last_byte;
    uint8_t last_byte_bits;
    EncodeWindowBits(lgwin, &last_byte, &last_byte_bits);
    BrotliBlockReader r(1u << lgwin);
    int ok = 1;
    bool is_last = false;
    while (ok && !is_last) {
      // Read next block of input.
      size_t bytes;
      const uint8_t* data = r.Read(in, &bytes, &is_last);
      if (data == NULL) {
        if (!is_last) {
          ok = 0;
          break;
        }
        assert(bytes == 0);
      }
      // Set up output storage.
      const size_t max_out_size = 2 * bytes + 500;
      if (storage == NULL) {
        storage = new uint8_t[max_out_size];
      }
      storage[0] = last_byte;
      size_t storage_ix = last_byte_bits;
      // Set up hash table.
      size_t htsize = HashTableSize(MaxHashTableSize(quality), bytes);
      if (table == NULL) {
        table = new int[htsize];
      }
      memset(table, 0, htsize * sizeof(table[0]));
      // Set up command and literal buffers for two pass mode.
      if (quality == 1 && command_buf == NULL) {
        size_t buf_size = std::min(bytes, kCompressFragmentTwoPassBlockSize);
        command_buf = new uint32_t[buf_size];
        literal_buf = new uint8_t[buf_size];
      }
      // Do the actual compression.
      if (quality == 0) {
        BrotliCompressFragmentFast(data, bytes, is_last, table, htsize,
                                   cmd_depths, cmd_bits,
                                   &cmd_code_numbits, cmd_code,
                                   &storage_ix, storage);
      } else {
        BrotliCompressFragmentTwoPass(data, bytes, is_last,
                                      command_buf, literal_buf,
                                      table, htsize,
                                      &storage_ix, storage);
      }
      // Save last bytes to stitch it together with the next output block.
      last_byte = storage[storage_ix >> 3];
      last_byte_bits = storage_ix & 7u;
      // Write output block.
      size_t out_bytes = storage_ix >> 3;
      if (out_bytes > 0 && !out->Write(storage, out_bytes)) {
        ok = 0;
        break;
      }
    }
    delete[] storage;
    delete[] table;
    delete[] command_buf;
    delete[] literal_buf;
    return ok;
  }

  size_t in_bytes = 0;
  size_t out_bytes = 0;
  uint8_t* output = NULL;
  bool final_block = false;
  BrotliCompressor compressor(params);
  if (dictsize != 0) compressor.BrotliSetCustomDictionary(dictsize, dict);
  while (!final_block) {
    if (!CopyOneBlockToRingBuffer(in, &compressor, &in_bytes, &final_block)) {
      return false;
    }
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
