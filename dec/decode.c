/* Copyright 2013 Google Inc. All Rights Reserved.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include <malloc.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "./bit_reader.h"
#include "./context.h"
#include "./decode.h"
#include "./dictionary.h"
#include "./port.h"
#include "./transform.h"
#include "./huffman.h"
#include "./prefix.h"

#ifdef __ARM_NEON__
#include <arm_neon.h>
#endif

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#ifdef BROTLI_DECODE_DEBUG
#define BROTLI_LOG_UINT(name)                                    \
  printf("[%s] %s = %lu\n", __func__, #name, (unsigned long)(name))
#define BROTLI_LOG_ARRAY_INDEX(array_name, idx)                  \
  printf("[%s] %s[%lu] = %lu\n", __func__, #array_name, \
         (unsigned long)(idx), (unsigned long)array_name[idx])
#define BROTLI_LOG(x) printf x
#define BROTLI_LOG_UCHAR_VECTOR(v, len) PrintUcharVector(v, len);
static void PrintUcharVector(const uint8_t* v, int len) {
  while (len-- > 0) printf(" %d", *v++);
  printf("\n");
}
#else
#define BROTLI_LOG_UINT(name)
#define BROTLI_LOG_ARRAY_INDEX(array_name, idx)
#define BROTLI_LOG(x)
#define BROTLI_LOG_UCHAR_VECTOR(v, len)
#endif

static const uint8_t kDefaultCodeLength = 8;
static const uint8_t kCodeLengthRepeatCode = 16;
static const int kNumLiteralCodes = 256;
static const int kNumInsertAndCopyCodes = 704;
static const int kNumBlockLengthCodes = 26;
static const int kLiteralContextBits = 6;
static const int kDistanceContextBits = 2;

#define HUFFMAN_TABLE_BITS      8
#define HUFFMAN_TABLE_MASK      0xff

#define CODE_LENGTH_CODES 18
static const uint8_t kCodeLengthCodeOrder[CODE_LENGTH_CODES] = {
  1, 2, 3, 4, 0, 5, 17, 6, 16, 7, 8, 9, 10, 11, 12, 13, 14, 15,
};

#define NUM_DISTANCE_SHORT_CODES 16

static uint32_t DecodeWindowBits(BrotliBitReader* br) {
  uint32_t n;
  if (BrotliReadBits(br, 1) == 0) {
    return 16;
  }
  n = BrotliReadBits(br, 3);
  if (n != 0) {
    return 17 + n;
  }
  n = BrotliReadBits(br, 3);
  if (n != 0) {
    return 8 + n;
  }
  return 17;
}

static BROTLI_INLINE BROTLI_NO_ASAN void memmove16(
    uint8_t* dst, uint8_t* src) {
#ifdef __ARM_NEON__
  vst1q_u8(dst, vld1q_u8(src));
#else
  /* memcpy is unsafe for overlapping regions and ASAN detects this.
     But, because of optimizations, it works exactly as memmove:
     copies data to registers first, and then stores them to dst. */
  memcpy(dst, src, 16);
#endif
}

/* Decodes a number in the range [0..255], by reading 1 - 11 bits. */
static BROTLI_INLINE int DecodeVarLenUint8(BrotliBitReader* br) {
  if (BrotliReadBits(br, 1)) {
    int nbits = (int)BrotliReadBits(br, 3);
    if (nbits == 0) {
      return 1;
    } else {
      return (int)BrotliReadBits(br, nbits) + (1 << nbits);
    }
  }
  return 0;
}

static int DecodeMetaBlockLength(BrotliBitReader* br,
                                 int* meta_block_length,
                                 int* input_end,
                                 int* is_metadata,
                                 int* is_uncompressed) {
  int size_nibbles;
  int size_bytes;
  int i;
  *input_end = (int)BrotliReadBits(br, 1);
  *meta_block_length = 0;
  *is_uncompressed = 0;
  *is_metadata = 0;
  if (*input_end && BrotliReadBits(br, 1)) {
    return 1;
  }
  size_nibbles = (int)BrotliReadBits(br, 2) + 4;
  if (size_nibbles == 7) {
    *is_metadata = 1;
    /* Verify reserved bit. */
    if (BrotliReadBits(br, 1) != 0) {
      return 0;
    }
    size_bytes = (int)BrotliReadBits(br, 2);
    if (size_bytes == 0) {
      return 1;
    }
    for (i = 0; i < size_bytes; ++i) {
      int next_byte = (int)BrotliReadBits(br, 8);
      if (i + 1 == size_bytes && size_bytes > 1 && next_byte == 0) {
        return 0;
      }
      *meta_block_length |= next_byte << (i * 8);
    }
  } else {
    for (i = 0; i < size_nibbles; ++i) {
      int next_nibble = (int)BrotliReadBits(br, 4);
      if (i + 1 == size_nibbles && size_nibbles > 4 && next_nibble == 0) {
        return 0;
      }
      *meta_block_length |= next_nibble << (i * 4);
    }
  }
  ++(*meta_block_length);
  if (!*input_end && !*is_metadata) {
    *is_uncompressed = (int)BrotliReadBits(br, 1);
  }
  return 1;
}

/* Decodes the next Huffman code from bit-stream. */
static BROTLI_INLINE int ReadSymbol(const HuffmanCode* table,
                                    BrotliBitReader* br) {
  /* Read the bits for two reads at once. */
  uint32_t val = BrotliGetBitsUnmasked(br, 16);
  table += val & HUFFMAN_TABLE_MASK;
  if (PREDICT_FALSE(table->bits > HUFFMAN_TABLE_BITS)) {
    int nbits = table->bits - HUFFMAN_TABLE_BITS;
    BrotliDropBits(br, HUFFMAN_TABLE_BITS);
    table += table->value;
    table += (int)(val >> HUFFMAN_TABLE_BITS) & ((1 << nbits) - 1);
  }
  BrotliDropBits(br, table->bits);
  return table->value;
}

static BROTLI_INLINE void PreloadSymbol(const HuffmanCode* table,
                                        BrotliBitReader* br,
                                        unsigned* bits,
                                        unsigned* value) {
#if (BROTLI_PRELOAD_SYMBOLS)
  uint32_t val = BrotliGetBitsUnmasked(br, 16);
  table += val & HUFFMAN_TABLE_MASK;
  *bits = table->bits;
  *value = table->value;
#endif
}

static BROTLI_INLINE unsigned ReadPreloadedSymbol(const HuffmanCode* table,
                                                  BrotliBitReader* br,
                                                  unsigned* bits,
                                                  unsigned* value) {
#if (!BROTLI_PRELOAD_SYMBOLS)
  return ReadSymbol(table, br);
#else
  unsigned result = *value;
  if (PREDICT_FALSE(*bits > HUFFMAN_TABLE_BITS)) {
    uint32_t val = BrotliGetBitsUnmasked(br, 16);
    const HuffmanCode* ext = table + (val & HUFFMAN_TABLE_MASK) + *value;
    int mask = (1 << (*bits - HUFFMAN_TABLE_BITS)) - 1;
    BrotliDropBits(br, HUFFMAN_TABLE_BITS);
    ext += (int)(val >> HUFFMAN_TABLE_BITS) & mask;
    BrotliDropBits(br, ext->bits);
    result = ext->value;
  } else {
    br->bit_pos_ += *bits;
  }
  PreloadSymbol(table, br, bits, value);
  return result;
#endif
}

static BrotliResult ReadHuffmanCode(int alphabet_size,
                                    HuffmanCode* table,
                                    int* opt_table_size,
                                    BrotliState* s) {
  BrotliBitReader* br = &s->br;
  /* simple_code_or_skip is used as follows:
     1 for simple code;
     0 for no skipping, 2 skips 2 code lengths, 3 skips 3 code lengths */
  int simple_code_or_skip;

  /* Unnecessary masking, but might be good for safety. */
  alphabet_size &= 0x3ff;

  /* State machine */
  for (;;) {
    switch(s->sub1_state) {
      case BROTLI_STATE_SUB1_NONE:
        if (!BrotliCheckInputAmount(br, 32)) {
          return BROTLI_RESULT_NEEDS_MORE_INPUT;
        }
        simple_code_or_skip = (int)BrotliReadBits(br, 2);
        BROTLI_LOG_UINT(simple_code_or_skip);
        if (simple_code_or_skip == 1) {
          /* Read symbols, codes & code lengths directly. */
          int table_size;
          int i = 0;
          int max_bits_counter = alphabet_size - 1;
          int max_bits = 0;
          uint16_t symbols[4] = { 0 };
          uint32_t num_symbols = BrotliReadBits(br, 2);
          while (max_bits_counter) {
            max_bits_counter >>= 1;
            ++max_bits;
          }
          do {
            int k;
            uint32_t v = BrotliReadBits(br, max_bits);
            if (v >= alphabet_size) {
              return BROTLI_FAILURE();
            }
            symbols[i] = (uint16_t)v;
            for (k = 0; k < i; k++) {
              if (symbols[k] == (uint16_t)v) {
                return BROTLI_FAILURE();
              }
            }
          } while (++i <= num_symbols);
          if (num_symbols == 3) {
            num_symbols += BrotliReadBits(br, 1);
          }
          BROTLI_LOG_UINT(num_symbols);
          table_size = BrotliBuildSimpleHuffmanTable(
              table, HUFFMAN_TABLE_BITS, symbols, num_symbols);
          if (opt_table_size) {
            *opt_table_size = table_size;
          }
          s->sub1_state = BROTLI_STATE_SUB1_NONE;
          return BROTLI_RESULT_SUCCESS;
        } else {  /* Decode Huffman-coded code lengths. */
          int i;
          int8_t space = 32;
          int8_t num_codes = 0;
          /* Static Huffman code for the code length code lengths. */
          static const uint8_t huff_len[16] = {
            2, 2, 2, 3, 2, 2, 2, 4, 2, 2, 2, 3, 2, 2, 2, 4,
          };
          static const uint8_t huff_val[16] = {
            0, 4, 3, 2, 0, 4, 3, 1, 0, 4, 3, 2, 0, 4, 3, 5,
          };
          s->code_lengths = (uint8_t*)malloc((size_t)alphabet_size);
          if (s->code_lengths == NULL) {
            return BROTLI_FAILURE();
          }
          memset(&s->code_length_histo[0], 0, sizeof(s->code_length_histo));
          memset(&s->code_length_code_lengths[0], 0,
                 sizeof(s->code_length_code_lengths));
          s->huffman_max_nonzero = 0;
          for (i = simple_code_or_skip;
               i < CODE_LENGTH_CODES; ++i) {
            const uint8_t code_len_idx = kCodeLengthCodeOrder[i];
            uint8_t ix = (uint8_t)BrotliGetBits(br, 4);
            uint8_t v = huff_val[ix];
            BrotliDropBits(br, huff_len[ix]);
            s->code_length_code_lengths[code_len_idx] = v;
            BROTLI_LOG_ARRAY_INDEX(s->code_length_code_lengths, code_len_idx);
            if (v != 0) {
              space = (int8_t)(space - (32 >> v));
              ++num_codes;
              ++s->code_length_histo[v];
              if (s->huffman_max_nonzero < code_len_idx) {
                s->huffman_max_nonzero = code_len_idx;
              }
              if (space <= 0) {
                break;
              }
            }
          }
          if (!(num_codes == 1 || space == 0)) {
            return BROTLI_FAILURE();
          }
          s->sub1_state = BROTLI_STATE_SUB1_HUFFMAN_LENGTH_BEGIN;
        }
        /* No break, go to next state */
      case BROTLI_STATE_SUB1_HUFFMAN_LENGTH_BEGIN:
        s->symbol = 0;
        s->prev_code_len = kDefaultCodeLength;
        s->repeat = 0;
        s->repeat_code_len = 0;
        s->space = 32768;
        if (!BrotliBuildHuffmanTable(s->table, 5,
                                     s->code_length_code_lengths,
                                     (int)(s->huffman_max_nonzero + 1),
                                     s->code_length_histo)) {
          BROTLI_LOG((
              "[ReadHuffmanCodeLengths] Building code length tree failed: "));
          BROTLI_LOG_UCHAR_VECTOR(s->code_length_code_lengths,
                                  CODE_LENGTH_CODES);
          return BROTLI_FAILURE();
        }
        memset(&s->code_length_histo[0], 0, sizeof(s->code_length_histo));
        /* No break, continue to next state. */
      case BROTLI_STATE_SUB1_HUFFMAN_LENGTH_SYMBOLS:
        while (s->symbol < alphabet_size && s->space > 0) {
          const HuffmanCode* p = s->table;
          uint8_t code_len;
          if (!BrotliCheckInputAmount(br, 32)) {
            /* We might have come with 'No break' from previous switch case.
               Let's reset the state here before returning. */
            s->sub1_state = BROTLI_STATE_SUB1_HUFFMAN_LENGTH_SYMBOLS;
            return BROTLI_RESULT_NEEDS_MORE_INPUT;
          }
          p += BrotliGetBits(br, 5);
          BrotliDropBits(br, p->bits);
          code_len = (uint8_t)p->value;
          /* We predict that branch will be taken and write value now.
             Even if branch is mispredicted - it works as prefetch. */
          s->code_lengths[s->symbol] = code_len;
          if (code_len < kCodeLengthRepeatCode) {
            s->repeat = 0;
            if (code_len != 0) {
              s->prev_code_len = code_len;
              s->space -= 32768 >> code_len;
              s->huffman_max_nonzero = (uint32_t)s->symbol;
              s->code_length_histo[code_len]++;
            }
            s->symbol++;
          } else {
            const int extra_bits = code_len - 14;
            int old_repeat;
            int repeat_delta;
            uint8_t new_len = 0;
            if (code_len == kCodeLengthRepeatCode) {
              new_len = s->prev_code_len;
            }
            if (s->repeat_code_len != new_len) {
              s->repeat = 0;
              s->repeat_code_len = new_len;
            }
            old_repeat = s->repeat;
            if (s->repeat > 0) {
              s->repeat -= 2;
              s->repeat <<= extra_bits;
            }
            s->repeat += (int)BrotliReadBits(br, extra_bits) + 3;
            repeat_delta = s->repeat - old_repeat;
            if (s->symbol + repeat_delta > alphabet_size) {
              return BROTLI_FAILURE();
            }
            memset(&s->code_lengths[s->symbol], s->repeat_code_len,
                   (size_t)repeat_delta);
            s->symbol += repeat_delta;
            if (s->repeat_code_len != 0) {
              s->huffman_max_nonzero = (uint32_t)(s->symbol - 1);
              s->space -= repeat_delta << (15 - s->repeat_code_len);
              s->code_length_histo[s->repeat_code_len] = (uint16_t)
                  (s->code_length_histo[s->repeat_code_len] + repeat_delta);
            }
          }
        }
        if (s->space != 0) {
          BROTLI_LOG(("[ReadHuffmanCodeLengths] s->space = %d\n", s->space));
          return BROTLI_FAILURE();
        }
        s->sub1_state = BROTLI_STATE_SUB1_HUFFMAN_DONE;
        /* No break, go to next state */
      case BROTLI_STATE_SUB1_HUFFMAN_DONE:
        {
          int table_size = BrotliBuildHuffmanTable(
              table, HUFFMAN_TABLE_BITS, s->code_lengths,
              (int)(s->huffman_max_nonzero + 1),
              s->code_length_histo);
          if (table_size == 0) {
            BROTLI_LOG(("[ReadHuffmanCode] BuildHuffmanTable failed: "));
            BROTLI_LOG_UCHAR_VECTOR(s->code_lengths, alphabet_size);
            return BROTLI_FAILURE();
          }
          free(s->code_lengths);
          s->code_lengths = NULL;
          if (opt_table_size) {
            *opt_table_size = table_size;
          }
          s->sub1_state = BROTLI_STATE_SUB1_NONE;
        }
        return BROTLI_RESULT_SUCCESS;
    }
  }
}

static BROTLI_INLINE int ReadBlockLength(const HuffmanCode* table,
                                         BrotliBitReader* br) {
  int code;
  int nbits;
  code = ReadSymbol(table, br);
  nbits = kBlockLengthPrefixCode[code].nbits;
  return kBlockLengthPrefixCode[code].offset + (int)BrotliReadBits(br, nbits);
}

/* Transform:
    1) initialize list L with values 0, 1,... 255
    2) For each input element X:
    2.1) let Y = L[X]
    2.2) remove X-th element from L
    2.3) prepend Y to L
    2.4) append Y to output

   In most cases max(Y) <= 7, so most of L remains intact.
   To reduce the cost of initialization, we reuse L, remember the upper bound
   of Y values, and reinitialize only first elements in L.

   Most of input values are 0 and 1. To reduce number of brances, we replace
   inner for loop with do-while.
 */
static BROTLI_NOINLINE void InverseMoveToFrontTransform(uint8_t* v, int v_len,
    BrotliState* state) {
  /* Reinitialize elements that could have been changed. */
  int i = 4;
  int upper_bound = state->mtf_upper_bound;
  uint8_t* mtf = state->mtf;
  /* Load endian-aware constant. */
  const uint8_t b0123[4] = {0, 1, 2, 3};
  uint32_t pattern;
  memcpy(&pattern, &b0123, 4);

  /* Initialize list using 4 consequent values pattern. */
  *(uint32_t*)mtf = pattern;
  do {
    pattern += 0x04040404; /* Advance all 4 values by 4. */
    *(uint32_t*)(mtf + i) = pattern;
    i += 4;
  } while (i <= upper_bound);

  /* Transform the input. */
  upper_bound = 0;
  for (i = 0; i < v_len; ++i) {
    int index = v[i];
    uint8_t value = mtf[index];
    v[i] = value;
    upper_bound |= index;
    do {
      index--;
      mtf[index + 1] = mtf[index];
    } while (index > 0);
    mtf[0] = value;
  }

  /* Remember amount of elements to be reinitialized. */
  state->mtf_upper_bound = upper_bound;
}

/* Expose function for testing. Will be removed by linker as unused. */
void InverseMoveToFrontTransformForTesting(uint8_t* v, int l, BrotliState* s) {
  InverseMoveToFrontTransform(v, l, s);
}

static BrotliResult HuffmanTreeGroupDecode(HuffmanTreeGroup* group,
                                           BrotliState* s) {
  switch (s->sub0_state) {
    case BROTLI_STATE_SUB0_NONE:
      s->next = group->codes;
      s->htree_index = 0;
      s->sub0_state = BROTLI_STATE_SUB0_TREE_GROUP;
      /* No break, continue to next state. */
    case BROTLI_STATE_SUB0_TREE_GROUP:
      while (s->htree_index < group->num_htrees) {
        int table_size;
        BrotliResult result =
            ReadHuffmanCode(group->alphabet_size, s->next, &table_size, s);
        if (result != BROTLI_RESULT_SUCCESS) return result;
        group->htrees[s->htree_index] = s->next;
        s->next += table_size;
        if (table_size == 0) {
          return BROTLI_FAILURE();
        }
        ++s->htree_index;
      }
      s->sub0_state = BROTLI_STATE_SUB0_NONE;
      return BROTLI_RESULT_SUCCESS;
    default:
      return BROTLI_FAILURE();  /* unknown state */
  }

  return BROTLI_FAILURE();
}

static BrotliResult DecodeContextMap(int context_map_size,
                                     int* num_htrees,
                                     uint8_t** context_map,
                                     BrotliState* s) {
  BrotliBitReader* br = &s->br;
  BrotliResult result = BROTLI_RESULT_SUCCESS;
  int use_rle_for_zeros;

  switch((int)s->sub0_state) {
    case BROTLI_STATE_SUB0_NONE:
      if (!BrotliCheckInputAmount(br, 32)) {
        return BROTLI_RESULT_NEEDS_MORE_INPUT;
      }
      *num_htrees = DecodeVarLenUint8(br) + 1;

      s->context_index = 0;

      BROTLI_LOG_UINT(context_map_size);
      BROTLI_LOG_UINT(*num_htrees);

      *context_map = (uint8_t*)malloc((size_t)context_map_size);
      if (*context_map == 0) {
        return BROTLI_FAILURE();
      }
      if (*num_htrees <= 1) {
        memset(*context_map, 0, (size_t)context_map_size);
        return BROTLI_RESULT_SUCCESS;
      }

      use_rle_for_zeros = (int)BrotliReadBits(br, 1);
      if (use_rle_for_zeros) {
        s->max_run_length_prefix = (int)BrotliReadBits(br, 4) + 1;
      } else {
        s->max_run_length_prefix = 0;
      }
      s->context_map_table = (HuffmanCode*)malloc(
          BROTLI_HUFFMAN_MAX_TABLE_SIZE * sizeof(*s->context_map_table));
      if (s->context_map_table == NULL) {
        return BROTLI_FAILURE();
      }
      s->sub0_state = BROTLI_STATE_SUB0_CONTEXT_MAP_HUFFMAN;
      /* No break, continue to next state. */
    case BROTLI_STATE_SUB0_CONTEXT_MAP_HUFFMAN:
      result = ReadHuffmanCode(*num_htrees + s->max_run_length_prefix,
                               s->context_map_table, NULL, s);
      if (result != BROTLI_RESULT_SUCCESS) return result;
      s->sub0_state = BROTLI_STATE_SUB0_CONTEXT_MAPS;
      /* No break, continue to next state. */
    case BROTLI_STATE_SUB0_CONTEXT_MAPS: {
      int context_index = s->context_index;
      while (context_index < context_map_size) {
        int code;
        if (!BrotliCheckInputAmount(br, 32)) {
          s->context_index = context_index;
          return BROTLI_RESULT_NEEDS_MORE_INPUT;
        }
        code = ReadSymbol(s->context_map_table, br);
        if (code == 0) {
          (*context_map)[context_index] = 0;
          ++context_index;
        } else if (code <= s->max_run_length_prefix) {
          int reps = 1 + (1 << code) + (int)BrotliReadBits(br, code);
          while (--reps) {
            if (context_index >= context_map_size) {
              return BROTLI_FAILURE();
            }
            (*context_map)[context_index] = 0;
            ++context_index;
          }
        } else {
          (*context_map)[context_index] =
              (uint8_t)(code - s->max_run_length_prefix);
          ++context_index;
        }
      }
      if (BrotliReadBits(br, 1)) {
        InverseMoveToFrontTransform(*context_map, context_map_size, s);
      }
      free(s->context_map_table);
      s->context_map_table = NULL;
      s->sub0_state = BROTLI_STATE_SUB0_NONE;
      return BROTLI_RESULT_SUCCESS;
    }
  }

  return BROTLI_FAILURE();
}

static void DecodeBlockType(const int max_block_type,
                            const HuffmanCode* trees,
                            int tree_type,
                            int* ringbuffers,
                            BrotliBitReader* br) {
  int* ringbuffer = ringbuffers + tree_type * 2;
  int block_type =
      ReadSymbol(&trees[tree_type * BROTLI_HUFFMAN_MAX_TABLE_SIZE], br) - 2;
  if (block_type == -1) {
    block_type = ringbuffer[1] + 1;
  } else if (block_type == -2) {
    block_type = ringbuffer[0];
  }
  if (block_type >= max_block_type) {
    block_type -= max_block_type;
  }
  ringbuffer[0] = ringbuffer[1];
  ringbuffer[1] = block_type;
}

/* Decodes the block type and updates the state for literal context. */
static void DecodeBlockTypeWithContext(BrotliState* s,
                                       BrotliBitReader* br) {
  uint8_t context_mode;
  int context_offset;
  DecodeBlockType(s->num_block_types[0],
                  s->block_type_trees, 0,
                  s->block_type_rb, br);
  s->block_length[0] = ReadBlockLength(s->block_len_trees, br);
  context_offset = s->block_type_rb[1] << kLiteralContextBits;
  s->context_map_slice = s->context_map + context_offset;
  s->literal_htree_index = s->context_map_slice[0];
  context_mode = s->context_modes[s->block_type_rb[1]];
  s->context_lookup1 =
      &kContextLookup[kContextLookupOffsets[context_mode]];
  s->context_lookup2 =
      &kContextLookup[kContextLookupOffsets[context_mode + 1]];
}

BrotliResult WriteRingBuffer(BrotliOutput output,
                             BrotliState* s) {
  int num_written = BrotliWrite(
      output, s->ringbuffer + s->partially_written,
      (size_t)(s->to_write - s->partially_written));
  if (num_written < 0) {
    return BROTLI_FAILURE();
  }
  s->partially_written += num_written;
  if (s->partially_written < s->to_write) {
    return BROTLI_RESULT_NEEDS_MORE_OUTPUT;
  }
  return BROTLI_RESULT_SUCCESS;
}

BrotliResult BROTLI_NOINLINE CopyUncompressedBlockToOutput(BrotliOutput output,
                                                           int pos,
                                                           BrotliState* s) {
  BrotliResult result;
  int num_read;

  /* State machine */
  for (;;) {
    switch ((int)s->sub0_state) {
      case BROTLI_STATE_SUB0_NONE:
        /* For short lengths copy byte-by-byte */
        if (s->meta_block_remaining_len < 8 ||
            s->meta_block_remaining_len < BrotliGetRemainingBytes(&s->br)) {
          s->sub0_state = BROTLI_STATE_SUB0_UNCOMPRESSED_SHORT;
          break;
        }

        /* Copy remaining bytes from s->br.buf_ to ringbuffer. */
        s->nbytes = (int)BrotliGetRemainingBytes(&s->br);
        BrotliCopyBytes(&s->ringbuffer[pos], &s->br, (size_t)s->nbytes);
        pos += s->nbytes;
        s->meta_block_remaining_len -= s->nbytes;

        if (pos >= s->ringbuffer_size) {
          s->to_write = s->ringbuffer_size;
          s->partially_written = 0;
          s->sub0_state = BROTLI_STATE_SUB0_UNCOMPRESSED_WRITE_1;
          break;
        }
        if (pos + s->meta_block_remaining_len >= s->ringbuffer_size) {
          s->sub0_state = BROTLI_STATE_SUB0_UNCOMPRESSED_FILL;
        } else {
          s->sub0_state = BROTLI_STATE_SUB0_UNCOMPRESSED_COPY;
        }
        break;
      case BROTLI_STATE_SUB0_UNCOMPRESSED_SHORT:
        while (s->meta_block_remaining_len > 0) {
          if (!BrotliCheckInputAmount(&s->br, 32)) {
            return BROTLI_RESULT_NEEDS_MORE_INPUT;
          }
          s->ringbuffer[pos++] = (uint8_t)BrotliReadBits(&s->br, 8);
          s->meta_block_remaining_len--;
        }
        if (pos >= s->ringbuffer_size) {
          s->to_write = s->ringbuffer_size;
          s->partially_written = 0;
          s->sub0_state = BROTLI_STATE_SUB0_UNCOMPRESSED_WRITE_2;
        } else {
          s->sub0_state = BROTLI_STATE_SUB0_NONE;
          return BROTLI_RESULT_SUCCESS;
        }
        /* No break, if state is updated, continue to next state */
      case BROTLI_STATE_SUB0_UNCOMPRESSED_WRITE_1:
      case BROTLI_STATE_SUB0_UNCOMPRESSED_WRITE_2:
      case BROTLI_STATE_SUB0_UNCOMPRESSED_WRITE_3:
        result = WriteRingBuffer(output, s);
        if (result != BROTLI_RESULT_SUCCESS) {
          return result;
        }
        pos &= s->ringbuffer_size;
        s->max_distance = s->max_backward_distance;
        if (s->sub0_state == BROTLI_STATE_SUB0_UNCOMPRESSED_WRITE_2) {
          s->meta_block_remaining_len--;
          s->sub0_state = BROTLI_STATE_SUB0_UNCOMPRESSED_SHORT;
          break;
        }
        if (s->sub0_state == BROTLI_STATE_SUB0_UNCOMPRESSED_WRITE_1) {
          s->meta_block_remaining_len += s->ringbuffer_size;
          /* If we wrote past the logical end of the ringbuffer, copy the tail
             of the ringbuffer to its beginning and flush the ringbuffer to the
             output. */
          memcpy(s->ringbuffer, s->ringbuffer_end, (size_t)pos);
        }
        if (pos + s->meta_block_remaining_len >= s->ringbuffer_size) {
          s->sub0_state = BROTLI_STATE_SUB0_UNCOMPRESSED_FILL;
        } else {
          s->sub0_state = BROTLI_STATE_SUB0_UNCOMPRESSED_COPY;
          break;
        }
        /* No break, continue to next state */
      case BROTLI_STATE_SUB0_UNCOMPRESSED_FILL:
        /* If we have more to copy than the remaining size of the ringbuffer,
           then we first fill the ringbuffer from the input and then flush the
           ringbuffer to the output */
        s->nbytes = s->ringbuffer_size - pos;
        num_read = BrotliRead(s->br.input_, &s->ringbuffer[pos],
                              (size_t)s->nbytes);
        s->meta_block_remaining_len -= num_read;
        if (num_read < s->nbytes) {
          if (num_read < 0) return BROTLI_FAILURE();
          return BROTLI_RESULT_NEEDS_MORE_INPUT;
        }
        s->to_write = s->ringbuffer_size;
        s->partially_written = 0;
        s->sub0_state = BROTLI_STATE_SUB0_UNCOMPRESSED_WRITE_3;
        break;
        /* No break, continue to next state */
      case BROTLI_STATE_SUB0_UNCOMPRESSED_COPY:
        /* Copy straight from the input onto the ringbuffer. The ringbuffer will
           be flushed to the output at a later time. */
        num_read = BrotliRead(s->br.input_, &s->ringbuffer[pos],
                              (size_t)s->meta_block_remaining_len);
        s->meta_block_remaining_len -= num_read;
        if (s->meta_block_remaining_len > 0) {
          if (num_read < 0) return BROTLI_FAILURE();
          return BROTLI_RESULT_NEEDS_MORE_INPUT;
        }
        s->sub0_state = BROTLI_STATE_SUB0_UNCOMPRESSED_WARMUP;
        /* No break, continue to next state */
      case BROTLI_STATE_SUB0_UNCOMPRESSED_WARMUP:
        if (!BrotliCheckInputAmount(&s->br, 32)) {
          return BROTLI_RESULT_NEEDS_MORE_INPUT;
        }
        BrotliWarmupBitReader(&s->br);
        s->sub0_state = BROTLI_STATE_SUB0_NONE;
        return BROTLI_RESULT_SUCCESS;
    }
  }
  return BROTLI_FAILURE();
}

int BrotliDecompressedSize(size_t encoded_size,
                           const uint8_t* encoded_buffer,
                           size_t* decoded_size) {
  int i;
  uint64_t val = 0;
  int bit_pos = 0;
  int is_last;
  int is_uncompressed = 0;
  int size_nibbles;
  int meta_block_len = 0;
  if (encoded_size == 0) {
    return 0;
  }
  /* Look at the first 8 bytes, it is enough to decode the length of the first
     meta-block. */
  for (i = 0; (size_t)i < encoded_size && i < 8; ++i) {
    val |= (uint64_t)encoded_buffer[i] << (8 * i);
  }
  /* Skip the window bits. */
  ++bit_pos;
  if (val & 1) {
    bit_pos += 3;
    if (((val >> 1) & 7) == 0) {
      bit_pos += 3;
    }
  }
  /* Decode the ISLAST bit. */
  is_last = (val >> bit_pos) & 1;
  ++bit_pos;
  if (is_last) {
    /* Decode the ISEMPTY bit, if it is set to 1, we are done. */
    if ((val >> bit_pos) & 1) {
      *decoded_size = 0;
      return 1;
    }
    ++bit_pos;
  }
  /* Decode the length of the first meta-block. */
  size_nibbles = (int)((val >> bit_pos) & 3) + 4;
  if (size_nibbles == 7) {
    /* First meta-block contains metadata, this case is not supported here. */
    return 0;
  }
  bit_pos += 2;
  for (i = 0; i < size_nibbles; ++i) {
    meta_block_len |= (int)((val >> bit_pos) & 0xf) << (4 * i);
    bit_pos += 4;
  }
  ++meta_block_len;
  if (is_last) {
    /* If this meta-block is the only one, we are done. */
    *decoded_size = (size_t)meta_block_len;
    return 1;
  }
  is_uncompressed = (val >> bit_pos) & 1;
  ++bit_pos;
  if (is_uncompressed) {
    /* If the first meta-block is uncompressed, we skip it and look at the
       first two bits (ISLAST and ISEMPTY) of the next meta-block, and if
       both are set to 1, we have a stream with an uncompressed meta-block
       followed by an empty one, so the decompressed size is the size of the
       first meta-block. */
    size_t offset = (size_t)((bit_pos + 7) >> 3) + (size_t)meta_block_len;
    if (offset < encoded_size && ((encoded_buffer[offset] & 3) == 3)) {
      *decoded_size = (size_t)meta_block_len;
      return 1;
    }
  }
  /* Could not get the size because the file has multiple meta-blocks */
  return 0;
}

BrotliResult BrotliDecompressBuffer(size_t encoded_size,
                                    const uint8_t* encoded_buffer,
                                    size_t* decoded_size,
                                    uint8_t* decoded_buffer) {
  BrotliMemInput memin;
  BrotliInput in = BrotliInitMemInput(encoded_buffer, encoded_size, &memin);
  BrotliMemOutput mout;
  BrotliOutput out = BrotliInitMemOutput(decoded_buffer, *decoded_size, &mout);
  BrotliResult success = BrotliDecompress(in, out);
  *decoded_size = mout.pos;
  return success;
}

BrotliResult BrotliDecompress(BrotliInput input, BrotliOutput output) {
  BrotliState s;
  BrotliResult result;
  BrotliStateInit(&s);
  result = BrotliDecompressStreaming(input, output, 1, &s);
  if (result == BROTLI_RESULT_NEEDS_MORE_INPUT) {
    /* Not ok: it didn't finish even though this is a non-streaming function. */
    result = BROTLI_FAILURE();
  }
  BrotliStateCleanup(&s);
  return result;
}

BrotliResult BrotliDecompressBufferStreaming(size_t* available_in,
                                             const uint8_t** next_in,
                                             int finish,
                                             size_t* available_out,
                                             uint8_t** next_out,
                                             size_t* total_out,
                                             BrotliState* s) {
  BrotliResult result;
  BrotliMemInput memin;
  BrotliInput in = BrotliInitMemInput(*next_in, *available_in, &memin);
  BrotliMemOutput memout;
  BrotliOutput out = BrotliInitMemOutput(*next_out, *available_out, &memout);

  result = BrotliDecompressStreaming(in, out, finish, s);

  /* The current implementation reads everything, so 0 bytes are available. */
  *next_in += memin.pos;
  *available_in -= memin.pos;

  /* Update the output position to where we write next. */
  *next_out += memout.pos;
  *available_out -= memout.pos;
  *total_out += memout.pos;

  return result;
}

BrotliResult BrotliDecompressStreaming(BrotliInput input, BrotliOutput output,
                                       int finish, BrotliState* s) {
  uint8_t context;
  int pos = s->pos;
  int i = s->loop_counter;
  BrotliResult result = BROTLI_RESULT_SUCCESS;
  BrotliBitReader* br = &s->br;
  int initial_remaining_len;
  int bytes_copied;
  uint8_t *copy_src;
  uint8_t *copy_dst;

  /* We need the slack region for the following reasons:
       - always doing two 8-byte copies for fast backward copying
       - transforms
       - flushing the input s->ringbuffer when decoding uncompressed blocks */
  static const int kRingBufferWriteAheadSlack = 128 + BROTLI_READ_SIZE;

  s->br.input_ = input;

  /* State machine */
  for (;;) {
    if (result != BROTLI_RESULT_SUCCESS) {
      if (result == BROTLI_RESULT_NEEDS_MORE_INPUT) {
        if (BrotliReadInput(br, finish)) {
          result = BROTLI_RESULT_SUCCESS;
          continue;
        }
        if (finish) {
          BROTLI_LOG(("Unexpected end of input. State: %d\n", s->state));
          result = BROTLI_FAILURE();
        }
      }
      break;  /* Fail, or partial data. */
    }

    switch (s->state) {
      case BROTLI_STATE_UNINITED:
        pos = 0;
        BrotliInitBitReader(br, input);

        s->state = BROTLI_STATE_BITREADER_WARMUP;
        /* No break, continue to next state */
      case BROTLI_STATE_BITREADER_WARMUP:
        if (!BrotliCheckInputAmount(&s->br, 32)) {
          result = BROTLI_RESULT_NEEDS_MORE_INPUT;
          break;
        }
        BrotliWarmupBitReader(&s->br);
        /* Decode window size. */
        s->window_bits = DecodeWindowBits(br);
        if (s->window_bits == 9) {
          /* Value 9 is reserved for future use. */
          result = BROTLI_FAILURE();
          break;
        }
        s->max_backward_distance = (1 << s->window_bits) - 16;

        /* Allocate memory for both block_type_trees and block_len_trees. */
        s->block_type_trees = (HuffmanCode*)malloc(
            6 * BROTLI_HUFFMAN_MAX_TABLE_SIZE * sizeof(HuffmanCode));

        if (s->block_type_trees == NULL) {
          result = BROTLI_FAILURE();
          break;
        }
        s->block_len_trees = s->block_type_trees +
            3 * BROTLI_HUFFMAN_MAX_TABLE_SIZE;

        s->state = BROTLI_STATE_METABLOCK_BEGIN;
        /* No break, continue to next state */
      case BROTLI_STATE_METABLOCK_BEGIN:
        if (s->input_end) {
          s->to_write = pos;
          s->partially_written = 0;
          s->state = BROTLI_STATE_DONE;
          break;
        }
        BrotliStateMetablockBegin(s);
        s->state = BROTLI_STATE_METABLOCK_HEADER_1;
        /* No break, continue to next state */
      case BROTLI_STATE_METABLOCK_HEADER_1:
        if (!BrotliCheckInputAmount(br, 32)) {
          result = BROTLI_RESULT_NEEDS_MORE_INPUT;
          break;
        }
        BROTLI_LOG_UINT(pos);
        if (!DecodeMetaBlockLength(br,
                                   &s->meta_block_remaining_len,
                                   &s->input_end,
                                   &s->is_metadata,
                                   &s->is_uncompressed)) {
          result = BROTLI_FAILURE();
          break;
        }
        BROTLI_LOG_UINT(s->meta_block_remaining_len);
        /* If it is the first metablock, allocate the ringbuffer */
        if (s->ringbuffer == NULL) {
          size_t known_size = 0;
          s->ringbuffer_size = 1 << s->window_bits;

          /* If we know the data size is small, do not allocate more ringbuffer
             size than needed to reduce memory usage. Since this happens after
             the first BrotliCheckInputAmount call, we can read the bitreader
             buffer at position 0.
             We need at least 2 bytes of ring buffer size to get the last two
             bytes for context from there */
          if (BrotliDecompressedSize(BROTLI_READ_SIZE, br->buf_, &known_size)) {
            while (s->ringbuffer_size >= known_size * 2
                && s->ringbuffer_size > 32) {
              s->ringbuffer_size /= 2;
            }
          }

          /* But make it fit the custom dictionary if there is one. */
          while (s->ringbuffer_size < s->custom_dict_size) {
            s->ringbuffer_size *= 2;
          }

          s->ringbuffer_mask = s->ringbuffer_size - 1;
          s->ringbuffer = (uint8_t*)malloc((size_t)(s->ringbuffer_size +
                                                 kRingBufferWriteAheadSlack +
                                                 kMaxDictionaryWordLength));
          if (!s->ringbuffer) {
            result = BROTLI_FAILURE();
            break;
          }
          s->ringbuffer_end = s->ringbuffer + s->ringbuffer_size;
          s->ringbuffer[s->ringbuffer_size - 2] = 0;
          s->ringbuffer[s->ringbuffer_size - 1] = 0;

          if (s->custom_dict) {
            memcpy(&s->ringbuffer[(-s->custom_dict_size) & s->ringbuffer_mask],
                                  s->custom_dict, (size_t)s->custom_dict_size);
          }
        }

        if (s->is_metadata) {
          if (!BrotliJumpToByteBoundary(&s->br)) {
            result = BROTLI_FAILURE();
            break;
          }
          s->state = BROTLI_STATE_METADATA;
          break;
        }
        if (s->meta_block_remaining_len == 0) {
          s->state = BROTLI_STATE_METABLOCK_DONE;
          break;
        }
        if (s->is_uncompressed) {
          if (!BrotliJumpToByteBoundary(&s->br)) {
            result = BROTLI_FAILURE();
            break;
          }
          s->state = BROTLI_STATE_UNCOMPRESSED;
          break;
        }
        i = 0;
        s->state = BROTLI_STATE_HUFFMAN_CODE_0;
        break;
      case BROTLI_STATE_UNCOMPRESSED:
        initial_remaining_len = s->meta_block_remaining_len;
        /* pos is given as argument since s->pos is only updated at the end. */
        result = CopyUncompressedBlockToOutput(output, pos, s);
        bytes_copied = initial_remaining_len - s->meta_block_remaining_len;
        pos = (pos + bytes_copied) & s->ringbuffer_mask;
        if (result != BROTLI_RESULT_SUCCESS) {
          break;
        }
        s->state = BROTLI_STATE_METABLOCK_DONE;
        break;
      case BROTLI_STATE_METADATA:
        for (; s->meta_block_remaining_len > 0; --s->meta_block_remaining_len) {
          if (!BrotliCheckInputAmount(&s->br, 32)) {
            result = BROTLI_RESULT_NEEDS_MORE_INPUT;
            break;
          }
          /* Read one byte and ignore it. */
          BrotliReadBits(&s->br, 8);
        }
        if (result == BROTLI_RESULT_SUCCESS) {
          s->state = BROTLI_STATE_METABLOCK_DONE;
        }
        break;
      case BROTLI_STATE_HUFFMAN_CODE_0:
        if (i >= 3) {
          BROTLI_LOG_UINT(s->num_block_type_rb[0]);
          BROTLI_LOG_UINT(s->num_block_type_rb[2]);
          BROTLI_LOG_UINT(s->num_block_type_rb[4]);
          BROTLI_LOG_UINT(s->block_length[0]);
          BROTLI_LOG_UINT(s->block_length[1]);
          BROTLI_LOG_UINT(s->block_length[2]);

          s->state = BROTLI_STATE_METABLOCK_HEADER_2;
          break;
        }
        s->num_block_types[i] = DecodeVarLenUint8(br) + 1;
        s->state = BROTLI_STATE_HUFFMAN_CODE_1;
        /* No break, continue to next state */
      case BROTLI_STATE_HUFFMAN_CODE_1:
        if (s->num_block_types[i] >= 2) {
          result = ReadHuffmanCode(s->num_block_types[i] + 2,
              &s->block_type_trees[i * BROTLI_HUFFMAN_MAX_TABLE_SIZE],
              NULL, s);
          if (result != BROTLI_RESULT_SUCCESS) break;
          s->state = BROTLI_STATE_HUFFMAN_CODE_2;
        } else {
          i++;
          s->state = BROTLI_STATE_HUFFMAN_CODE_0;
          break;
        }
        /* No break, continue to next state */
      case BROTLI_STATE_HUFFMAN_CODE_2:
        result = ReadHuffmanCode(kNumBlockLengthCodes,
            &s->block_len_trees[i * BROTLI_HUFFMAN_MAX_TABLE_SIZE],
            NULL, s);
        if (result != BROTLI_RESULT_SUCCESS) break;
        s->block_length[i] = ReadBlockLength(
            &s->block_len_trees[i * BROTLI_HUFFMAN_MAX_TABLE_SIZE], br);
        i++;
        s->state = BROTLI_STATE_HUFFMAN_CODE_0;
        break;
      case BROTLI_STATE_METABLOCK_HEADER_2:
        /* We need up to 256 * 2 + 6 bits, this fits in 128 bytes. */
        if (!BrotliCheckInputAmount(br, 128)) {
          result = BROTLI_RESULT_NEEDS_MORE_INPUT;
          break;
        }
        s->distance_postfix_bits = (int)BrotliReadBits(br, 2);
        s->num_direct_distance_codes = NUM_DISTANCE_SHORT_CODES +
            ((int)BrotliReadBits(br, 4) << s->distance_postfix_bits);
        s->distance_postfix_mask = (1 << s->distance_postfix_bits) - 1;
        s->context_modes = (uint8_t*)malloc((size_t)s->num_block_types[0]);
        if (s->context_modes == 0) {
          result = BROTLI_FAILURE();
          break;
        }
        for (i = 0; i < s->num_block_types[0]; ++i) {
          s->context_modes[i] = (uint8_t)(BrotliReadBits(br, 2) << 1);
          BROTLI_LOG_ARRAY_INDEX(s->context_modes, i);
        }
        BROTLI_LOG_UINT(s->num_direct_distance_codes);
        BROTLI_LOG_UINT(s->distance_postfix_bits);
        s->state = BROTLI_STATE_CONTEXT_MAP_1;
        /* No break, continue to next state */
      case BROTLI_STATE_CONTEXT_MAP_1:
        result = DecodeContextMap(s->num_block_types[0] << kLiteralContextBits,
                                  &s->num_literal_htrees, &s->context_map, s);
        if (result != BROTLI_RESULT_SUCCESS) break;

        s->trivial_literal_context = 1;
        for (i = 0; i < s->num_block_types[0] << kLiteralContextBits; i++) {
          if (s->context_map[i] != i >> kLiteralContextBits) {
            s->trivial_literal_context = 0;
            break;
          }
        }

        s->state = BROTLI_STATE_CONTEXT_MAP_2;
        /* No break, continue to next state */
      case BROTLI_STATE_CONTEXT_MAP_2:
        result = DecodeContextMap(s->num_block_types[2] << kDistanceContextBits,
                                  &s->num_dist_htrees, &s->dist_context_map, s);
        if (result != BROTLI_RESULT_SUCCESS) break;

        BrotliHuffmanTreeGroupInit(
            &s->literal_hgroup, kNumLiteralCodes, s->num_literal_htrees);
        BrotliHuffmanTreeGroupInit(
            &s->insert_copy_hgroup, kNumInsertAndCopyCodes,
            s->num_block_types[1]);
        {
          int num_distance_codes = (s->num_direct_distance_codes +
                                    (48 << s->distance_postfix_bits));
          BrotliHuffmanTreeGroupInit(
              &s->distance_hgroup, num_distance_codes, s->num_dist_htrees);
        }
        i = 0;
        s->state = BROTLI_STATE_TREE_GROUP;
        /* No break, continue to next state */
      case BROTLI_STATE_TREE_GROUP:
        switch (i) {
          case 0:
            result = HuffmanTreeGroupDecode(&s->literal_hgroup, s);
            break;
          case 1:
            result = HuffmanTreeGroupDecode(&s->insert_copy_hgroup, s);
            break;
          case 2:
            result = HuffmanTreeGroupDecode(&s->distance_hgroup, s);
            break;
        }
        if (result != BROTLI_RESULT_SUCCESS) break;
        i++;

        if (i >= 3) {
          uint8_t context_mode = s->context_modes[s->block_type_rb[1]];
          s->context_map_slice = s->context_map;
          s->dist_context_map_slice = s->dist_context_map;
          s->context_lookup1 =
              &kContextLookup[kContextLookupOffsets[context_mode]];
          s->context_lookup2 =
              &kContextLookup[kContextLookupOffsets[context_mode + 1]];
          s->htree_command = s->insert_copy_hgroup.htrees[0];

          s->state = BROTLI_STATE_BLOCK_BEGIN;
          break;
        }

        break;
      case BROTLI_STATE_BLOCK_BEGIN:
 /* Block decoding is the inner loop, jumping with goto makes it 3% faster */
 BlockBegin:
        if (!BrotliCheckInputAmount(br, 32)) {
          result = BROTLI_RESULT_NEEDS_MORE_INPUT;
          break;
        }
        if (s->meta_block_remaining_len <= 0) {
          /* Next metablock, if any */
          s->state = BROTLI_STATE_METABLOCK_DONE;
          break;
        }

        if (s->block_length[1] == 0) {
          DecodeBlockType(s->num_block_types[1],
                          s->block_type_trees, 1,
                          s->block_type_rb, br);
          s->block_length[1] = ReadBlockLength(
              &s->block_len_trees[BROTLI_HUFFMAN_MAX_TABLE_SIZE], br);
          s->htree_command = s->insert_copy_hgroup.htrees[s->block_type_rb[3]];
        }
        s->distance_code = -1;
        --s->block_length[1];
        {
          int cmd_code = ReadSymbol(s->htree_command, br);
          const uint16_t *v = &kCmdLut[cmd_code][0];
          if (PREDICT_FALSE((cmd_code & ~0x7f) == 0)) {
            s->distance_code = 0;
          }
          i = (int)BrotliReadBits(br, v[0]) + v[1];
          s->copy_length = (int)BrotliReadBits(br, v[2]) + v[3];
        }
        BROTLI_LOG_UINT(i);
        BROTLI_LOG_UINT(s->copy_length);
        BROTLI_LOG_UINT(s->distance_code);

        s->state = BROTLI_STATE_BLOCK_INNER;
        s->meta_block_remaining_len -= i;
        /* No break, go to next state */
      case BROTLI_STATE_BLOCK_INNER:
        if (i != 0) {
          if (s->trivial_literal_context) {
            unsigned bits;
            unsigned value;
            const HuffmanCode* hc =
                s->literal_hgroup.htrees[s->literal_htree_index];
#if (BROTLI_PRELOAD_SYMBOLS)
            if (!BrotliCheckInputAmount(br, 32)) {
              result = BROTLI_RESULT_NEEDS_MORE_INPUT;
              break;
            }
            PreloadSymbol(hc, br, &bits, &value);
#endif
            do {
              if (!BrotliCheckInputAmount(br, 32)) {
                result = BROTLI_RESULT_NEEDS_MORE_INPUT;
                break;
              }
              if (PREDICT_FALSE(s->block_length[0] == 0)) {
                DecodeBlockTypeWithContext(s, br);
                hc = s->literal_hgroup.htrees[s->literal_htree_index];
                PreloadSymbol(hc, br, &bits, &value);
              }

              s->ringbuffer[pos] =
                  (uint8_t)ReadPreloadedSymbol(hc, br, &bits, &value);

              --s->block_length[0];
              BROTLI_LOG_UINT(s->literal_htree_index);
              BROTLI_LOG_ARRAY_INDEX(s->ringbuffer, pos);
              ++pos;
              if (PREDICT_FALSE(pos == s->ringbuffer_size)) {
                s->to_write = s->ringbuffer_size;
                s->partially_written = 0;
                s->state = BROTLI_STATE_BLOCK_INNER_WRITE;
                --i;
                break;
              }
            } while (--i != 0);
          } else {
            uint8_t p1 = s->ringbuffer[(pos - 1) & s->ringbuffer_mask];
            uint8_t p2 = s->ringbuffer[(pos - 2) & s->ringbuffer_mask];
            do {
              const HuffmanCode* hc;
              if (!BrotliCheckInputAmount(br, 32)) {
                result = BROTLI_RESULT_NEEDS_MORE_INPUT;
                break;
              }
              if (PREDICT_FALSE(s->block_length[0] == 0)) {
                DecodeBlockTypeWithContext(s, br);
              }

              context = s->context_lookup1[p1] | s->context_lookup2[p2];
              BROTLI_LOG_UINT(context);
              hc = s->literal_hgroup.htrees[s->context_map_slice[context]];
              --s->block_length[0];
              p2 = p1;
              p1 = (uint8_t)ReadSymbol(hc, br);
              s->ringbuffer[pos] = p1;
              BROTLI_LOG_UINT(s->context_map_slice[context]);
              BROTLI_LOG_ARRAY_INDEX(s->ringbuffer, pos & s->ringbuffer_mask);
              ++pos;
              if (PREDICT_FALSE(pos == s->ringbuffer_size)) {
                s->to_write = s->ringbuffer_size;
                s->partially_written = 0;
                s->state = BROTLI_STATE_BLOCK_INNER_WRITE;
                --i;
                break;
              }
            } while (--i != 0);
          }
        }
        if (result != BROTLI_RESULT_SUCCESS ||
            s->state == BROTLI_STATE_BLOCK_INNER_WRITE) break;

        if (s->meta_block_remaining_len <= 0) {
          s->state = BROTLI_STATE_METABLOCK_DONE;
          break;
        } else if (s->distance_code < 0) {
          s->state = BROTLI_STATE_BLOCK_DISTANCE;
        } else {
          s->state = BROTLI_STATE_BLOCK_POST;
          break;
        }
        /* No break, go to next state */
      case BROTLI_STATE_BLOCK_DISTANCE:
        if (!BrotliCheckInputAmount(br, 32)) {
          result = BROTLI_RESULT_NEEDS_MORE_INPUT;
          break;
        }
        BROTLI_DCHECK(s->distance_code < 0);

        if (s->block_length[2] == 0) {
          int dist_context_offset;
          DecodeBlockType(s->num_block_types[2],
                          s->block_type_trees, 2,
                          s->block_type_rb, br);
          s->block_length[2] = ReadBlockLength(
              &s->block_len_trees[2 * BROTLI_HUFFMAN_MAX_TABLE_SIZE], br);
          dist_context_offset = s->block_type_rb[5] << kDistanceContextBits;
          s->dist_context_map_slice =
              s->dist_context_map + dist_context_offset;
        }
        --s->block_length[2];
        context = (uint8_t)(s->copy_length > 4 ? 3 : s->copy_length - 2);
        s->dist_htree_index = s->dist_context_map_slice[context];
        s->distance_code =
            ReadSymbol(s->distance_hgroup.htrees[s->dist_htree_index], br);
        if (s->distance_code >= s->num_direct_distance_codes) {
          int nbits;
          int postfix;
          int offset;
          s->distance_code -= s->num_direct_distance_codes;
          postfix = s->distance_code & s->distance_postfix_mask;
          s->distance_code >>= s->distance_postfix_bits;
          nbits = (s->distance_code >> 1) + 1;
          offset = ((2 + (s->distance_code & 1)) << nbits) - 4;
          s->distance_code = s->num_direct_distance_codes +
              ((offset + (int)BrotliReadBits(br, nbits)) <<
               s->distance_postfix_bits) + postfix;
        }
        s->state = BROTLI_STATE_BLOCK_POST;
        /* No break, go to next state */
      case BROTLI_STATE_BLOCK_POST:
        if (!BrotliCheckInputAmount(br, 32)) {
          result = BROTLI_RESULT_NEEDS_MORE_INPUT;
          break;
        }
        /* Convert the distance code to the actual distance by possibly */
        /* looking up past distances from the s->ringbuffer. */
        if ((s->distance_code & ~0xf) == 0) {
          /* kDistanceShortCodeIndexOffset has 2-bit values from LSB: */
          /* 3, 2, 1, 0, 3, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2 */
          const uint32_t kDistanceShortCodeIndexOffset = 0xaaafff1b;
          /* kDistanceShortCodeValueOffset has 2-bit values from LSB: */
          /* 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 1, 1, 2, 2, 3, 3 */
          const uint32_t kDistanceShortCodeValueOffset = 0xfa5fa500;
          s->distance_code <<= 1;
          {
            int v = (s->dist_rb_idx +
                     (int)(kDistanceShortCodeIndexOffset >>
                           s->distance_code)) & 0x3;
            s->distance = s->dist_rb[v];
            if (s->distance_code == 0) {
              s->dist_rb_idx--;
            } else {
              int v = (int)(kDistanceShortCodeValueOffset >>
                            s->distance_code) & 0x3;
              if ((s->distance_code & 0x3) != 0) {
                s->distance += v;
              } else {
                s->distance -= v;
                if (s->distance < 0) {
                  result = BROTLI_FAILURE();
                  break;
                }
              }
            }
          }
        } else {
          s->distance = s->distance_code - NUM_DISTANCE_SHORT_CODES + 1;
        }
        BROTLI_LOG_UINT(s->distance);

        if (pos + s->custom_dict_size < s->max_backward_distance &&
            s->max_distance != s->max_backward_distance) {
          s->max_distance = pos + s->custom_dict_size;
        } else {
          s->max_distance = s->max_backward_distance;
        }
        i = s->copy_length;
        if (s->distance > s->max_distance) {
          if (i >= kMinDictionaryWordLength &&
              i <= kMaxDictionaryWordLength) {
            int offset = kBrotliDictionaryOffsetsByLength[i];
            int word_id = s->distance - s->max_distance - 1;
            int shift = kBrotliDictionarySizeBitsByLength[i];
            int mask = (1 << shift) - 1;
            int word_idx = word_id & mask;
            int transform_idx = word_id >> shift;
            offset += word_idx * i;
            if (transform_idx < kNumTransforms) {
              const uint8_t* word = &kBrotliDictionary[offset];
              int len = i;
              if (transform_idx == 0) {
                memcpy(&s->ringbuffer[pos], word, (size_t)len);
              } else {
                len = TransformDictionaryWord(
                    &s->ringbuffer[pos], word, len, transform_idx);
              }
              pos += len;
              s->meta_block_remaining_len -= len;
              if (pos >= s->ringbuffer_size) {
                s->to_write = s->ringbuffer_size;
                s->partially_written = 0;
                s->state = BROTLI_STATE_BLOCK_POST_WRITE_1;
                break;
              }
            } else {
              BROTLI_LOG(("Invalid backward reference. pos: %d distance: %d "
                     "len: %d bytes left: %d\n",
                  pos, s->distance, i,
                  s->meta_block_remaining_len));
              result = BROTLI_FAILURE();
              break;
            }
          } else {
            BROTLI_LOG(("Invalid backward reference. pos: %d distance: %d "
                   "len: %d bytes left: %d\n", pos, s->distance, i,
                   s->meta_block_remaining_len));
            result = BROTLI_FAILURE();
            break;
          }
        } else {
          const uint8_t *ringbuffer_end_minus_copy_length =
              s->ringbuffer_end - i;
          copy_src = &s->ringbuffer[(pos - s->distance) & s->ringbuffer_mask];
          copy_dst = &s->ringbuffer[pos];

          s->dist_rb[s->dist_rb_idx & 3] = s->distance;
          ++s->dist_rb_idx;

          s->meta_block_remaining_len -= i;

          if (PREDICT_FALSE(s->meta_block_remaining_len < 0)) {
            BROTLI_LOG(("Invalid backward reference. pos: %d distance: %d "
                   "len: %d bytes left: %d\n", pos, s->distance, i,
                   s->meta_block_remaining_len));
            result = BROTLI_FAILURE();
            break;
          }
          /* There is 128+ bytes of slack in the ringbuffer allocation.
             Also, we have 16 short codes, that make these 16 bytes irrelevant
             in the ringbuffer. Let's copy over them as a first guess.
           */
          memmove16(copy_dst, copy_src);

          /* Now check if the copy extends over the ringbuffer end,
             or if the copy overlaps with itself, if yes, do wrap-copy. */
          if (copy_src < copy_dst) {
            if (copy_dst >= ringbuffer_end_minus_copy_length) {
              goto postWrapCopy;
            }
            if (copy_src + i > copy_dst) {
              goto postSelfintersecting;
            }
          } else {
            if (copy_src >= ringbuffer_end_minus_copy_length) {
              goto postWrapCopy;
            }
            if (copy_dst + i > copy_src) {
              goto postSelfintersecting;
            }
          }
          pos += i;
          if (i > 16) {
            if (i > 32) {
              memcpy(copy_dst + 16, copy_src + 16, (size_t)(i - 16));
            } else {
              /* This branch covers about 45% cases.
                 Fixed size short copy allows more compiler optimizations. */
              memmove16(copy_dst + 16, copy_src + 16);
            }
          }
        }
        s->state = BROTLI_STATE_BLOCK_BEGIN;
        goto BlockBegin;
      postSelfintersecting:
        while (--i >= 0) {
          s->ringbuffer[pos] =
              s->ringbuffer[(pos - s->distance) & s->ringbuffer_mask];
          ++pos;
        }
        goto BlockBegin;
      postWrapCopy:
        s->state = BROTLI_STATE_BLOCK_POST_WRAP_COPY;
      case BROTLI_STATE_BLOCK_POST_WRAP_COPY:
        while (--i >= 0) {
          s->ringbuffer[pos] =
              s->ringbuffer[(pos - s->distance) & s->ringbuffer_mask];
          ++pos;
          if (pos == s->ringbuffer_size) {
            s->to_write = s->ringbuffer_size;
            s->partially_written = 0;
            s->state = BROTLI_STATE_BLOCK_POST_WRITE_2;
            break;
          }
        }
        if (s->state == BROTLI_STATE_BLOCK_POST_WRAP_COPY) {
          s->state = BROTLI_STATE_BLOCK_BEGIN;
          goto BlockBegin;
        }
        break;
      case BROTLI_STATE_BLOCK_INNER_WRITE:
      case BROTLI_STATE_BLOCK_POST_WRITE_1:
      case BROTLI_STATE_BLOCK_POST_WRITE_2:
        result = WriteRingBuffer(output, s);
        if (result != BROTLI_RESULT_SUCCESS) {
          break;
        }
        pos -= s->ringbuffer_size;
        s->max_distance = s->max_backward_distance;
        if (s->state == BROTLI_STATE_BLOCK_POST_WRITE_1) {
          memcpy(s->ringbuffer, s->ringbuffer_end, (size_t)pos);
          s->state = BROTLI_STATE_BLOCK_BEGIN;
          goto BlockBegin;
        } else if (s->state == BROTLI_STATE_BLOCK_POST_WRITE_2) {
          s->state = BROTLI_STATE_BLOCK_POST_WRAP_COPY;
        } else {  /* BROTLI_STATE_BLOCK_INNER_WRITE */
          s->state = BROTLI_STATE_BLOCK_INNER;
        }
        break;
      case BROTLI_STATE_METABLOCK_DONE:
        BrotliStateCleanupAfterMetablock(s);
        s->state = BROTLI_STATE_METABLOCK_BEGIN;
        break;
      case BROTLI_STATE_DONE:
        if (s->ringbuffer != 0) {
          result = WriteRingBuffer(output, s);
          if (result != BROTLI_RESULT_SUCCESS) {
            break;
          }
        }
        if (!BrotliJumpToByteBoundary(&s->br)) {
          result = BROTLI_FAILURE();
        }
        return result;
    }
  }

  s->pos = pos;
  s->loop_counter = i;
  return result;
}

void BrotliSetCustomDictionary(
    size_t size, const uint8_t* dict, BrotliState* s) {
  s->custom_dict = dict;
  s->custom_dict_size = (int) size;
}

#if defined(__cplusplus) || defined(c_plusplus)
}    /* extern "C" */
#endif
