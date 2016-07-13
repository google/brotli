/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Implementation of Brotli compressor. */

#include <brotli/encode.h>

#include <stdlib.h>  /* free, malloc */
#include <string.h>  /* memcpy, memset */

#include "./backward_references.h"
#include "./bit_cost.h"
#include "./brotli_bit_stream.h"
#include "./compress_fragment.h"
#include "./compress_fragment_two_pass.h"
#include "./context.h"
#include "./entropy_encode.h"
#include "./fast_log.h"
#include "./hash.h"
#include "./histogram.h"
#include "./memory.h"
#include "./metablock.h"
#include "./port.h"
#include "./prefix.h"
#include "./ringbuffer.h"
#include "./utf8_util.h"
#include "./write_bits.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

static const int kMinQualityForBlockSplit = 4;
static const int kMinQualityForContextModeling = 5;
static const int kMinQualityForOptimizeHistograms = 4;
/* For quality 2 there is no block splitting, so we buffer at most this much
   literals and commands. */
static const size_t kMaxNumDelayedSymbols = 0x2fff;

#define COPY_ARRAY(dst, src) memcpy(dst, src, sizeof(src));

typedef enum BrotliEncoderStreamState {
  /* Default state. */
  BROTLI_STREAM_PROCESSING = 0,
  /* Intermediate state; after next block is emitted, byte-padding should be
     performed before getting back to default state. */
  BROTLI_STREAM_FLUSH_REQUESTED = 1,
  /* Last metablock was produced; no more input is acceptable. */
  BROTLI_STREAM_FINISHED = 2
} BrotliEncoderStreamState;

typedef struct BrotliEncoderStateStruct {
  /* Encoding parameters */
  BrotliEncoderMode mode;
  int quality;
  int lgwin;
  int lgblock;

  MemoryManager memory_manager_;

  Hashers hashers_;
  int hash_type_;
  uint64_t input_pos_;
  RingBuffer ringbuffer_;
  size_t cmd_alloc_size_;
  Command* commands_;
  size_t num_commands_;
  size_t num_literals_;
  size_t last_insert_len_;
  uint64_t last_flush_pos_;
  uint64_t last_processed_pos_;
  int dist_cache_[4];
  int saved_dist_cache_[4];
  uint8_t last_byte_;
  uint8_t last_byte_bits_;
  uint8_t prev_byte_;
  uint8_t prev_byte2_;
  size_t storage_size_;
  uint8_t* storage_;
  /* Hash table for |quality| 0 mode. */
  int small_table_[1 << 10];  /* 4KiB */
  int* large_table_;          /* Allocated only when needed */
  size_t large_table_size_;
  /* Command and distance prefix codes (each 64 symbols, stored back-to-back)
     used for the next block in |quality| 0. The command prefix code is over a
     smaller alphabet with the following 64 symbols:
        0 - 15: insert length code 0, copy length code 0 - 15, same distance
       16 - 39: insert length code 0, copy length code 0 - 23
       40 - 63: insert length code 0 - 23, copy length code 0
     Note that symbols 16 and 40 represent the same code in the full alphabet,
     but we do not use either of them in |quality| 0. */
  uint8_t cmd_depths_[128];
  uint16_t cmd_bits_[128];
  /* The compressed form of the command and distance prefix codes for the next
     block in |quality| 0. */
  uint8_t cmd_code_[512];
  size_t cmd_code_numbits_;
  /* Command and literal buffers for quality 1. */
  uint32_t* command_buf_;
  uint8_t* literal_buf_;

  uint8_t* next_out_;
  size_t available_out_;
  size_t total_out_;
  uint8_t flush_buf_[2];
  BrotliEncoderStreamState stream_state_;

  int is_last_block_emitted_;
  int is_initialized_;
} BrotliEncoderStateStruct;

static int EnsureInitialized(BrotliEncoderState* s);

size_t BrotliEncoderInputBlockSize(BrotliEncoderState* s) {
  if (!EnsureInitialized(s)) return 0;
  return (size_t)1 << s->lgblock;
}

static uint64_t UnprocessedInputSize(BrotliEncoderState* s) {
  return s->input_pos_ - s->last_processed_pos_;
}

static size_t RemainingInputBlockSize(BrotliEncoderState* s) {
  const uint64_t delta = UnprocessedInputSize(s);
  size_t block_size = BrotliEncoderInputBlockSize(s);
  if (delta >= block_size) return 0;
  return block_size - (size_t)delta;
}

int BrotliEncoderSetParameter(
    BrotliEncoderState* state, BrotliEncoderParameter p, uint32_t value) {
  /* Changing parameters on the fly is not implemented yet. */
  if (state->is_initialized_) return 0;
  /* TODO: Validate/clamp params here. */
  switch (p) {
    case BROTLI_PARAM_MODE:
      state->mode = (BrotliEncoderMode)value;
      return 1;

    case BROTLI_PARAM_QUALITY:
      state->quality = (int)value;
      return 1;

    case BROTLI_PARAM_LGWIN:
      state->lgwin = (int)value;
      return 1;

    case BROTLI_PARAM_LGBLOCK:
      state->lgblock = (int)value;
      return 1;

    default: return 0;
  }
}

static void RecomputeDistancePrefixes(Command* cmds,
                                      size_t num_commands,
                                      uint32_t num_direct_distance_codes,
                                      uint32_t distance_postfix_bits) {
  size_t i;
  if (num_direct_distance_codes == 0 && distance_postfix_bits == 0) {
    return;
  }
  for (i = 0; i < num_commands; ++i) {
    Command* cmd = &cmds[i];
    if (CommandCopyLen(cmd) && cmd->cmd_prefix_ >= 128) {
      PrefixEncodeCopyDistance(CommandDistanceCode(cmd),
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
  uint32_t result = (uint32_t)position;
  if (position > (1u << 30)) {
    result = (result & ((1u << 30) - 1)) | (1u << 30);
  }
  return result;
}

static uint8_t* GetBrotliStorage(BrotliEncoderState* s, size_t size) {
  MemoryManager* m = &s->memory_manager_;
  if (s->storage_size_ < size) {
    BROTLI_FREE(m, s->storage_);
    s->storage_ = BROTLI_ALLOC(m, uint8_t, size);
    if (BROTLI_IS_OOM(m)) return NULL;
    s->storage_size_ = size;
  }
  return s->storage_;
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

static int* GetHashTable(BrotliEncoderState* s, int quality,
                         size_t input_size, size_t* table_size) {
  /* Use smaller hash table when input.size() is smaller, since we
     fill the table, incurring O(hash table size) overhead for
     compression, and if the input is short, we won't need that
     many hash table entries anyway. */
  MemoryManager* m = &s->memory_manager_;
  const size_t max_table_size = MaxHashTableSize(quality);
  size_t htsize = HashTableSize(max_table_size, input_size);
  int* table;
  assert(max_table_size >= 256);

  if (htsize <= sizeof(s->small_table_) / sizeof(s->small_table_[0])) {
    table = s->small_table_;
  } else {
    if (htsize > s->large_table_size_) {
      s->large_table_size_ = htsize;
      BROTLI_FREE(m, s->large_table_);
      s->large_table_ = BROTLI_ALLOC(m, int, htsize);
      if (BROTLI_IS_OOM(m)) return 0;
    }
    table = s->large_table_;
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
    *last_byte = (uint8_t)(((lgwin - 17) << 1) | 1);
    *last_byte_bits = 4;
  } else {
    *last_byte = (uint8_t)(((lgwin - 8) << 4) | 1);
    *last_byte_bits = 7;
  }
}

/* Initializes the command and distance prefix codes for the first block. */
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
  static const uint8_t kDefaultCommandCode[] = {
    0xff, 0x77, 0xd5, 0xbf, 0xe7, 0xde, 0xea, 0x9e, 0x51, 0x5d, 0xde, 0xc6,
    0x70, 0x57, 0xbc, 0x58, 0x58, 0x58, 0xd8, 0xd8, 0x58, 0xd5, 0xcb, 0x8c,
    0xea, 0xe0, 0xc3, 0x87, 0x1f, 0x83, 0xc1, 0x60, 0x1c, 0x67, 0xb2, 0xaa,
    0x06, 0x83, 0xc1, 0x60, 0x30, 0x18, 0xcc, 0xa1, 0xce, 0x88, 0x54, 0x94,
    0x46, 0xe1, 0xb0, 0xd0, 0x4e, 0xb2, 0xf7, 0x04, 0x00,
  };
  static const size_t kDefaultCommandCodeNumBits = 448;
  COPY_ARRAY(cmd_depths, kDefaultCommandDepths);
  COPY_ARRAY(cmd_bits, kDefaultCommandBits);

  /* Initialize the pre-compressed form of the command and distance prefix
     codes. */
  COPY_ARRAY(cmd_code, kDefaultCommandCode);
  *cmd_code_numbits = kDefaultCommandCodeNumBits;
}

/* Decide about the context map based on the ability of the prediction
   ability of the previous byte UTF8-prefix on the next byte. The
   prediction ability is calculated as shannon entropy. Here we need
   shannon entropy instead of 'BitsEntropy' since the prefix will be
   encoded with the remaining 6 bits of the following byte, and
   BitsEntropy will assume that symbol to be stored alone using Huffman
   coding. */
static void ChooseContextMap(int quality,
                             uint32_t* bigram_histo,
                             size_t* num_literal_contexts,
                             const uint32_t** literal_context_map) {
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

  uint32_t monogram_histo[3] = { 0 };
  uint32_t two_prefix_histo[6] = { 0 };
  size_t total = 0;
  size_t i;
  size_t dummy;
  double entropy[4];
  for (i = 0; i < 9; ++i) {
    size_t j = i;
    total += bigram_histo[i];
    monogram_histo[i % 3] += bigram_histo[i];
    if (j >= 6) {
      j -= 6;
    }
    two_prefix_histo[j] += bigram_histo[i];
  }
  entropy[1] = ShannonEntropy(monogram_histo, 3, &dummy);
  entropy[2] = (ShannonEntropy(two_prefix_histo, 3, &dummy) +
                ShannonEntropy(two_prefix_histo + 3, 3, &dummy));
  entropy[3] = 0;
  for (i = 0; i < 3; ++i) {
    entropy[3] += ShannonEntropy(bigram_histo + 3 * i, 3, &dummy);
  }

  assert(total != 0);
  entropy[0] = 1.0 / (double)total;
  entropy[1] *= entropy[0];
  entropy[2] *= entropy[0];
  entropy[3] *= entropy[0];

  if (quality < 7) {
    /* 3 context models is a bit slower, don't use it at lower qualities. */
    entropy[3] = entropy[1] * 10;
  }
  /* If expected savings by symbol are less than 0.2 bits, skip the
     context modeling -- in exchange for faster decoding speed. */
  if (entropy[1] - entropy[2] < 0.2 &&
      entropy[1] - entropy[3] < 0.2) {
    *num_literal_contexts = 1;
  } else if (entropy[2] - entropy[3] < 0.02) {
    *num_literal_contexts = 2;
    *literal_context_map = kStaticContextMapSimpleUTF8;
  } else {
    *num_literal_contexts = 3;
    *literal_context_map = kStaticContextMapContinuation;
  }
}

static void DecideOverLiteralContextModeling(const uint8_t* input,
    size_t start_pos, size_t length, size_t mask, int quality,
    ContextType* literal_context_mode, size_t* num_literal_contexts,
    const uint32_t** literal_context_map) {
  if (quality < kMinQualityForContextModeling || length < 64) {
    return;
  } else {
    /* Gather bigram data of the UTF8 byte prefixes. To make the analysis of
       UTF8 data faster we only examine 64 byte long strides at every 4kB
       intervals. */
    const size_t end_pos = start_pos + length;
    uint32_t bigram_prefix_histo[9] = { 0 };
    for (; start_pos + 64 <= end_pos; start_pos += 4096) {
      static const int lut[4] = { 0, 0, 1, 2 };
      const size_t stride_end_pos = start_pos + 64;
      int prev = lut[input[start_pos & mask] >> 6] * 3;
      size_t pos;
      for (pos = start_pos + 1; pos < stride_end_pos; ++pos) {
        const uint8_t literal = input[pos & mask];
        ++bigram_prefix_histo[prev + lut[literal >> 6]];
        prev = lut[literal >> 6] * 3;
      }
    }
    *literal_context_mode = CONTEXT_UTF8;
    ChooseContextMap(quality, &bigram_prefix_histo[0], num_literal_contexts,
                     literal_context_map);
  }
}

static int ShouldCompress(const uint8_t* data,
                          const size_t mask,
                          const uint64_t last_flush_pos,
                          const size_t bytes,
                          const size_t num_literals,
                          const size_t num_commands) {
  if (num_commands < (bytes >> 8) + 2) {
    if (num_literals > 0.99 * (double)bytes) {
      uint32_t literal_histo[256] = { 0 };
      static const uint32_t kSampleRate = 13;
      static const double kMinEntropy = 7.92;
      const double bit_cost_threshold =
          (double)bytes * kMinEntropy / kSampleRate;
      size_t t = (bytes + kSampleRate - 1) / kSampleRate;
      uint32_t pos = (uint32_t)last_flush_pos;
      size_t i;
      for (i = 0; i < t; i++) {
        ++literal_histo[data[pos & mask]];
        pos += kSampleRate;
      }
      if (BitsEntropy(literal_histo, 256) > bit_cost_threshold) {
        return 0;
      }
    }
  }
  return 1;
}

static void WriteMetaBlockInternal(MemoryManager* m,
                                   const uint8_t* data,
                                   const size_t mask,
                                   const uint64_t last_flush_pos,
                                   const size_t bytes,
                                   const int is_last,
                                   const int quality,
                                   const int is_font_mode,
                                   const uint8_t prev_byte,
                                   const uint8_t prev_byte2,
                                   const size_t num_literals,
                                   const size_t num_commands,
                                   Command* commands,
                                   const int* saved_dist_cache,
                                   int* dist_cache,
                                   size_t* storage_ix,
                                   uint8_t* storage) {
  uint8_t last_byte;
  uint8_t last_byte_bits;
  uint32_t num_direct_distance_codes = 0;
  uint32_t distance_postfix_bits = 0;

  if (bytes == 0) {
    /* Write the ISLAST and ISEMPTY bits. */
    BrotliWriteBits(2, 3, storage_ix, storage);
    *storage_ix = (*storage_ix + 7u) & ~7u;
    return;
  }

  if (!ShouldCompress(data, mask, last_flush_pos, bytes,
                      num_literals, num_commands)) {
    /* Restore the distance cache, as its last update by
       CreateBackwardReferences is now unused. */
    memcpy(dist_cache, saved_dist_cache, 4 * sizeof(dist_cache[0]));
    BrotliStoreUncompressedMetaBlock(is_last, data,
                                     WrapPosition(last_flush_pos), mask, bytes,
                                     storage_ix, storage);
    return;
  }

  last_byte = storage[0];
  last_byte_bits = (uint8_t)(*storage_ix & 0xff);
  if (quality > 9 && is_font_mode) {
    num_direct_distance_codes = 12;
    distance_postfix_bits = 1;
    RecomputeDistancePrefixes(commands,
                              num_commands,
                              num_direct_distance_codes,
                              distance_postfix_bits);
  }
  if (quality == 2) {
    BrotliStoreMetaBlockFast(m, data, WrapPosition(last_flush_pos),
                             bytes, mask, is_last,
                             commands, num_commands,
                             storage_ix, storage);
    if (BROTLI_IS_OOM(m)) return;
  } else if (quality < kMinQualityForBlockSplit) {
    BrotliStoreMetaBlockTrivial(m, data, WrapPosition(last_flush_pos),
                                bytes, mask, is_last,
                                commands, num_commands,
                                storage_ix, storage);
    if (BROTLI_IS_OOM(m)) return;
  } else {
    ContextType literal_context_mode = CONTEXT_UTF8;
    MetaBlockSplit mb;
    InitMetaBlockSplit(&mb);
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
        BrotliBuildMetaBlockGreedy(m, data, WrapPosition(last_flush_pos), mask,
                                   commands, num_commands, &mb);
        if (BROTLI_IS_OOM(m)) return;
      } else {
        BrotliBuildMetaBlockGreedyWithContexts(m, data,
                                               WrapPosition(last_flush_pos),
                                               mask,
                                               prev_byte, prev_byte2,
                                               literal_context_mode,
                                               num_literal_contexts,
                                               literal_context_map,
                                               commands, num_commands,
                                               &mb);
        if (BROTLI_IS_OOM(m)) return;
      }
    } else {
      if (!BrotliIsMostlyUTF8(data, WrapPosition(last_flush_pos), mask, bytes,
                              kMinUTF8Ratio)) {
        literal_context_mode = CONTEXT_SIGNED;
      }
      BrotliBuildMetaBlock(m, data, WrapPosition(last_flush_pos), mask, quality,
                           prev_byte, prev_byte2,
                           commands, num_commands,
                           literal_context_mode,
                           &mb);
      if (BROTLI_IS_OOM(m)) return;
    }
    if (quality >= kMinQualityForOptimizeHistograms) {
      BrotliOptimizeHistograms(num_direct_distance_codes,
                               distance_postfix_bits,
                               &mb);
    }
    BrotliStoreMetaBlock(m, data, WrapPosition(last_flush_pos), bytes, mask,
                         prev_byte, prev_byte2,
                         is_last,
                         num_direct_distance_codes,
                         distance_postfix_bits,
                         literal_context_mode,
                         commands, num_commands,
                         &mb,
                         storage_ix, storage);
    if (BROTLI_IS_OOM(m)) return;
    DestroyMetaBlockSplit(m, &mb);
  }
  if (bytes + 4 < (*storage_ix >> 3)) {
    /* Restore the distance cache and last byte. */
    memcpy(dist_cache, saved_dist_cache, 4 * sizeof(dist_cache[0]));
    storage[0] = last_byte;
    *storage_ix = last_byte_bits;
    BrotliStoreUncompressedMetaBlock(is_last, data,
                                     WrapPosition(last_flush_pos), mask,
                                     bytes, storage_ix, storage);
  }
}

static int EnsureInitialized(BrotliEncoderState* s) {
  if (BROTLI_IS_OOM(&s->memory_manager_)) return 0;
  if (s->is_initialized_) return 1;

  /* Sanitize params. */
  s->quality = BROTLI_MAX(int, 0, s->quality);
  if (s->lgwin < kBrotliMinWindowBits) {
    s->lgwin = kBrotliMinWindowBits;
  } else if (s->lgwin > kBrotliMaxWindowBits) {
    s->lgwin = kBrotliMaxWindowBits;
  }
  if (s->quality <= 1) {
    s->lgblock = s->lgwin;
  } else if (s->quality < kMinQualityForBlockSplit) {
    s->lgblock = 14;
  } else if (s->lgblock == 0) {
    s->lgblock = 16;
    if (s->quality >= 9 && s->lgwin > s->lgblock) {
      s->lgblock = BROTLI_MIN(int, 18, s->lgwin);
    }
  } else {
    s->lgblock = BROTLI_MIN(int, kBrotliMaxInputBlockBits,
        BROTLI_MAX(int, kBrotliMinInputBlockBits, s->lgblock));
  }

  /* Initialize input and literal cost ring buffers.
     We allocate at least lgwin + 1 bits for the ring buffer so that the newly
     added block fits there completely and we still get lgwin bits and at least
     read_block_size_bits + 1 bits because the copy tail length needs to be
     smaller than ringbuffer size. */
  {
    int ringbuffer_bits =
        BROTLI_MAX(int, s->lgwin + 1, s->lgblock + 1);
    RingBufferSetup(ringbuffer_bits, s->lgblock, &s->ringbuffer_);
  }

  /* Initialize last byte with stream header. */
  EncodeWindowBits(s->lgwin, &s->last_byte_, &s->last_byte_bits_);

  if (s->quality == 0) {
    InitCommandPrefixCodes(s->cmd_depths_, s->cmd_bits_,
                           s->cmd_code_, &s->cmd_code_numbits_);
  }

  /* Initialize hashers. */
  s->hash_type_ = BROTLI_MIN(int, 10, s->quality);
  HashersSetup(&s->memory_manager_, &s->hashers_, s->hash_type_);
  if (BROTLI_IS_OOM(&s->memory_manager_)) return 0;

  s->is_initialized_ = 1;
  return 1;
}

static void BrotliEncoderInitState(BrotliEncoderState* s) {
  s->mode = BROTLI_DEFAULT_MODE;
  s->quality = BROTLI_DEFAULT_QUALITY;
  s->lgwin = BROTLI_DEFAULT_WINDOW;
  s->lgblock = 0;

  s->input_pos_ = 0;
  s->num_commands_ = 0;
  s->num_literals_ = 0;
  s->last_insert_len_ = 0;
  s->last_flush_pos_ = 0;
  s->last_processed_pos_ = 0;
  s->prev_byte_ = 0;
  s->prev_byte2_ = 0;
  s->storage_size_ = 0;
  s->storage_ = 0;
  s->large_table_ = NULL;
  s->large_table_size_ = 0;
  s->cmd_code_numbits_ = 0;
  s->command_buf_ = NULL;
  s->literal_buf_ = NULL;
  s->next_out_ = NULL;
  s->available_out_ = 0;
  s->total_out_ = 0;
  s->stream_state_ = BROTLI_STREAM_PROCESSING;
  s->is_last_block_emitted_ = 0;
  s->is_initialized_ = 0;

  InitHashers(&s->hashers_);

  RingBufferInit(&s->ringbuffer_);

  s->commands_ = 0;
  s->cmd_alloc_size_ = 0;

  /* Initialize distance cache. */
  s->dist_cache_[0] = 4;
  s->dist_cache_[1] = 11;
  s->dist_cache_[2] = 15;
  s->dist_cache_[3] = 16;
  /* Save the state of the distance cache in case we need to restore it for
     emitting an uncompressed block. */
  memcpy(s->saved_dist_cache_, s->dist_cache_, sizeof(s->dist_cache_));
}

BrotliEncoderState* BrotliEncoderCreateInstance(brotli_alloc_func alloc_func,
                                                brotli_free_func free_func,
                                                void* opaque) {
  BrotliEncoderState* state = 0;
  if (!alloc_func && !free_func) {
    state = (BrotliEncoderState*)malloc(sizeof(BrotliEncoderState));
  } else if (alloc_func && free_func) {
    state = (BrotliEncoderState*)alloc_func(opaque, sizeof(BrotliEncoderState));
  }
  if (state == 0) {
    /* BROTLI_DUMP(); */
    return 0;
  }
  BrotliInitMemoryManager(
      &state->memory_manager_, alloc_func, free_func, opaque);
  BrotliEncoderInitState(state);
  return state;
}

static void BrotliEncoderCleanupState(BrotliEncoderState* s) {
  MemoryManager* m = &s->memory_manager_;
  if (BROTLI_IS_OOM(m)) {
    BrotliWipeOutMemoryManager(m);
    return;
  }
  BROTLI_FREE(m, s->storage_);
  BROTLI_FREE(m, s->commands_);
  RingBufferFree(m, &s->ringbuffer_);
  DestroyHashers(m, &s->hashers_);
  BROTLI_FREE(m, s->large_table_);
  BROTLI_FREE(m, s->command_buf_);
  BROTLI_FREE(m, s->literal_buf_);
}

/* Deinitializes and frees BrotliEncoderState instance. */
void BrotliEncoderDestroyInstance(BrotliEncoderState* state) {
  if (!state) {
    return;
  } else {
    MemoryManager* m = &state->memory_manager_;
    brotli_free_func free_func = m->free_func;
    void* opaque = m->opaque;
    BrotliEncoderCleanupState(state);
    free_func(opaque, state);
  }
}

void BrotliEncoderCopyInputToRingBuffer(BrotliEncoderState* s,
                                        const size_t input_size,
                                        const uint8_t* input_buffer) {
  RingBuffer* ringbuffer_ = &s->ringbuffer_;
  MemoryManager* m = &s->memory_manager_;
  if (!EnsureInitialized(s)) return;
  RingBufferWrite(m, input_buffer, input_size, ringbuffer_);
  if (BROTLI_IS_OOM(m)) return;
  s->input_pos_ += input_size;

  /* TL;DR: If needed, initialize 7 more bytes in the ring buffer to make the
     hashing not depend on uninitialized data. This makes compression
     deterministic and it prevents uninitialized memory warnings in Valgrind.
     Even without erasing, the output would be valid (but nondeterministic).

     Background information: The compressor stores short (at most 8 bytes)
     substrings of the input already read in a hash table, and detects
     repetitions by looking up such substrings in the hash table. If it
     can find a substring, it checks whether the substring is really there
     in the ring buffer (or it's just a hash collision). Should the hash
     table become corrupt, this check makes sure that the output is
     still valid, albeit the compression ratio would be bad.

     The compressor populates the hash table from the ring buffer as it's
     reading new bytes from the input. However, at the last few indexes of
     the ring buffer, there are not enough bytes to build full-length
     substrings from. Since the hash table always contains full-length
     substrings, we erase with dummy 0s here to make sure that those
     substrings will contain 0s at the end instead of uninitialized
     data.

     Please note that erasing is not necessary (because the
     memory region is already initialized since he ring buffer
     has a `tail' that holds a copy of the beginning,) so we
     skip erasing if we have already gone around at least once in
     the ring buffer.

     Only clear during the first round of ringbuffer writes. On
     subsequent rounds data in the ringbuffer would be affected. */
  if (ringbuffer_->pos_ <= ringbuffer_->mask_) {
    /* This is the first time when the ring buffer is being written.
       We clear 7 bytes just after the bytes that have been copied from
       the input buffer.

       The ringbuffer has a "tail" that holds a copy of the beginning,
       but only once the ring buffer has been fully written once, i.e.,
       pos <= mask. For the first time, we need to write values
       in this tail (where index may be larger than mask), so that
       we have exactly defined behavior and don't read un-initialized
       memory. Due to performance reasons, hashing reads data using a
       LOAD64, which can go 7 bytes beyond the bytes written in the
       ringbuffer. */
    memset(ringbuffer_->buffer_ + ringbuffer_->pos_, 0, 7);
  }
}

void BrotliEncoderSetCustomDictionary(BrotliEncoderState* s, size_t size,
                                      const uint8_t* dict) {
  size_t max_dict_size = MaxBackwardLimit(s->lgwin);
  size_t dict_size = size;
  MemoryManager* m = &s->memory_manager_;

  if (!EnsureInitialized(s)) return;

  if (dict_size == 0 || s->quality <= 1) {
    return;
  }
  if (size > max_dict_size) {
    dict += size - max_dict_size;
    dict_size = max_dict_size;
  }
  BrotliEncoderCopyInputToRingBuffer(s, dict_size, dict);
  s->last_flush_pos_ = dict_size;
  s->last_processed_pos_ = dict_size;
  if (dict_size > 0) {
    s->prev_byte_ = dict[dict_size - 1];
  }
  if (dict_size > 1) {
    s->prev_byte2_ = dict[dict_size - 2];
  }
  HashersPrependCustomDictionary(m, &s->hashers_,
      s->hash_type_, s->lgwin, dict_size, dict);
  if (BROTLI_IS_OOM(m)) return;
}

int BrotliEncoderWriteData(BrotliEncoderState* s, const int is_last,
                           const int force_flush, size_t* out_size,
                           uint8_t** output) {
  const uint64_t delta = UnprocessedInputSize(s);
  const uint32_t bytes = (uint32_t)delta;
  size_t max_length;
  uint8_t* data;
  uint32_t mask;
  MemoryManager* m = &s->memory_manager_;

  if (!EnsureInitialized(s)) return 0;
  data = s->ringbuffer_.buffer_;
  mask = s->ringbuffer_.mask_;

  /* Adding more blocks after "last" block is forbidden. */
  if (s->is_last_block_emitted_) return 0;
  if (is_last) s->is_last_block_emitted_ = 1;

  if (delta > BrotliEncoderInputBlockSize(s)) {
    return 0;
  }
  if (s->quality == 1 && !s->command_buf_) {
    s->command_buf_ =
        BROTLI_ALLOC(m, uint32_t, kCompressFragmentTwoPassBlockSize);
    s->literal_buf_ =
        BROTLI_ALLOC(m, uint8_t, kCompressFragmentTwoPassBlockSize);
    if (BROTLI_IS_OOM(m)) return 0;
  }

  if (s->quality <= 1) {
    uint8_t* storage;
    size_t storage_ix = s->last_byte_bits_;
    size_t table_size;
    int* table;

    if (delta == 0 && !is_last) {
      /* We have no new input data and we don't have to finish the stream, so
         nothing to do. */
      *out_size = 0;
      return 1;
    }
    storage = GetBrotliStorage(s, 2 * bytes + 500);
    if (BROTLI_IS_OOM(m)) return 0;
    storage[0] = s->last_byte_;
    table = GetHashTable(s, s->quality, bytes, &table_size);
    if (BROTLI_IS_OOM(m)) return 0;
    if (s->quality == 0) {
      BrotliCompressFragmentFast(
          m, &data[WrapPosition(s->last_processed_pos_) & mask],
          bytes, is_last,
          table, table_size,
          s->cmd_depths_, s->cmd_bits_,
          &s->cmd_code_numbits_, s->cmd_code_,
          &storage_ix, storage);
      if (BROTLI_IS_OOM(m)) return 0;
    } else {
      BrotliCompressFragmentTwoPass(
          m, &data[WrapPosition(s->last_processed_pos_) & mask],
          bytes, is_last,
          s->command_buf_, s->literal_buf_,
          table, table_size,
          &storage_ix, storage);
      if (BROTLI_IS_OOM(m)) return 0;
    }
    s->last_byte_ = storage[storage_ix >> 3];
    s->last_byte_bits_ = storage_ix & 7u;
    s->last_processed_pos_ = s->input_pos_;
    *output = &storage[0];
    *out_size = storage_ix >> 3;
    return 1;
  }

  {
    /* Theoretical max number of commands is 1 per 2 bytes. */
    size_t newsize = s->num_commands_ + bytes / 2 + 1;
    if (newsize > s->cmd_alloc_size_) {
      Command* new_commands;
      /* Reserve a bit more memory to allow merging with a next block
         without realloc: that would impact speed. */
      newsize += (bytes / 4) + 16;
      s->cmd_alloc_size_ = newsize;
      new_commands = BROTLI_ALLOC(m, Command, newsize);
      if (BROTLI_IS_OOM(m)) return 0;
      if (s->commands_) {
        memcpy(new_commands, s->commands_, sizeof(Command) * s->num_commands_);
        BROTLI_FREE(m, s->commands_);
      }
      s->commands_ = new_commands;
    }
  }

  BrotliCreateBackwardReferences(m, bytes, WrapPosition(s->last_processed_pos_),
                                 is_last, data, mask,
                                 s->quality,
                                 s->lgwin,
                                 &s->hashers_,
                                 s->hash_type_,
                                 s->dist_cache_,
                                 &s->last_insert_len_,
                                 &s->commands_[s->num_commands_],
                                 &s->num_commands_,
                                 &s->num_literals_);
  if (BROTLI_IS_OOM(m)) return 0;

  max_length =
      BROTLI_MIN(size_t, mask + 1, (size_t)1 << kBrotliMaxInputBlockBits);
  {
    const size_t max_literals = max_length / 8;
    const size_t max_commands = max_length / 8;
    const uint64_t input_limit = s->input_pos_ + BrotliEncoderInputBlockSize(s);
    if (!is_last && !force_flush &&
        (s->quality >= kMinQualityForBlockSplit ||
         (s->num_literals_ + s->num_commands_ < kMaxNumDelayedSymbols)) &&
        s->num_literals_ < max_literals &&
        s->num_commands_ < max_commands &&
        input_limit <= s->last_flush_pos_ + max_length) {
      /* Merge with next input block. Everything will happen later. */
      s->last_processed_pos_ = s->input_pos_;
      *out_size = 0;
      return 1;
    }
  }

  /* Create the last insert-only command. */
  if (s->last_insert_len_ > 0) {
    InitInsertCommand(&s->commands_[s->num_commands_++], s->last_insert_len_);
    s->num_literals_ += s->last_insert_len_;
    s->last_insert_len_ = 0;
  }

  if (!is_last && s->input_pos_ == s->last_flush_pos_) {
    /* We have no new input data and we don't have to finish the stream, so
       nothing to do. */
    *out_size = 0;
    return 1;
  }
  assert(s->input_pos_ >= s->last_flush_pos_);
  assert(s->input_pos_ > s->last_flush_pos_ || is_last);
  assert(s->input_pos_ - s->last_flush_pos_ <= 1u << 24);
  {
    const uint32_t metablock_size =
        (uint32_t)(s->input_pos_ - s->last_flush_pos_);
    uint8_t* storage = GetBrotliStorage(s, 2 * metablock_size + 500);
    size_t storage_ix = s->last_byte_bits_;
    int is_font_mode = (s->mode == BROTLI_MODE_FONT) ? 1 : 0;
    if (BROTLI_IS_OOM(m)) return 0;
    storage[0] = s->last_byte_;
    WriteMetaBlockInternal(
        m, data, mask, s->last_flush_pos_, metablock_size, is_last,
        s->quality, is_font_mode, s->prev_byte_, s->prev_byte2_,
        s->num_literals_, s->num_commands_, s->commands_, s->saved_dist_cache_,
        s->dist_cache_, &storage_ix, storage);
    if (BROTLI_IS_OOM(m)) return 0;
    s->last_byte_ = storage[storage_ix >> 3];
    s->last_byte_bits_ = storage_ix & 7u;
    s->last_flush_pos_ = s->input_pos_;
    s->last_processed_pos_ = s->input_pos_;
    if (s->last_flush_pos_ > 0) {
      s->prev_byte_ = data[((uint32_t)s->last_flush_pos_ - 1) & mask];
    }
    if (s->last_flush_pos_ > 1) {
      s->prev_byte2_ = data[(uint32_t)(s->last_flush_pos_ - 2) & mask];
    }
    s->num_commands_ = 0;
    s->num_literals_ = 0;
    /* Save the state of the distance cache in case we need to restore it for
       emitting an uncompressed block. */
    memcpy(s->saved_dist_cache_, s->dist_cache_, sizeof(s->dist_cache_));
    *output = &storage[0];
    *out_size = storage_ix >> 3;
    return 1;
  }
}

int BrotliEncoderWriteMetaBlock(BrotliEncoderState* s, const size_t input_size,
                                const uint8_t* input_buffer, const int is_last,
                                size_t* encoded_size, uint8_t* encoded_buffer) {
  size_t out_size = 0;
  uint8_t* output;
  int result;
  if (!EnsureInitialized(s)) return 0;
  BrotliEncoderCopyInputToRingBuffer(s, input_size, input_buffer);
  result = BrotliEncoderWriteData(
      s, is_last, /* force_flush */ 1, &out_size, &output);
  if (!result || out_size > *encoded_size) {
    return 0;
  }
  if (out_size > 0) {
    memcpy(encoded_buffer, output, out_size);
  }
  *encoded_size = out_size;
  return 1;
}

int BrotliEncoderWriteMetadata(BrotliEncoderState* s, const size_t input_size,
                               const uint8_t* input_buffer, const int is_last,
                               size_t* encoded_size, uint8_t* encoded_buffer) {
  uint64_t hdr_buffer_data[2];
  uint8_t* hdr_buffer = (uint8_t*)&hdr_buffer_data[0];
  size_t storage_ix;
  if (!EnsureInitialized(s)) return 0;
  if (input_size > (1 << 24) || input_size + 6 > *encoded_size) {
    return 0;
  }
  storage_ix = s->last_byte_bits_;
  hdr_buffer[0] = s->last_byte_;
  BrotliWriteBits(1, 0, &storage_ix, hdr_buffer);
  BrotliWriteBits(2, 3, &storage_ix, hdr_buffer);
  BrotliWriteBits(1, 0, &storage_ix, hdr_buffer);
  if (input_size == 0) {
    BrotliWriteBits(2, 0, &storage_ix, hdr_buffer);
    *encoded_size = (storage_ix + 7u) >> 3;
    memcpy(encoded_buffer, hdr_buffer, *encoded_size);
  } else {
    uint32_t nbits = (input_size == 1) ? 0 :
        (Log2FloorNonZero((uint32_t)input_size - 1) + 1);
    uint32_t nbytes = (nbits + 7) / 8;
    size_t hdr_size;
    BrotliWriteBits(2, nbytes, &storage_ix, hdr_buffer);
    BrotliWriteBits(8 * nbytes, input_size - 1, &storage_ix, hdr_buffer);
    hdr_size = (storage_ix + 7u) >> 3;
    memcpy(encoded_buffer, hdr_buffer, hdr_size);
    memcpy(&encoded_buffer[hdr_size], input_buffer, input_size);
    *encoded_size = hdr_size + input_size;
  }
  if (is_last) {
    encoded_buffer[(*encoded_size)++] = 3;
  }
  s->last_byte_ = 0;
  s->last_byte_bits_ = 0;
  return 1;
}

int BrotliEncoderFinishStream(BrotliEncoderState* s, size_t* encoded_size,
                              uint8_t* encoded_buffer) {
  if (!EnsureInitialized(s)) return 0;
  return BrotliEncoderWriteMetaBlock(
      s, 0, NULL, 1, encoded_size, encoded_buffer);
}

static int BrotliCompressBufferQuality10(int lgwin,
                                         size_t input_size,
                                         const uint8_t* input_buffer,
                                         size_t* encoded_size,
                                         uint8_t* encoded_buffer) {
  MemoryManager memory_manager;
  MemoryManager* m = &memory_manager;

  const size_t mask = BROTLI_SIZE_MAX >> 1;
  const size_t max_backward_limit = MaxBackwardLimit(lgwin);
  int dist_cache[4] = { 4, 11, 15, 16 };
  int saved_dist_cache[4] = { 4, 11, 15, 16 };
  int ok = 1;
  const size_t max_out_size = *encoded_size;
  size_t total_out_size = 0;
  uint8_t last_byte;
  uint8_t last_byte_bits;
  H10* hasher;

  const size_t hasher_eff_size =
      BROTLI_MIN(size_t, input_size, max_backward_limit + 16);

  const int quality = 10;
  const int lgblock = BROTLI_MIN(int, 18, lgwin);
  const int lgmetablock = BROTLI_MIN(int, 24, lgwin + 1);
  const size_t max_block_size = (size_t)1 << lgblock;
  const size_t max_metablock_size = (size_t)1 << lgmetablock;
  const size_t max_literals_per_metablock = max_metablock_size / 8;
  const size_t max_commands_per_metablock = max_metablock_size / 8;
  size_t metablock_start = 0;
  uint8_t prev_byte = 0;
  uint8_t prev_byte2 = 0;

  BrotliInitMemoryManager(m, 0, 0, 0);

  assert(input_size <= mask + 1);
  EncodeWindowBits(lgwin, &last_byte, &last_byte_bits);
  hasher = BROTLI_ALLOC(m, H10, 1);
  if (BROTLI_IS_OOM(m)) goto oom;
  InitializeH10(hasher);
  InitH10(m, hasher, input_buffer, lgwin, 0, hasher_eff_size, 1);
  if (BROTLI_IS_OOM(m)) goto oom;

  while (ok && metablock_start < input_size) {
    const size_t metablock_end =
        BROTLI_MIN(size_t, input_size, metablock_start + max_metablock_size);
    const size_t expected_num_commands =
        (metablock_end - metablock_start) / 12 + 16;
    Command* commands = 0;
    size_t num_commands = 0;
    size_t last_insert_len = 0;
    size_t num_literals = 0;
    size_t metablock_size = 0;
    size_t cmd_alloc_size = 0;
    int is_last;
    uint8_t* storage;
    size_t storage_ix;

    size_t block_start;
    for (block_start = metablock_start; block_start < metablock_end; ) {
      size_t block_size =
          BROTLI_MIN(size_t, metablock_end - block_start, max_block_size);
      ZopfliNode* nodes = BROTLI_ALLOC(m, ZopfliNode, block_size + 1);
      size_t path_size;
      size_t new_cmd_alloc_size;
      if (BROTLI_IS_OOM(m)) goto oom;
      BrotliInitZopfliNodes(nodes, block_size + 1);
      StitchToPreviousBlockH10(hasher, block_size, block_start,
                               input_buffer, mask);
      path_size = BrotliZopfliComputeShortestPath(
          m, block_size, block_start, input_buffer, mask, quality,
          max_backward_limit, dist_cache, hasher, nodes);
      if (BROTLI_IS_OOM(m)) goto oom;
      /* We allocate a command buffer in the first iteration of this loop that
         will be likely big enough for the whole metablock, so that for most
         inputs we will not have to reallocate in later iterations. We do the
         allocation here and not before the loop, because if the input is small,
         this will be allocated after the zopfli cost model is freed, so this
         will not increase peak memory usage.
         TODO: If the first allocation is too small, increase command
         buffer size exponentially. */
      new_cmd_alloc_size = BROTLI_MAX(size_t, expected_num_commands,
                                      num_commands + path_size + 1);
      if (cmd_alloc_size != new_cmd_alloc_size) {
        Command* new_commands = BROTLI_ALLOC(m, Command, new_cmd_alloc_size);
        if (BROTLI_IS_OOM(m)) goto oom;
        cmd_alloc_size = new_cmd_alloc_size;
        if (commands) {
          memcpy(new_commands, commands, sizeof(Command) * num_commands);
          BROTLI_FREE(m, commands);
        }
        commands = new_commands;
      }
      BrotliZopfliCreateCommands(block_size, block_start, max_backward_limit,
                                 &nodes[0], dist_cache, &last_insert_len,
                                 &commands[num_commands], &num_literals);
      num_commands += path_size;
      block_start += block_size;
      metablock_size += block_size;
      BROTLI_FREE(m, nodes);
      if (num_literals > max_literals_per_metablock ||
          num_commands > max_commands_per_metablock) {
        break;
      }
    }

    if (last_insert_len > 0) {
      InitInsertCommand(&commands[num_commands++], last_insert_len);
      num_literals += last_insert_len;
    }

    is_last = (metablock_start + metablock_size == input_size) ? 1 : 0;
    storage = NULL;
    storage_ix = last_byte_bits;

    if (metablock_size == 0) {
      /* Write the ISLAST and ISEMPTY bits. */
      storage = BROTLI_ALLOC(m, uint8_t, 16);
      if (BROTLI_IS_OOM(m)) goto oom;
      storage[0] = last_byte;
      BrotliWriteBits(2, 3, &storage_ix, storage);
      storage_ix = (storage_ix + 7u) & ~7u;
    } else if (!ShouldCompress(input_buffer, mask, metablock_start,
                               metablock_size, num_literals, num_commands)) {
      /* Restore the distance cache, as its last update by
         CreateBackwardReferences is now unused. */
      memcpy(dist_cache, saved_dist_cache, 4 * sizeof(dist_cache[0]));
      storage = BROTLI_ALLOC(m, uint8_t, metablock_size + 16);
      if (BROTLI_IS_OOM(m)) goto oom;
      storage[0] = last_byte;
      BrotliStoreUncompressedMetaBlock(is_last, input_buffer,
                                       metablock_start, mask, metablock_size,
                                       &storage_ix, storage);
    } else {
      uint32_t num_direct_distance_codes = 0;
      uint32_t distance_postfix_bits = 0;
      ContextType literal_context_mode = CONTEXT_UTF8;
      MetaBlockSplit mb;
      InitMetaBlockSplit(&mb);
      if (!BrotliIsMostlyUTF8(input_buffer, metablock_start, mask,
                              metablock_size, kMinUTF8Ratio)) {
        literal_context_mode = CONTEXT_SIGNED;
      }
      BrotliBuildMetaBlock(m, input_buffer, metablock_start, mask, quality,
                           prev_byte, prev_byte2,
                           commands, num_commands,
                           literal_context_mode,
                           &mb);
      if (BROTLI_IS_OOM(m)) goto oom;
      BrotliOptimizeHistograms(num_direct_distance_codes,
                               distance_postfix_bits,
                               &mb);
      storage = BROTLI_ALLOC(m, uint8_t, 2 * metablock_size + 500);
      if (BROTLI_IS_OOM(m)) goto oom;
      storage[0] = last_byte;
      BrotliStoreMetaBlock(m, input_buffer, metablock_start, metablock_size,
                           mask, prev_byte, prev_byte2,
                           is_last,
                           num_direct_distance_codes,
                           distance_postfix_bits,
                           literal_context_mode,
                           commands, num_commands,
                           &mb,
                           &storage_ix, storage);
      if (BROTLI_IS_OOM(m)) goto oom;
      if (metablock_size + 4 < (storage_ix >> 3)) {
        /* Restore the distance cache and last byte. */
        memcpy(dist_cache, saved_dist_cache, 4 * sizeof(dist_cache[0]));
        storage[0] = last_byte;
        storage_ix = last_byte_bits;
        BrotliStoreUncompressedMetaBlock(is_last, input_buffer,
                                         metablock_start, mask,
                                         metablock_size, &storage_ix, storage);
      }
      DestroyMetaBlockSplit(m, &mb);
    }
    last_byte = storage[storage_ix >> 3];
    last_byte_bits = storage_ix & 7u;
    metablock_start += metablock_size;
    prev_byte = input_buffer[metablock_start - 1];
    prev_byte2 = input_buffer[metablock_start - 2];
    /* Save the state of the distance cache in case we need to restore it for
       emitting an uncompressed block. */
    memcpy(saved_dist_cache, dist_cache, 4 * sizeof(dist_cache[0]));

    {
      const size_t out_size = storage_ix >> 3;
      total_out_size += out_size;
      if (total_out_size <= max_out_size) {
        memcpy(encoded_buffer, storage, out_size);
        encoded_buffer += out_size;
      } else {
        ok = 0;
      }
    }
    BROTLI_FREE(m, storage);
    BROTLI_FREE(m, commands);
  }

  *encoded_size = total_out_size;
  CleanupH10(m, hasher);
  BROTLI_FREE(m, hasher);
  return ok;

oom:
  BrotliWipeOutMemoryManager(m);
  return 0;
}

size_t BrotliEncoderMaxCompressedSize(size_t input_size) {
  /* [window bits / empty metadata] + N * [uncompressed] + [last empty] */
  size_t num_large_blocks = input_size >> 24;
  size_t tail = input_size - (num_large_blocks << 24);
  size_t tail_overhead = (tail > (1 << 20)) ? 4 : 3;
  size_t overhead = 2 + (4 * num_large_blocks) + tail_overhead + 1;
  size_t result = input_size + overhead;
  if (input_size == 0) return 1;
  return (result < input_size) ? 0 : result;
}

/* Wraps data to uncompressed brotli stream with minimal window size.
   |output| should point at region with at least BrotliEncoderMaxCompressedSize
   addressable bytes.
   Returns the length of stream. */
static size_t MakeUncompressedStream(
    const uint8_t* input, size_t input_size, uint8_t* output) {
  size_t size = input_size;
  size_t result = 0;
  size_t offset = 0;
  if (input_size == 0) {
    output[0] = 6;
    return 1;
  }
  output[result++] = 0x21;  /* window bits = 10, is_last = false */
  output[result++] = 0x03;  /* empty metadata, padding */
  while (size > 0) {
    uint32_t nibbles = 0;
    uint32_t chunk_size;
    uint32_t bits;
    chunk_size = (size > (1u << 24)) ? (1u << 24) : (uint32_t)size;
    if (chunk_size > (1u << 16)) nibbles = (chunk_size > (1u << 20)) ? 2 : 1;
    bits =
        (nibbles << 1) | ((chunk_size - 1) << 3) | (1u << (19 + 4 * nibbles));
    output[result++] = (uint8_t)bits;
    output[result++] = (uint8_t)(bits >> 8);
    output[result++] = (uint8_t)(bits >> 16);
    if (nibbles == 2) output[result++] = (uint8_t)(bits >> 24);
    memcpy(&output[result], &input[offset], chunk_size);
    result += chunk_size;
    offset += chunk_size;
    size -= chunk_size;
  }
  output[result++] = 3;
  return result;
}

int BrotliEncoderCompress(int quality, int lgwin, BrotliEncoderMode mode,
                          size_t input_size,
                          const uint8_t* input_buffer,
                          size_t* encoded_size,
                          uint8_t* encoded_buffer) {
  BrotliEncoderState* s;
  size_t out_size = *encoded_size;
  const uint8_t* input_start = input_buffer;
  uint8_t* output_start = encoded_buffer;
  size_t max_out_size = BrotliEncoderMaxCompressedSize(input_size);
  if (out_size == 0) {
    /* Output buffer needs at least one byte. */
    return 0;
  }
  if (input_size == 0) {
    /* Handle the special case of empty input. */
    *encoded_size = 1;
    *encoded_buffer = 6;
    return 1;
  }
  if (quality == 10) {
    /* TODO: Implement this direct path for all quality levels. */
    const int lg_win = BROTLI_MIN(int, 24, BROTLI_MAX(int, 16, lgwin));
    int ok = BrotliCompressBufferQuality10(lg_win, input_size, input_buffer,
                                           encoded_size, encoded_buffer);
    if (!ok || (max_out_size && *encoded_size > max_out_size)) {
      goto fallback;
    }
    return 1;
  }

  s = BrotliEncoderCreateInstance(0, 0, 0);
  if (!s) {
    return 0;
  } else {
    size_t available_in = input_size;
    const uint8_t* next_in = input_buffer;
    size_t available_out = *encoded_size;
    uint8_t* next_out = encoded_buffer;
    size_t total_out = 0;
    int result = 0;
    BrotliEncoderSetParameter(s, BROTLI_PARAM_QUALITY, (uint32_t)quality);
    BrotliEncoderSetParameter(s, BROTLI_PARAM_LGWIN, (uint32_t)lgwin);
    BrotliEncoderSetParameter(s, BROTLI_PARAM_MODE, (uint32_t)mode);
    result = BrotliEncoderCompressStream(s, BROTLI_OPERATION_FINISH,
        &available_in, &next_in, &available_out, &next_out, &total_out);
    if (!BrotliEncoderIsFinished(s)) result = 0;
    *encoded_size = total_out;
    BrotliEncoderDestroyInstance(s);
    if (!result || (max_out_size && *encoded_size > max_out_size)) {
      goto fallback;
    }
    return 1;
  }
fallback:
  *encoded_size = 0;
  if (!max_out_size) return 0;
  if (out_size >= max_out_size) {
    *encoded_size =
        MakeUncompressedStream(input_start, input_size, output_start);
    return 1;
  }
  return 0;
}

static void InjectBytePaddingBlock(BrotliEncoderState* s) {
  uint32_t seal = s->last_byte_;
  size_t seal_bits = s->last_byte_bits_;
  s->last_byte_ = 0;
  s->last_byte_bits_ = 0;
  /* is_last = 0, data_nibbles = 11, reseved = 0, meta_nibbles = 00 */
  seal |= 0x6u << seal_bits;
  seal_bits += 6;
  s->flush_buf_[0] = (uint8_t)seal;
  if (seal_bits > 8) s->flush_buf_[1] = (uint8_t)(seal >> 8);
  s->next_out_ = s->flush_buf_;
  s->available_out_ = (seal_bits + 7) >> 3;
}

static int BrotliEncoderCompressStreamFast(
    BrotliEncoderState* s, BrotliEncoderOperation op, size_t* available_in,
    const uint8_t** next_in, size_t* available_out, uint8_t** next_out,
    size_t* total_out) {
  const size_t block_size_limit = (size_t)1 << s->lgwin;
  const size_t buf_size = BROTLI_MIN(size_t, kCompressFragmentTwoPassBlockSize,
      BROTLI_MIN(size_t, *available_in, block_size_limit));
  uint32_t* tmp_command_buf = NULL;
  uint32_t* command_buf = NULL;
  uint8_t* tmp_literal_buf = NULL;
  uint8_t* literal_buf = NULL;
  MemoryManager* m = &s->memory_manager_;
  if (s->quality == 1) {
    if (!s->command_buf_ && buf_size == kCompressFragmentTwoPassBlockSize) {
      s->command_buf_ =
          BROTLI_ALLOC(m, uint32_t, kCompressFragmentTwoPassBlockSize);
      s->literal_buf_ =
          BROTLI_ALLOC(m, uint8_t, kCompressFragmentTwoPassBlockSize);
      if (BROTLI_IS_OOM(m)) return 0;
    }
    if (s->command_buf_) {
      command_buf = s->command_buf_;
      literal_buf = s->literal_buf_;
    } else {
      tmp_command_buf = BROTLI_ALLOC(m, uint32_t, buf_size);
      tmp_literal_buf = BROTLI_ALLOC(m, uint8_t, buf_size);
      if (BROTLI_IS_OOM(m)) return 0;
      command_buf = tmp_command_buf;
      literal_buf = tmp_literal_buf;
    }
  }

  while (1) {
    if (s->available_out_ == 0 &&
        s->stream_state_ == BROTLI_STREAM_FLUSH_REQUESTED) {
      s->stream_state_ = BROTLI_STREAM_PROCESSING;
      if (s->last_byte_bits_ == 0) break;
      InjectBytePaddingBlock(s);
      continue;
    }

    if (s->available_out_ != 0 && *available_out != 0) {
      size_t copy_output_size =
          BROTLI_MIN(size_t, s->available_out_, *available_out);
      memcpy(*next_out, s->next_out_, copy_output_size);
      *next_out += copy_output_size;
      *available_out -= copy_output_size;
      s->next_out_ += copy_output_size;
      s->available_out_ -= copy_output_size;
      s->total_out_ += copy_output_size;
      if (total_out) *total_out = s->total_out_;
      continue;
    }

    /* Compress block only when internal output buffer is empty, stream is not
       finished, there is no pending flush request, and there is either
       additional input or pending operation. */
    if (s->available_out_ == 0 &&
        s->stream_state_ == BROTLI_STREAM_PROCESSING &&
        (*available_in != 0 || op != BROTLI_OPERATION_PROCESS)) {
      size_t block_size = BROTLI_MIN(size_t, block_size_limit, *available_in);
      int is_last =
          (*available_in == block_size) && (op == BROTLI_OPERATION_FINISH);
      int force_flush =
          (*available_in == block_size) && (op == BROTLI_OPERATION_FLUSH);
      size_t max_out_size = 2 * block_size + 500;
      int inplace = 1;
      uint8_t* storage = NULL;
      size_t storage_ix = s->last_byte_bits_;
      size_t table_size;
      int* table;

      if (force_flush && block_size == 0) {
        s->stream_state_ = BROTLI_STREAM_FLUSH_REQUESTED;
        continue;
      }
      if (max_out_size <= *available_out) {
        storage = *next_out;
      } else {
        inplace = 0;
        storage = GetBrotliStorage(s, max_out_size);
        if (BROTLI_IS_OOM(m)) return 0;
      }
      storage[0] = s->last_byte_;
      table = GetHashTable(s, s->quality, block_size, &table_size);
      if (BROTLI_IS_OOM(m)) return 0;

      if (s->quality == 0) {
        BrotliCompressFragmentFast(m, *next_in, block_size, is_last, table,
            table_size, s->cmd_depths_, s->cmd_bits_, &s->cmd_code_numbits_,
            s->cmd_code_, &storage_ix, storage);
        if (BROTLI_IS_OOM(m)) return 0;
      } else {
        BrotliCompressFragmentTwoPass(m, *next_in, block_size, is_last,
            command_buf, literal_buf, table, table_size,
            &storage_ix, storage);
        if (BROTLI_IS_OOM(m)) return 0;
      }
      *next_in += block_size;
      *available_in -= block_size;
      if (inplace) {
        size_t out_bytes = storage_ix >> 3;
        assert(out_bytes <= *available_out);
        assert((storage_ix & 7) == 0 || out_bytes < *available_out);
        *next_out += out_bytes;
        *available_out -= out_bytes;
        s->total_out_ += out_bytes;
        if (total_out) *total_out = s->total_out_;
      } else {
        size_t out_bytes = storage_ix >> 3;
        s->next_out_ = storage;
        s->available_out_ = out_bytes;
      }
      s->last_byte_ = storage[storage_ix >> 3];
      s->last_byte_bits_ = storage_ix & 7u;

      if (force_flush) s->stream_state_ = BROTLI_STREAM_FLUSH_REQUESTED;
      if (is_last) s->stream_state_ = BROTLI_STREAM_FINISHED;
      continue;
    }
    break;
  }
  BROTLI_FREE(m, tmp_command_buf);
  BROTLI_FREE(m, tmp_literal_buf);
  return 1;
}

int BrotliEncoderCompressStream(BrotliEncoderState* s,
                                BrotliEncoderOperation op, size_t* available_in,
                                const uint8_t** next_in, size_t* available_out,
                                uint8_t** next_out, size_t* total_out) {
  if (!EnsureInitialized(s)) return 0;

  if (s->stream_state_ != BROTLI_STREAM_PROCESSING && *available_in != 0) {
    return 0;
  }
  if (s->quality <= 1) {
    return BrotliEncoderCompressStreamFast(s, op, available_in, next_in,
        available_out, next_out, total_out);
  }
  while (1) {
    size_t remaining_block_size = RemainingInputBlockSize(s);

    if (remaining_block_size != 0 && *available_in != 0) {
      size_t copy_input_size =
          BROTLI_MIN(size_t, remaining_block_size, *available_in);
      BrotliEncoderCopyInputToRingBuffer(s, copy_input_size, *next_in);
      *next_in += copy_input_size;
      *available_in -= copy_input_size;
      continue;
    }

    if (s->available_out_ == 0 &&
        s->stream_state_ == BROTLI_STREAM_FLUSH_REQUESTED) {
      s->stream_state_ = BROTLI_STREAM_PROCESSING;
      if (s->last_byte_bits_ == 0) break;
      InjectBytePaddingBlock(s);
      continue;
    }

    if (s->available_out_ != 0 && *available_out != 0) {
      size_t copy_output_size =
          BROTLI_MIN(size_t, s->available_out_, *available_out);
      memcpy(*next_out, s->next_out_, copy_output_size);
      *next_out += copy_output_size;
      *available_out -= copy_output_size;
      s->next_out_ += copy_output_size;
      s->available_out_ -= copy_output_size;
      s->total_out_ += copy_output_size;
      if (total_out) *total_out = s->total_out_;
      continue;
    }

    /* Compress data only when internal outpuf buffer is empty, stream is not
       finished and there is no pending flush request. */
    if (s->available_out_ == 0 &&
        s->stream_state_ == BROTLI_STREAM_PROCESSING) {
      if (remaining_block_size == 0 || op != BROTLI_OPERATION_PROCESS) {
        int is_last = (*available_in == 0) && op == BROTLI_OPERATION_FINISH;
        int force_flush = (*available_in == 0) && op == BROTLI_OPERATION_FLUSH;
        int result = BrotliEncoderWriteData(s, is_last, force_flush,
            &s->available_out_, &s->next_out_);
        if (!result) return 0;
        if (force_flush) s->stream_state_ = BROTLI_STREAM_FLUSH_REQUESTED;
        if (is_last) s->stream_state_ = BROTLI_STREAM_FINISHED;
        continue;
      }
    }
    break;
  }
  return 1;
}

int BrotliEncoderIsFinished(BrotliEncoderState* s) {
  return (s->stream_state_ == BROTLI_STREAM_FINISHED &&
      !BrotliEncoderHasMoreOutput(s)) ? 1 : 0;
}

int BrotliEncoderHasMoreOutput(BrotliEncoderState* s) {
  return (s->available_out_ != 0) ? 1 : 0;
}


#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif
