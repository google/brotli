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
#else
#define BROTLI_LOG_UINT(name)
#define BROTLI_LOG_ARRAY_INDEX(array_name, idx)
#define BROTLI_LOG(x)
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

/* Static prefix code for the complex code length code lengths. */
static const uint8_t kCodeLengthPrefixLength[16] = {
  2, 2, 2, 3, 2, 2, 2, 4, 2, 2, 2, 3, 2, 2, 2, 4,
};

static const uint8_t kCodeLengthPrefixValue[16] = {
  0, 4, 3, 2, 0, 4, 3, 1, 0, 4, 3, 2, 0, 4, 3, 5,
};

#define NUM_DISTANCE_SHORT_CODES 16

/* Decodes a number in the range [9..24], by reading 1 - 7 bits.
   Precondition: bit-reader accumulator has at least 7 bits. */
static uint32_t DecodeWindowBits(BrotliBitReader* br) {
  uint32_t n;
  BrotliTakeBits(br, 1, &n);
  if (n == 0) {
    return 16;
  }
  BrotliTakeBits(br, 3, &n);
  if (n != 0) {
    return 17 + n;
  }
  BrotliTakeBits(br, 3, &n);
  if (n != 0) {
    return 8 + n;
  }
  return 17;
}

static BROTLI_INLINE BROTLI_NO_ASAN void memmove16(
    uint8_t* dst, uint8_t* src) {
#if BROTLI_SAFE_MEMMOVE
  /* For x86 this compiles to the same binary as signle memcpy.
     On ARM memcpy is not inlined, so it works slower.
     This implementation makes decompression 1% slower than regular one,
     and 2% slower than NEON implementation.
   */
  uint32_t buffer[4];
  memcpy(buffer, src, 16);
  memcpy(dst, buffer, 16);
#elif defined(__ARM_NEON__)
  vst1q_u8(dst, vld1q_u8(src));
#else
  /* memcpy is unsafe for overlapping regions and ASAN detects this.
     But, because of optimizations, it works exactly as memmove:
     copies data to registers first, and then stores them to dst. */
  memcpy(dst, src, 16);
#endif
}

/* Decodes a number in the range [0..255], by reading 1 - 11 bits. */
static BROTLI_NOINLINE BrotliResult DecodeVarLenUint8(BrotliState* s,
    BrotliBitReader* br, int* value) {
  uint32_t bits;
  switch (s->substate_decode_uint8) {
    case BROTLI_STATE_DECODE_UINT8_NONE:
      if (PREDICT_FALSE(!BrotliSafeReadBits(br, 1, &bits))) {
        return BROTLI_RESULT_NEEDS_MORE_INPUT;
      }
      if (bits == 0) {
        *value = 0;
        return BROTLI_RESULT_SUCCESS;
      }
      /* No break, transit to the next state. */

    case BROTLI_STATE_DECODE_UINT8_SHORT:
      if (PREDICT_FALSE(!BrotliSafeReadBits(br, 3, &bits))) {
        s->substate_decode_uint8 = BROTLI_STATE_DECODE_UINT8_SHORT;
        return BROTLI_RESULT_NEEDS_MORE_INPUT;
      }
      if (bits == 0) {
        *value = 1;
        s->substate_decode_uint8 = BROTLI_STATE_DECODE_UINT8_NONE;
        return BROTLI_RESULT_SUCCESS;
      }
      /* Use output value as a temporary storage. It MUST be persisted. */
      *value = (int)bits;
      /* No break, transit to the next state. */

    case BROTLI_STATE_DECODE_UINT8_LONG:
      if (PREDICT_FALSE(!BrotliSafeReadBits(br, *value, &bits))) {
        s->substate_decode_uint8 = BROTLI_STATE_DECODE_UINT8_LONG;
        return BROTLI_RESULT_NEEDS_MORE_INPUT;
      }
      *value = (1 << *value) + (int)bits;
      s->substate_decode_uint8 = BROTLI_STATE_DECODE_UINT8_NONE;
      return BROTLI_RESULT_SUCCESS;

    default:
      return BROTLI_FAILURE();
  }
}

/* Decodes a metablock length and flags by reading 2 - 31 bits. */
static BrotliResult BROTLI_NOINLINE DecodeMetaBlockLength(BrotliState* s,
                                                          BrotliBitReader* br) {
  uint32_t bits;
  int i;
  for (;;) {
    switch (s->substate_metablock_header) {
      case BROTLI_STATE_METABLOCK_HEADER_NONE:
        if (!BrotliSafeReadBits(br, 1, &bits)) {
          return BROTLI_RESULT_NEEDS_MORE_INPUT;
        }
        s->is_last_metablock = (uint8_t)bits;
        s->meta_block_remaining_len = 0;
        s->is_uncompressed = 0;
        s->is_metadata = 0;
        if (!s->is_last_metablock) {
          s->substate_metablock_header = BROTLI_STATE_METABLOCK_HEADER_NIBBLES;
          break;
        }
        s->substate_metablock_header = BROTLI_STATE_METABLOCK_HEADER_EMPTY;
        /* No break, transit to the next state. */

      case BROTLI_STATE_METABLOCK_HEADER_EMPTY:
        if (!BrotliSafeReadBits(br, 1, &bits)) {
          return BROTLI_RESULT_NEEDS_MORE_INPUT;
        }
        if (bits) {
          s->substate_metablock_header = BROTLI_STATE_METABLOCK_HEADER_NONE;
          return BROTLI_RESULT_SUCCESS;
        }
        s->substate_metablock_header = BROTLI_STATE_METABLOCK_HEADER_NIBBLES;
        /* No break, transit to the next state. */

      case BROTLI_STATE_METABLOCK_HEADER_NIBBLES:
        if (!BrotliSafeReadBits(br, 2, &bits)) {
          return BROTLI_RESULT_NEEDS_MORE_INPUT;
        }
        s->size_nibbles = (uint8_t)(bits + 4);
        s->loop_counter = 0;
        if (bits == 3) {
          s->is_metadata = 1;
          s->substate_metablock_header = BROTLI_STATE_METABLOCK_HEADER_RESERVED;
          break;
        }
        s->substate_metablock_header = BROTLI_STATE_METABLOCK_HEADER_SIZE;
        /* No break, transit to the next state. */

      case BROTLI_STATE_METABLOCK_HEADER_SIZE:
        i = s->loop_counter;
        for (; i < s->size_nibbles; ++i) {
          if (!BrotliSafeReadBits(br, 4, &bits)) {
            s->loop_counter = i;
            return BROTLI_RESULT_NEEDS_MORE_INPUT;
          }
          if (i + 1 == s->size_nibbles && s->size_nibbles > 4 && bits == 0) {
            return BROTLI_FAILURE();
          }
          s->meta_block_remaining_len |= (int)(bits << (i * 4));
        }
        s->substate_metablock_header =
            BROTLI_STATE_METABLOCK_HEADER_UNCOMPRESSED;
        /* No break, transit to the next state. */

      case BROTLI_STATE_METABLOCK_HEADER_UNCOMPRESSED:
        if (!s->is_last_metablock && !s->is_metadata) {
          if (!BrotliSafeReadBits(br, 1, &bits)) {
            return BROTLI_RESULT_NEEDS_MORE_INPUT;
          }
          s->is_uncompressed = (uint8_t)bits;
        }
        ++s->meta_block_remaining_len;
        s->substate_metablock_header = BROTLI_STATE_METABLOCK_HEADER_NONE;
        return BROTLI_RESULT_SUCCESS;

      case BROTLI_STATE_METABLOCK_HEADER_RESERVED:
        if (!BrotliSafeReadBits(br, 1, &bits)) {
          return BROTLI_RESULT_NEEDS_MORE_INPUT;
        }
        if (bits != 0) {
          return BROTLI_FAILURE();
        }
        s->substate_metablock_header = BROTLI_STATE_METABLOCK_HEADER_BYTES;
        /* No break, transit to the next state. */

      case BROTLI_STATE_METABLOCK_HEADER_BYTES:
        if (!BrotliSafeReadBits(br, 2, &bits)) {
          return BROTLI_RESULT_NEEDS_MORE_INPUT;
        }
        if (bits == 0) {
          s->substate_metablock_header = BROTLI_STATE_METABLOCK_HEADER_NONE;
          return BROTLI_RESULT_SUCCESS;
        }
        s->size_nibbles = (uint8_t)bits;
        s->substate_metablock_header = BROTLI_STATE_METABLOCK_HEADER_METADATA;
        /* No break, transit to the next state. */

      case BROTLI_STATE_METABLOCK_HEADER_METADATA:
        i = s->loop_counter;
        for (; i < s->size_nibbles; ++i) {
          if (!BrotliSafeReadBits(br, 8, &bits)) {
            s->loop_counter = i;
            return BROTLI_RESULT_NEEDS_MORE_INPUT;
          }
          if (i + 1 == s->size_nibbles && s->size_nibbles > 1 && bits == 0) {
            return BROTLI_FAILURE();
          }
          s->meta_block_remaining_len |= (int)(bits << (i * 8));
        }
        s->substate_metablock_header =
            BROTLI_STATE_METABLOCK_HEADER_UNCOMPRESSED;
        break;

      default:
        return BROTLI_FAILURE();
    }
  }
}

/* Decodes the next Huffman code from bit-stream. Reads 0 - 15 bits. */
static BROTLI_INLINE int ReadSymbol(const HuffmanCode* table,
                                    BrotliBitReader* br) {
  /* Read the bits for two reads at once. */
  uint32_t val = BrotliGetBitsUnmasked(br, 15);
  table += val & HUFFMAN_TABLE_MASK;
  if (table->bits > HUFFMAN_TABLE_BITS) {
    int nbits = table->bits - HUFFMAN_TABLE_BITS;
    BrotliDropBits(br, HUFFMAN_TABLE_BITS);
    table += table->value;
    table += (int)(val >> HUFFMAN_TABLE_BITS) & (int)BitMask(nbits);
  }
  BrotliDropBits(br, table->bits);
  return table->value;
}

/* Makes a look-up in first level Huffman table. Peeks 8 bits. */
static BROTLI_INLINE void PreloadSymbol(const HuffmanCode* table,
                                        BrotliBitReader* br,
                                        unsigned* bits,
                                        unsigned* value) {
  table += BrotliGetBits(br, HUFFMAN_TABLE_BITS);
  *bits = table->bits;
  *value = table->value;
}

/* Decodes the next Huffman code using data prepared by PreloadSymbol.
   Reads 0 - 15 bits. Also peeks 8 following bits. */
static BROTLI_INLINE unsigned ReadPreloadedSymbol(const HuffmanCode* table,
                                                  BrotliBitReader* br,
                                                  unsigned* bits,
                                                  unsigned* value) {
  unsigned result = *value;
  if (PREDICT_FALSE(*bits > HUFFMAN_TABLE_BITS)) {
    uint32_t val = BrotliGetBitsUnmasked(br, 15);
    const HuffmanCode* ext = table + (val & HUFFMAN_TABLE_MASK) + *value;
    int mask = (int)BitMask((int)(*bits - HUFFMAN_TABLE_BITS));
    BrotliDropBits(br, HUFFMAN_TABLE_BITS);
    ext += (int)(val >> HUFFMAN_TABLE_BITS) & mask;
    BrotliDropBits(br, ext->bits);
    result = ext->value;
  } else {
    BrotliDropBits(br, (int)*bits);
  }
  PreloadSymbol(table, br, bits, value);
  return result;
}

static BROTLI_INLINE int Log2Floor(int x) {
  int result = 0;
  while (x) {
    x >>= 1;
    ++result;
  }
  return result;
}

/* Decodes the Huffman tables.
   There are 2 scenarios:
    A) Huffman code contains only few symbols (1..4). Those symbols are read
       directly; their code lengths are defined by the number of symbols.
       For this scenario 4 - 45 bits will be read.

    B) 2-phase decoding:
    B.1) Small Huffman table is decoded; it is specified with code lengths
         encoded with predefined entropy code. 32 - 74 bits are used.
    B.2) Decoded table is used to decode code lengths of symbols in resulting
         Huffman table. In worst case 3520 bits are read.
*/
static BrotliResult ReadHuffmanCode(int alphabet_size,
                                    HuffmanCode* table,
                                    int* opt_table_size,
                                    BrotliState* s) {
  BrotliBitReader* br = &s->br;
  int i;
  /* Unnecessary masking, but might be good for safety. */
  alphabet_size &= 0x3ff;
  /* State machine */
  switch (s->substate_huffman) {
    case BROTLI_STATE_HUFFMAN_NONE:
      if (!BrotliCheckInputAmount(br, 32)) {
        return BROTLI_RESULT_NEEDS_MORE_INPUT;
      }
      i = (int)BrotliReadBits(br, 2);
      /* The value is used as follows:
         1 for simple code;
         0 for no skipping, 2 skips 2 code lengths, 3 skips 3 code lengths */
      BROTLI_LOG_UINT((unsigned)i);
      if (i == 1) {
        /* Read symbols, codes & code lengths directly. */
        int max_bits = Log2Floor(alphabet_size - 1);
        uint32_t num_symbols = BrotliReadBits(br, 2);
        for (i = 0; i < 4; ++i) {
          s->symbols_lists_array[i] = 0;
        }
        i = 0;
        /* max_bits == 0..10; symbol == 0..3; 0..40 bits will be read. */
        do {
          uint32_t v = BrotliReadBits(br, max_bits);
          if (v >= alphabet_size) {
            return BROTLI_FAILURE();
          }
          s->symbols_lists_array[i] = (uint16_t)v;
          BROTLI_LOG_UINT(s->symbols_lists_array[i]);
        } while (++i <= num_symbols);
        for (i = 0; i < num_symbols; ++i) {
          int k = i + 1;
          for (; k <= num_symbols; ++k) {
            if (s->symbols_lists_array[i] == s->symbols_lists_array[k]) {
              return BROTLI_FAILURE();
            }
          }
        }
        if (num_symbols == 3) {
          num_symbols += BrotliReadBits(br, 1);
        }
        BROTLI_LOG_UINT(num_symbols);
        i = BrotliBuildSimpleHuffmanTable(
            table, HUFFMAN_TABLE_BITS, s->symbols_lists_array, num_symbols);
        if (opt_table_size) {
          *opt_table_size = i;
        }
        s->substate_huffman = BROTLI_STATE_HUFFMAN_NONE;
        return BROTLI_RESULT_SUCCESS;
      } else {  /* Decode Huffman-coded code lengths. */
        int8_t num_codes = 0;
        unsigned space = 32;
        memset(&s->code_length_histo[0], 0, sizeof(s->code_length_histo[0]) *
            (BROTLI_HUFFMAN_MAX_CODE_LENGTH_CODE_LENGTH + 1));
        memset(&s->code_length_code_lengths[0], 0,
               sizeof(s->code_length_code_lengths));
        /* 15..18 codes will be read, 2..4 bits each; 30..72 bits totally. */
        for (; i < CODE_LENGTH_CODES; ++i) {
          const uint8_t code_len_idx = kCodeLengthCodeOrder[i];
          uint8_t ix = (uint8_t)BrotliGetBits(br, 4);
          uint8_t v = kCodeLengthPrefixValue[ix];
          BrotliDropBits(br, kCodeLengthPrefixLength[ix]);
          s->code_length_code_lengths[code_len_idx] = v;
          BROTLI_LOG_ARRAY_INDEX(s->code_length_code_lengths, code_len_idx);
          if (v != 0) {
            space = space - (32U >> v);
            ++num_codes;
            ++s->code_length_histo[v];
            if (space - 1U >= 32U) {
              /* space is 0 or wrapped around */
              break;
            }
          }
        }
        if (!(num_codes == 1 || space == 0)) {
          return BROTLI_FAILURE();
        }
      }
      BrotliBuildCodeLengthsHuffmanTable(s->table,
                                         s->code_length_code_lengths,
                                         s->code_length_histo);
      memset(&s->code_length_histo[0], 0, sizeof(s->code_length_histo));
      for (i = 0; i <= BROTLI_HUFFMAN_MAX_CODE_LENGTH; ++i) {
        s->next_symbol[i] = i - (BROTLI_HUFFMAN_MAX_CODE_LENGTH + 1);
        s->symbol_lists[i - (BROTLI_HUFFMAN_MAX_CODE_LENGTH + 1)] = 0xFFFF;
      }

      s->symbol = 0;
      s->prev_code_len = kDefaultCodeLength;
      s->repeat = 0;
      s->repeat_code_len = 0;
      s->space = 32768;
      s->substate_huffman = BROTLI_STATE_HUFFMAN_LENGTH_SYMBOLS;
      /* No break, transit to the next state. */
    case BROTLI_STATE_HUFFMAN_LENGTH_SYMBOLS: {
      uint32_t symbol = s->symbol;
      uint32_t repeat = s->repeat;
      uint32_t space = s->space;
      uint8_t prev_code_len = s->prev_code_len;
      uint8_t repeat_code_len = s->repeat_code_len;
      uint16_t* symbol_lists = s->symbol_lists;
      uint16_t* code_length_histo = s->code_length_histo;
      int* next_symbol = s->next_symbol;
      while (symbol < alphabet_size && space > 0) {
        const HuffmanCode* p = s->table;
        uint8_t code_len;
        if (!BrotliCheckInputAmount(br, 8)) {
          s->symbol = symbol;
          s->repeat = repeat;
          s->prev_code_len = prev_code_len;
          s->repeat_code_len = repeat_code_len;
          s->space = space;
          return BROTLI_RESULT_NEEDS_MORE_INPUT;
        }
        p += BrotliGetBits(br, BROTLI_HUFFMAN_MAX_CODE_LENGTH_CODE_LENGTH);
        BrotliDropBits(br, p->bits); /* Use 1..5 bits */
        code_len = (uint8_t)p->value; /* code_len == 0..17 */
        if (code_len < kCodeLengthRepeatCode) {
          repeat = 0;
          if (code_len != 0) { /* code_len == 1..15 */
            symbol_lists[next_symbol[code_len]] = (uint16_t)symbol;
            next_symbol[code_len] = (int)symbol;
            prev_code_len = code_len;
            space -= 32768U >> code_len;
            code_length_histo[code_len]++;
          }
          symbol++;
        } else { /* code_len == 16..17, extra_bits == 2..3 */
          uint32_t repeat_delta = BrotliReadBits(br, code_len - 14);
          uint32_t old_repeat;
          uint8_t new_len = 0;
          if (code_len == kCodeLengthRepeatCode) {
            new_len = prev_code_len;
          }
          if (repeat_code_len != new_len) {
            repeat = 0;
            repeat_code_len = new_len;
          }
          old_repeat = repeat;
          if (repeat > 0) {
            repeat -= 2;
            repeat <<= code_len - 14;
          }
          repeat += repeat_delta + 3;
          repeat_delta = repeat - old_repeat; /* repeat_delta >= 3 */
          /* So, for extra 2..3 bits we produce more than 2 symbols.
             Consequently, at most 5 bits per symbol are used. */
          if (symbol + repeat_delta > alphabet_size) {
            return BROTLI_FAILURE();
          }
          if (repeat_code_len != 0) {
            unsigned last = symbol + repeat_delta;
            i = next_symbol[repeat_code_len];
            do {
              symbol_lists[i] = (uint16_t)symbol;
              i = (int)symbol;
            } while (++symbol != last);
            next_symbol[repeat_code_len] = i;
            space -= repeat_delta << (15 - repeat_code_len);
            code_length_histo[repeat_code_len] = (uint16_t)
                (code_length_histo[repeat_code_len] + repeat_delta);
          } else {
            symbol += repeat_delta;
          }
        }
      }
      if (space != 0) {
        BROTLI_LOG(("[ReadHuffmanCode] space = %d\n", space));
        return BROTLI_FAILURE();
      }
      {
        int table_size = BrotliBuildHuffmanTable(
            table, HUFFMAN_TABLE_BITS, symbol_lists,
            s->code_length_histo);
        if (opt_table_size) {
          *opt_table_size = table_size;
        }
      }
      s->substate_huffman = BROTLI_STATE_HUFFMAN_NONE;
      return BROTLI_RESULT_SUCCESS;
    }

    default:
      return BROTLI_FAILURE();
  }
}

/* Decodes a block length by reading 3..39 bits. */
static BROTLI_INLINE int ReadBlockLength(const HuffmanCode* table,
                                         BrotliBitReader* br) {
  int code;
  int nbits;
  code = ReadSymbol(table, br);
  nbits = kBlockLengthPrefixCode[code].nbits; /* nbits == 2..24 */
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

   Most of input values are 0 and 1. To reduce number of branches, we replace
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


/* Decodes a series of Huffman table using ReadHuffmanCode function. */
static BrotliResult HuffmanTreeGroupDecode(HuffmanTreeGroup* group,
                                           BrotliState* s) {
  if (s->substate_tree_group != BROTLI_STATE_TREE_GROUP_LOOP) {
    s->next = group->codes;
    s->htree_index = 0;
    s->substate_tree_group = BROTLI_STATE_TREE_GROUP_LOOP;
  }
  while (s->htree_index < group->num_htrees) {
    int table_size;
    BrotliResult result =
        ReadHuffmanCode(group->alphabet_size, s->next, &table_size, s);
    if (result != BROTLI_RESULT_SUCCESS) return result;
    group->htrees[s->htree_index] = s->next;
    s->next += table_size;
    ++s->htree_index;
  }
  s->substate_tree_group = BROTLI_STATE_TREE_GROUP_NONE;
  return BROTLI_RESULT_SUCCESS;
}

/* Decodes a context map.
   Decoding is done in 4 phases:
    1) Read auxiliary information (6..16 bits) and allocate memory.
       In case of trivial context map, decoding is finished at this phase.
    2) Decode Huffman table using ReadHuffmanCode function.
       This table will be used for reading context map items.
    3) Read context map items; "0" values could be run-length encoded.
    4) Optionally, apply InverseMoveToFront transform to the resulting map.
 */
static BrotliResult DecodeContextMap(int context_map_size,
                                     int* num_htrees,
                                     uint8_t** context_map_arg,
                                     BrotliState* s) {
  BrotliBitReader* br = &s->br;
  BrotliResult result = BROTLI_RESULT_SUCCESS;
  int use_rle_for_zeros;

  switch((int)s->substate_context_map) {
    case BROTLI_STATE_CONTEXT_MAP_NONE:
      result = DecodeVarLenUint8(s, br, num_htrees);
      if (result != BROTLI_RESULT_SUCCESS) {
        return result;
      }
      (*num_htrees)++;
      s->context_index = 0;
      BROTLI_LOG_UINT(context_map_size);
      BROTLI_LOG_UINT(*num_htrees);
      *context_map_arg = (uint8_t*)malloc((size_t)context_map_size);
      if (*context_map_arg == 0) {
        return BROTLI_FAILURE();
      }
      if (*num_htrees <= 1) {
        memset(*context_map_arg, 0, (size_t)context_map_size);
        return BROTLI_RESULT_SUCCESS;
      }
      s->substate_context_map = BROTLI_STATE_CONTEXT_MAP_READ_PREFIX;
      /* No break, continue to next state. */
    case BROTLI_STATE_CONTEXT_MAP_READ_PREFIX:
      if (!BrotliWarmupBitReader(br) || !BrotliCheckInputAmount(br, 8)) {
        return BROTLI_RESULT_NEEDS_MORE_INPUT;
      }
      use_rle_for_zeros = (int)BrotliReadBits(br, 1);
      if (use_rle_for_zeros) {
        s->max_run_length_prefix = (int)BrotliReadBits(br, 4) + 1;
      } else {
        s->max_run_length_prefix = 0;
      }
      BROTLI_LOG_UINT(s->max_run_length_prefix);
      s->substate_context_map = BROTLI_STATE_CONTEXT_MAP_HUFFMAN;
      /* No break, continue to next state. */
    case BROTLI_STATE_CONTEXT_MAP_HUFFMAN:
      result = ReadHuffmanCode(*num_htrees + s->max_run_length_prefix,
                               s->context_map_table, NULL, s);
      if (result != BROTLI_RESULT_SUCCESS) return result;
      s->substate_context_map = BROTLI_STATE_CONTEXT_MAP_DECODE;
      /* No break, continue to next state. */
    case BROTLI_STATE_CONTEXT_MAP_DECODE: {
      int context_index = s->context_index;
      int max_run_length_prefix = s->max_run_length_prefix;
      uint8_t* context_map = *context_map_arg;
      int code;
      while (context_index < context_map_size) {
        if (!BrotliCheckInputAmount(br, 32)) {
          s->context_index = context_index;
          return BROTLI_RESULT_NEEDS_MORE_INPUT;
        }
        code = ReadSymbol(s->context_map_table, br);
        BROTLI_LOG_UINT(code);
        if (code == 0) {
          context_map[context_index++] = 0;
        } else if (code - max_run_length_prefix <= 0) {
          int reps = (1 << code) + (int)BrotliReadBits(br, code);
          BROTLI_LOG_UINT(reps);
          if (context_index + reps > context_map_size) {
            return BROTLI_FAILURE();
          }
          do {
            context_map[context_index++] = 0;
          } while (--reps);
        } else {
          context_map[context_index++] =
              (uint8_t)(code - max_run_length_prefix);
        }
      }
      if (BrotliReadBits(br, 1)) {
        InverseMoveToFrontTransform(context_map, context_map_size, s);
      }
      s->substate_context_map = BROTLI_STATE_CONTEXT_MAP_NONE;
      return BROTLI_RESULT_SUCCESS;
    }
  }

  return BROTLI_FAILURE();
}

/* Decodes a command or literal and updates block type ringbuffer.
   Reads 0..15 bits. */
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

/* Decodes the block type and updates the state for literal context.
   Reads 18..54 bits. */
static void DecodeBlockTypeWithContext(BrotliState* s,
                                       BrotliBitReader* br) {
  uint8_t context_mode;
  int context_offset;
  DecodeBlockType(s->num_block_types[0], s->block_type_trees, 0,
                  s->block_type_rb, br); /* Reads 0..15 bits. */
  s->block_length[0] = ReadBlockLength(s->block_len_trees, br); /* 3..39 bits */
  context_offset = s->block_type_rb[1] << kLiteralContextBits;
  s->context_map_slice = s->context_map + context_offset;
  s->literal_htree_index = s->context_map_slice[0];
  s->literal_htree = s->literal_hgroup.htrees[s->literal_htree_index];
  context_mode = s->context_modes[s->block_type_rb[1]];
  s->context_lookup1 = &kContextLookup[kContextLookupOffsets[context_mode]];
  s->context_lookup2 = &kContextLookup[kContextLookupOffsets[context_mode + 1]];
}

BrotliResult WriteRingBuffer(BrotliOutput output,
                             BrotliState* s) {
  int num_written;
  if (s->meta_block_remaining_len < 0) {
    return BROTLI_FAILURE();
  }
  num_written = BrotliWrite(
      output, s->ringbuffer + s->partially_written,
      (size_t)(s->to_write - s->partially_written));
  BROTLI_LOG_UINT(s->partially_written);
  BROTLI_LOG_UINT(s->to_write);
  BROTLI_LOG_UINT(num_written);
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
  int nbytes;
  /* State machine */
  for (;;) {
    switch ((int)s->substate_uncompressed) {
      case BROTLI_STATE_UNCOMPRESSED_NONE:
        /* For short lengths copy byte-by-byte */
        if (s->meta_block_remaining_len < 8 ||
            s->meta_block_remaining_len < BrotliGetRemainingBytes(&s->br)) {
          s->substate_uncompressed = BROTLI_STATE_UNCOMPRESSED_SHORT;
          break;
        }
        /* Copy remaining bytes from s->br.buf_ to ringbuffer. */
        nbytes = (int)BrotliGetRemainingBytes(&s->br);
        BrotliCopyBytes(&s->ringbuffer[pos], &s->br, (size_t)nbytes);
        pos += nbytes;
        s->meta_block_remaining_len -= nbytes;
        if (pos >= s->ringbuffer_size) {
          s->to_write = s->ringbuffer_size;
          s->partially_written = 0;
          s->substate_uncompressed = BROTLI_STATE_UNCOMPRESSED_WRITE;
          break;
        }
        s->substate_uncompressed = BROTLI_STATE_UNCOMPRESSED_COPY;
        break;
      case BROTLI_STATE_UNCOMPRESSED_SHORT:
        if (!BrotliWarmupBitReader(&s->br)) {
          return BROTLI_RESULT_NEEDS_MORE_INPUT;
        }
        while (s->meta_block_remaining_len > 0) {
          if (!BrotliCheckInputAmount(&s->br, 8)) {
            return BROTLI_RESULT_NEEDS_MORE_INPUT;
          }
          s->ringbuffer[pos++] = (uint8_t)BrotliReadBits(&s->br, 8);
          s->meta_block_remaining_len--;
        }
        if (pos >= s->ringbuffer_size) {
          s->to_write = s->ringbuffer_size;
          s->partially_written = 0;
          s->substate_uncompressed = BROTLI_STATE_UNCOMPRESSED_WRITE;
        } else {
          s->substate_uncompressed = BROTLI_STATE_UNCOMPRESSED_NONE;
          return BROTLI_RESULT_SUCCESS;
        }
        /* No break, if state is updated, continue to next state */
      case BROTLI_STATE_UNCOMPRESSED_WRITE:
        result = WriteRingBuffer(output, s);
        if (result != BROTLI_RESULT_SUCCESS) {
          return result;
        }
        pos &= s->ringbuffer_mask;
        s->max_distance = s->max_backward_distance;
        /* If we wrote past the logical end of the ringbuffer, copy the tail
           of the ringbuffer to its beginning and flush the ringbuffer to the
           output. */
        memcpy(s->ringbuffer, s->ringbuffer_end, (size_t)pos);
        s->substate_uncompressed = BROTLI_STATE_UNCOMPRESSED_COPY;
        /* No break, continue to next state */
      case BROTLI_STATE_UNCOMPRESSED_COPY:
        /* Copy straight from the input onto the ringbuffer. The ringbuffer will
           be flushed to the output at a later time. */
        nbytes = s->meta_block_remaining_len;
        if (pos + nbytes > s->ringbuffer_size) {
          nbytes = s->ringbuffer_size - pos;
        }
        num_read = BrotliRead(s->br.input_, &s->ringbuffer[pos],
                              (size_t)nbytes);
        pos += num_read;
        s->meta_block_remaining_len -= num_read;
        if (num_read < nbytes) {
          if (num_read < 0) return BROTLI_FAILURE();
          return BROTLI_RESULT_NEEDS_MORE_INPUT;
        }
        if (pos == s->ringbuffer_size) {
          s->to_write = s->ringbuffer_size;
          s->partially_written = 0;
          s->substate_uncompressed = BROTLI_STATE_UNCOMPRESSED_WRITE;
          break;
        }
        s->substate_uncompressed = BROTLI_STATE_UNCOMPRESSED_NONE;
        return BROTLI_RESULT_SUCCESS;
    }
  }
  return BROTLI_FAILURE();
}

int BrotliDecompressedSize(size_t encoded_size,
                           const uint8_t* encoded_buffer,
                           size_t* decoded_size) {
  BrotliMemInput memin;
  BrotliInput in = BrotliInitMemInput(encoded_buffer, encoded_size, &memin);
  BrotliBitReader br;
  BrotliState s;
  int next_block_header;
  int offset;
  BrotliStateInit(&s);
  BrotliInitBitReader(&br, in);
  if (!BrotliReadInput(&br, 1) || !BrotliWarmupBitReader(&br)) {
    return 0;
  }
  DecodeWindowBits(&br);
  if (DecodeMetaBlockLength(&s, &br) != BROTLI_RESULT_SUCCESS) {
    return 0;
  }
  *decoded_size = (size_t)s.meta_block_remaining_len;
  if (s.is_last_metablock) {
    return 1;
  }
  if (!s.is_uncompressed || !BrotliJumpToByteBoundary(&br)) {
    return 0;
  }
  next_block_header = BrotliPeekByte(&br, s.meta_block_remaining_len);
  if (next_block_header != -1) {
    return (next_block_header & 3) == 3;
  }
  /* Currently bit reader can't peek outside of its buffer... */
  offset = BROTLI_READ_SIZE - (int)BrotliGetRemainingBytes(&br);
  offset += s.meta_block_remaining_len;
  return (offset < encoded_size) && ((encoded_buffer[offset] & 3) == 3);
}

/* Allocates the smallest feasible ring buffer.

   If we know the data size is small, do not allocate more ringbuffer
   size than needed to reduce memory usage.

   This method is called before the first non-empty non-metadata block is
   processed. When this method is called, metablock size and flags MUST be
   decoded.
*/
int BROTLI_NOINLINE BrotliAllocateRingBuffer(BrotliState* s,
    BrotliBitReader* br) {
  static const int kRingBufferWriteAheadSlack = BROTLI_READ_SIZE;
  int is_last = s->is_last_metablock;
  s->ringbuffer_size = 1 << s->window_bits;

  if (s->is_uncompressed) {
    int next_block_header = BrotliPeekByte(br, s->meta_block_remaining_len);
    if (next_block_header != -1) { /* Peek succeeded */
      if ((next_block_header & 3) == 3) { /* ISLAST and ISEMPTY */
        is_last = 1;
      }
    }
  }

  /* We need at least 2 bytes of ring buffer size to get the last two
     bytes for context from there */
  if (is_last) {
    while (s->ringbuffer_size >= s->meta_block_remaining_len * 2
        && s->ringbuffer_size > 32) {
      s->ringbuffer_size >>= 1;
    }
  }

  /* But make it fit the custom dictionary if there is one. */
  while (s->ringbuffer_size < s->custom_dict_size) {
    s->ringbuffer_size <<= 1;
  }

  s->ringbuffer_mask = s->ringbuffer_size - 1;
  s->ringbuffer = (uint8_t*)malloc((size_t)(s->ringbuffer_size +
                                         kRingBufferWriteAheadSlack +
                                         kBrotliMaxDictionaryWordLength));
  if (!s->ringbuffer) {
    return 0;
  }
  s->ringbuffer_end = s->ringbuffer + s->ringbuffer_size;
  s->ringbuffer[s->ringbuffer_size - 2] = 0;
  s->ringbuffer[s->ringbuffer_size - 1] = 0;
  if (s->custom_dict) {
    memcpy(&s->ringbuffer[(-s->custom_dict_size) & s->ringbuffer_mask],
                          s->custom_dict, (size_t)s->custom_dict_size);
  }

  return 1;
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
  BrotliMemInput memin;
  BrotliInput in = BrotliInitMemInput(*next_in, *available_in, &memin);
  BrotliMemOutput memout;
  BrotliOutput out = BrotliInitMemOutput(*next_out, *available_out, &memout);
  BrotliResult result = BrotliDecompressStreaming(in, out, finish, s);
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
       - doing up to two 16-byte copies for fast backward copying
       - transforms
       - flushing the input s->ringbuffer when decoding uncompressed blocks */
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
        /* Prepare to the first read. */
        if (!BrotliWarmupBitReader(br)) {
          result = BROTLI_RESULT_NEEDS_MORE_INPUT;
          break;
        }
        /* Decode window size. */
        s->window_bits = DecodeWindowBits(br); /* Reads 1..7 bits. */
        BROTLI_LOG_UINT(s->window_bits);
        if (s->window_bits == 9) {
          /* Value 9 is reserved for future use. */
          result = BROTLI_FAILURE();
          break;
        }
        s->max_backward_distance = (1 << s->window_bits) - 16;
        s->max_backward_distance_minus_custom_dict_size =
            s->max_backward_distance - s->custom_dict_size;

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
        BrotliStateMetablockBegin(s);
        BROTLI_LOG_UINT(pos);
        s->state = BROTLI_STATE_METABLOCK_HEADER;
        /* No break, continue to next state */
      case BROTLI_STATE_METABLOCK_HEADER:
        result = DecodeMetaBlockLength(s, br); /* Reads 2 - 31 bits. */
        if (result != BROTLI_RESULT_SUCCESS) {
          i = s->loop_counter; /* Has been updated in DecodeMetaBlockLength. */
          break;
        }
        BROTLI_LOG_UINT(s->is_last_metablock);
        BROTLI_LOG_UINT(s->meta_block_remaining_len);
        BROTLI_LOG_UINT(s->is_metadata);
        BROTLI_LOG_UINT(s->is_uncompressed);
        if (s->is_metadata || s->is_uncompressed) {
          if (!BrotliJumpToByteBoundary(br)) {
            result = BROTLI_FAILURE();
            break;
          }
        }
        if (s->is_metadata) {
          s->state = BROTLI_STATE_METADATA;
          break;
        }
        if (s->meta_block_remaining_len == 0) {
          s->state = BROTLI_STATE_METABLOCK_DONE;
          break;
        }
        if (!s->ringbuffer) {
          if (!BrotliAllocateRingBuffer(s, br)) {
            result = BROTLI_FAILURE();
            break;
          }
        }
        if (s->is_uncompressed) {
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
          uint32_t bits;
          /* Read one byte and ignore it. */
          if (!BrotliSafeReadBits(br, 8, &bits)) {
            result = BROTLI_RESULT_NEEDS_MORE_INPUT;
            break;
          }
        }
        if (result == BROTLI_RESULT_SUCCESS) {
          s->state = BROTLI_STATE_METABLOCK_DONE;
        }
        break;
      case BROTLI_STATE_HUFFMAN_CODE_0:
        if (i >= 3) {
          s->state = BROTLI_STATE_CONTEXT_MODES;
          break;
        }
        /* Reads 1..11 bits. */
        result = DecodeVarLenUint8(s, br, &s->num_block_types[i]);
        if (result != BROTLI_RESULT_SUCCESS) {
          break;
        }
        s->num_block_types[i]++;
        BROTLI_LOG_UINT(s->num_block_types[i]);
        s->state = BROTLI_STATE_HUFFMAN_CODE_1;
        /* No break, continue to next state */
      case BROTLI_STATE_HUFFMAN_CODE_1:
        if (!BrotliWarmupBitReader(br)) {
          result = BROTLI_RESULT_NEEDS_MORE_INPUT;
          break;
        }
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
        s->state = BROTLI_STATE_HUFFMAN_CODE_3;
        /* No break, continue to next state */
      case BROTLI_STATE_HUFFMAN_CODE_3:
        if (!BrotliCheckInputAmount(br, 8)) {
          result = BROTLI_RESULT_NEEDS_MORE_INPUT;
          break;
        }
        s->block_length[i] = ReadBlockLength( /* Reads 3..39 bits. */
            &s->block_len_trees[i * BROTLI_HUFFMAN_MAX_TABLE_SIZE], br);
        BROTLI_LOG_UINT(s->block_length[i]);
        i++;
        s->state = BROTLI_STATE_HUFFMAN_CODE_0;
        break;
      case BROTLI_STATE_CONTEXT_MODES:
        /* We need up to 256 * 2 + 6 bits, this fits in 128 bytes. */
        if (!BrotliCheckInputAmount(br, 128)) {
          result = BROTLI_RESULT_NEEDS_MORE_INPUT;
          break;
        }
        s->distance_postfix_bits = (int)BrotliReadBits(br, 2);
        s->num_direct_distance_codes = NUM_DISTANCE_SHORT_CODES +
            ((int)BrotliReadBits(br, 4) << s->distance_postfix_bits);
        BROTLI_LOG_UINT(s->num_direct_distance_codes);
        BROTLI_LOG_UINT(s->distance_postfix_bits);
        s->distance_postfix_mask = (int)BitMask(s->distance_postfix_bits);
        s->context_modes = (uint8_t*)malloc((size_t)s->num_block_types[0]);
        if (s->context_modes == 0) {
          result = BROTLI_FAILURE();
          break;
        }
        for (i = 0; i < s->num_block_types[0]; ++i) {
          s->context_modes[i] = (uint8_t)(BrotliReadBits(br, 2) << 1);
          BROTLI_LOG_ARRAY_INDEX(s->context_modes, i);
        }
        s->state = BROTLI_STATE_CONTEXT_MAP_1;
        /* No break, continue to next state */
      case BROTLI_STATE_CONTEXT_MAP_1:
        result = DecodeContextMap(s->num_block_types[0] << kLiteralContextBits,
                                  &s->num_literal_htrees, &s->context_map, s);
        if (result != BROTLI_RESULT_SUCCESS) {
          break;
        }
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
        {
          int num_distance_codes =
              s->num_direct_distance_codes + (48 << s->distance_postfix_bits);
          result = DecodeContextMap(
              s->num_block_types[2] << kDistanceContextBits,
              &s->num_dist_htrees, &s->dist_context_map, s);
          if (result != BROTLI_RESULT_SUCCESS) {
            break;
          }
          BrotliHuffmanTreeGroupInit(
              &s->literal_hgroup, kNumLiteralCodes, s->num_literal_htrees);
          BrotliHuffmanTreeGroupInit(
              &s->insert_copy_hgroup, kNumInsertAndCopyCodes,
              s->num_block_types[1]);
          BrotliHuffmanTreeGroupInit(
              &s->distance_hgroup, num_distance_codes, s->num_dist_htrees);
        }
        i = 0;
        s->state = BROTLI_STATE_TREE_GROUP;
        /* No break, continue to next state */
      case BROTLI_STATE_TREE_GROUP:
        {
          HuffmanTreeGroup* hgroup = NULL;
          switch (i) {
            case 0:
              hgroup = &s->literal_hgroup;
              break;
            case 1:
              hgroup = &s->insert_copy_hgroup;
              break;
            case 2:
              hgroup = &s->distance_hgroup;
              break;
          }
          result = HuffmanTreeGroupDecode(hgroup, s);
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
          s->literal_htree = s->literal_hgroup.htrees[s->literal_htree_index];
          s->state = BROTLI_STATE_COMMAND_BEGIN;
        }
        break;
      case BROTLI_STATE_COMMAND_BEGIN:
        if (s->meta_block_remaining_len <= 0) {
          /* Next metablock, if any */
          s->state = BROTLI_STATE_METABLOCK_DONE;
          break;
        }
 /* Decoding of Brotli commands is the inner loop, jumping with goto makes it
    3% faster */
 CommandBegin:
        if (!BrotliCheckInputAmount(br, 32)) {
          s->state = BROTLI_STATE_COMMAND_BEGIN;
          result = BROTLI_RESULT_NEEDS_MORE_INPUT;
          break;
        }
          /* Read the insert/copy length in the command */
        if (s->block_length[1] == 0) {
          /* Block switch for insert/copy length. Reads 0..15 bits. */
          DecodeBlockType(s->num_block_types[1],
                          s->block_type_trees, 1,
                          s->block_type_rb, br);
          s->htree_command = s->insert_copy_hgroup.htrees[s->block_type_rb[3]];
          s->block_length[1] = ReadBlockLength( /* Reads 3..39 bits. */
              &s->block_len_trees[BROTLI_HUFFMAN_MAX_TABLE_SIZE], br);
        }
        {
          int cmd_code = ReadSymbol(s->htree_command, br);
          int insert_len_extra = 0;
          CmdLutElement v;
          --s->block_length[1];
          v = kCmdLut[cmd_code];
          s->distance_code = v.distance_code;
          s->distance_context = v.context;
          s->dist_htree_index = s->dist_context_map_slice[s->distance_context];
          i = v.insert_len_offset;
          if (PREDICT_FALSE(v.insert_len_extra_bits != 0)) {
            insert_len_extra = (int)BrotliReadBits(br, v.insert_len_extra_bits);
          }
          s->copy_length = (int)BrotliReadBits(br, v.copy_len_extra_bits) +
                           v.copy_len_offset;
          i += insert_len_extra;
        }
        BROTLI_LOG_UINT(i);
        BROTLI_LOG_UINT(s->copy_length);
        BROTLI_LOG_UINT(s->distance_code);
        if (i == 0) {
          goto postDecodeLiterals;
        }
        s->meta_block_remaining_len -= i;
        /* No break, go to next state */
      case BROTLI_STATE_COMMAND_INNER:
        /* Read the literals in the command */
        if (s->trivial_literal_context) {
          unsigned bits;
          unsigned value;
          PreloadSymbol(s->literal_htree, br, &bits, &value);
          do {
            if (!BrotliCheckInputAmount(br, 64)) {
              s->state = BROTLI_STATE_COMMAND_INNER;
              result = BROTLI_RESULT_NEEDS_MORE_INPUT;
              break;
            }
            if (PREDICT_FALSE(s->block_length[0] == 0)) {
              /* Block switch for literals */
              DecodeBlockTypeWithContext(s, br);
              PreloadSymbol(s->literal_htree, br, &bits, &value);
            }
            s->ringbuffer[pos] =
                (uint8_t)ReadPreloadedSymbol(s->literal_htree,
                                             br, &bits, &value);
            --s->block_length[0];
            BROTLI_LOG_UINT(s->literal_htree_index);
            BROTLI_LOG_ARRAY_INDEX(s->ringbuffer, pos);
            ++pos;
            if (PREDICT_FALSE(pos == s->ringbuffer_size)) {
              s->to_write = s->ringbuffer_size;
              s->partially_written = 0;
              s->state = BROTLI_STATE_COMMAND_INNER_WRITE;
              --i;
              goto innerWrite;
            }
          } while (--i != 0);
        } else {
          uint8_t p1 = s->ringbuffer[(pos - 1) & s->ringbuffer_mask];
          uint8_t p2 = s->ringbuffer[(pos - 2) & s->ringbuffer_mask];
          do {
            const HuffmanCode* hc;
            if (!BrotliCheckInputAmount(br, 64)) {
              s->state = BROTLI_STATE_COMMAND_INNER;
              result = BROTLI_RESULT_NEEDS_MORE_INPUT;
              break;
            }
            if (PREDICT_FALSE(s->block_length[0] == 0)) {
              /* Block switch for literals */
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
              s->state = BROTLI_STATE_COMMAND_INNER_WRITE;
              --i;
              goto innerWrite;
            }
          } while (--i != 0);
        }
        if (result != BROTLI_RESULT_SUCCESS) break;
        if (s->meta_block_remaining_len <= 0) {
          s->state = BROTLI_STATE_METABLOCK_DONE;
          break;
        }
postDecodeLiterals:
        if (s->distance_code >= 0) {
          --s->dist_rb_idx;
          s->distance_code = s->dist_rb[s->dist_rb_idx & 3];
          goto postReadDistance;  /* We already have the implicit distance */
        }
        /* Read distance code in the command, unless it was implicitly zero. */
        BROTLI_DCHECK(s->distance_code < 0);
        if (s->block_length[2] == 0) {
          /* Block switch for distance codes */
          int dist_context_offset;
          DecodeBlockType(s->num_block_types[2],
                          s->block_type_trees, 2,
                          s->block_type_rb, br); /* Reads 0..15 bits. */
          s->block_length[2] = ReadBlockLength( /* Reads 3..39 bits. */
              &s->block_len_trees[2 * BROTLI_HUFFMAN_MAX_TABLE_SIZE], br);
          dist_context_offset = s->block_type_rb[5] << kDistanceContextBits;
          s->dist_context_map_slice =
              s->dist_context_map + dist_context_offset;
          s->dist_htree_index = s->dist_context_map_slice[s->distance_context];
        }
        --s->block_length[2];
        s->distance_code =
            ReadSymbol(s->distance_hgroup.htrees[s->dist_htree_index], br);
        /* Convert the distance code to the actual distance by possibly */
        /* looking up past distances from the s->ringbuffer. */
        if ((s->distance_code & ~0xf) == 0) {
          if (s->distance_code == 0) {
            --s->dist_rb_idx;
            s->distance_code = s->dist_rb[s->dist_rb_idx & 3];
          } else {
            int distance_code = s->distance_code << 1;
            /* kDistanceShortCodeIndexOffset has 2-bit values from LSB: */
            /* 3, 2, 1, 0, 3, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2 */
            const uint32_t kDistanceShortCodeIndexOffset = 0xaaafff1b;
            /* kDistanceShortCodeValueOffset has 2-bit values from LSB: */
            /* 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 1, 1, 2, 2, 3, 3 */
            const uint32_t kDistanceShortCodeValueOffset = 0xfa5fa500;
            int v = (s->dist_rb_idx +
                (int)(kDistanceShortCodeIndexOffset >> distance_code)) & 0x3;
            s->distance_code = s->dist_rb[v];
            v = (int)(kDistanceShortCodeValueOffset >> distance_code) & 0x3;
            if ((distance_code & 0x3) != 0) {
              s->distance_code += v;
            } else {
              s->distance_code -= v;
              if (s->distance_code <= 0) {
                /* A huge distance will cause a BROTLI_FAILURE() soon. */
                /* This is a little faster than failing here. */
                s->distance_code = 0x0fffffff;
              }
            }
          }
        } else {
          int distval = s->distance_code - s->num_direct_distance_codes;
          if (distval >= 0) {
            int nbits;
            int postfix;
            int offset;
            if (s->distance_postfix_bits == 0) {
              nbits = (distval >> 1) + 1;
              offset = ((2 + (distval & 1)) << nbits) - 4;
              s->distance_code = s->num_direct_distance_codes +
                  offset + (int)BrotliReadBits(br, nbits);
            } else {
              postfix = distval & s->distance_postfix_mask;
              distval >>= s->distance_postfix_bits;
              nbits = (distval >> 1) + 1;
              offset = ((2 + (distval & 1)) << nbits) - 4;
              s->distance_code = s->num_direct_distance_codes +
                  ((offset + (int)BrotliReadBits(br, nbits)) <<
                   s->distance_postfix_bits) + postfix;
            }
          }
          s->distance_code = s->distance_code - NUM_DISTANCE_SHORT_CODES + 1;
        }
postReadDistance:
        BROTLI_LOG_UINT(s->distance_code);
        if (s->max_distance != s->max_backward_distance) {
          if (pos < s->max_backward_distance_minus_custom_dict_size) {
            s->max_distance = pos + s->custom_dict_size;
          } else {
            s->max_distance = s->max_backward_distance;
          }
        }
        i = s->copy_length;
        /* Apply copy of LZ77 back-reference, or static dictionary reference if
        the distance is larger than the max LZ77 distance */
        if (s->distance_code > s->max_distance) {
          if (i >= kBrotliMinDictionaryWordLength &&
              i <= kBrotliMaxDictionaryWordLength) {
            int offset = kBrotliDictionaryOffsetsByLength[i];
            int word_id = s->distance_code - s->max_distance - 1;
            int shift = kBrotliDictionarySizeBitsByLength[i];
            int mask = (int)BitMask(shift);
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
                s->state = BROTLI_STATE_COMMAND_POST_WRITE_1;
                break;
              }
            } else {
              BROTLI_LOG(("Invalid backward reference. pos: %d distance: %d "
                     "len: %d bytes left: %d\n",
                  pos, s->distance_code, i,
                  s->meta_block_remaining_len));
              result = BROTLI_FAILURE();
              break;
            }
          } else {
            BROTLI_LOG(("Invalid backward reference. pos: %d distance: %d "
                   "len: %d bytes left: %d\n", pos, s->distance_code, i,
                   s->meta_block_remaining_len));
            result = BROTLI_FAILURE();
            break;
          }
        } else {
          const uint8_t *ringbuffer_end_minus_copy_length =
              s->ringbuffer_end - i;
          copy_src = &s->ringbuffer[(pos - s->distance_code) &
                                    s->ringbuffer_mask];
          copy_dst = &s->ringbuffer[pos];
          /* update the recent distances cache */
          s->dist_rb[s->dist_rb_idx & 3] = s->distance_code;
          ++s->dist_rb_idx;
          s->meta_block_remaining_len -= i;
          if (PREDICT_FALSE(s->meta_block_remaining_len < 0)) {
            BROTLI_LOG(("Invalid backward reference. pos: %d distance: %d "
                   "len: %d bytes left: %d\n", pos, s->distance_code, i,
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
        if (s->meta_block_remaining_len <= 0) {
          /* Next metablock, if any */
          s->state = BROTLI_STATE_METABLOCK_DONE;
          break;
        } else {
          goto CommandBegin;
        }
      postSelfintersecting:
        while (--i >= 0) {
          s->ringbuffer[pos] =
              s->ringbuffer[(pos - s->distance_code) & s->ringbuffer_mask];
          ++pos;
        }
        if (s->meta_block_remaining_len <= 0) {
          /* Next metablock, if any */
          s->state = BROTLI_STATE_METABLOCK_DONE;
          break;
        } else {
          goto CommandBegin;
        }
      postWrapCopy:
        s->state = BROTLI_STATE_COMMAND_POST_WRAP_COPY;
        /* No break, go to next state */
      case BROTLI_STATE_COMMAND_POST_WRAP_COPY:
        while (--i >= 0) {
          s->ringbuffer[pos] =
              s->ringbuffer[(pos - s->distance_code) & s->ringbuffer_mask];
          ++pos;
          if (pos == s->ringbuffer_size) {
            s->to_write = s->ringbuffer_size;
            s->partially_written = 0;
            s->state = BROTLI_STATE_COMMAND_POST_WRITE_2;
            break;
          }
        }
        if (s->state == BROTLI_STATE_COMMAND_POST_WRAP_COPY) {
          if (s->meta_block_remaining_len <= 0) {
            /* Next metablock, if any */
            s->state = BROTLI_STATE_METABLOCK_DONE;
            break;
          } else {
            goto CommandBegin;
          }
        }
        break;
      case BROTLI_STATE_COMMAND_INNER_WRITE:
      case BROTLI_STATE_COMMAND_POST_WRITE_1:
      case BROTLI_STATE_COMMAND_POST_WRITE_2:
innerWrite:
        result = WriteRingBuffer(output, s);
        if (result != BROTLI_RESULT_SUCCESS) {
          break;
        }
        pos -= s->ringbuffer_size;
        s->max_distance = s->max_backward_distance;
        if (s->state == BROTLI_STATE_COMMAND_POST_WRITE_1) {
          memcpy(s->ringbuffer, s->ringbuffer_end, (size_t)pos);
          if (s->meta_block_remaining_len <= 0) {
            /* Next metablock, if any */
            s->state = BROTLI_STATE_METABLOCK_DONE;
            break;
          } else {
            goto CommandBegin;
          }
        } else if (s->state == BROTLI_STATE_COMMAND_POST_WRITE_2) {
          s->state = BROTLI_STATE_COMMAND_POST_WRAP_COPY;
        } else {  /* BROTLI_STATE_COMMAND_INNER_WRITE */
          if (i == 0) {
            if (s->meta_block_remaining_len <= 0) {
              s->state = BROTLI_STATE_METABLOCK_DONE;
              break;
            }
            goto postDecodeLiterals;
          }
          s->state = BROTLI_STATE_COMMAND_INNER;
        }
        break;
      case BROTLI_STATE_METABLOCK_DONE:
        BrotliStateCleanupAfterMetablock(s);
        if (!s->is_last_metablock) {
          s->state = BROTLI_STATE_METABLOCK_BEGIN;
          break;
        }
        s->to_write = pos;
        s->partially_written = 0;
        s->state = BROTLI_STATE_DONE;
        /* No break, continue to next state */
      case BROTLI_STATE_DONE:
        if (s->ringbuffer != 0) {
          result = WriteRingBuffer(output, s);
          if (result != BROTLI_RESULT_SUCCESS) {
            break;
          }
        }
        if (!BrotliJumpToByteBoundary(br)) {
          result = BROTLI_FAILURE();
        }
        if (!BrotliIsBitReaderOK(br)) {
          /* The brotli input stream was too small, does not follow the spec.
             NOTE: larger input is allowed, smaller not. */
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
