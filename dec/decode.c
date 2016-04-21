/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

#include "./decode.h"

#ifdef __ARM_NEON__
#include <arm_neon.h>
#endif

#include <stdlib.h>  /* free, malloc */
#include <string.h>  /* memcpy, memset */

#include "./bit_reader.h"
#include "./context.h"
#include "./dictionary.h"
#include "./huffman.h"
#include "./port.h"
#include "./prefix.h"
#include "./state.h"
#include "./transform.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#define BROTLI_FAILURE() (BROTLI_DUMP(), BROTLI_RESULT_ERROR)

#define BROTLI_LOG_UINT(name)                                       \
  BROTLI_LOG(("[%s] %s = %lu\n", __func__, #name, (unsigned long)(name)))
#define BROTLI_LOG_ARRAY_INDEX(array_name, idx)                     \
  BROTLI_LOG(("[%s] %s[%lu] = %lu\n", __func__, #array_name,        \
         (unsigned long)(idx), (unsigned long)array_name[idx]))

static const uint32_t kDefaultCodeLength = 8;
static const uint32_t kCodeLengthRepeatCode = 16;
static const uint32_t kNumLiteralCodes = 256;
static const uint32_t kNumInsertAndCopyCodes = 704;
static const uint32_t kNumBlockLengthCodes = 26;
static const int kLiteralContextBits = 6;
static const int kDistanceContextBits = 2;

#define HUFFMAN_TABLE_BITS 8U
#define HUFFMAN_TABLE_MASK 0xff

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

BrotliState* BrotliCreateState(
    brotli_alloc_func alloc_func, brotli_free_func free_func, void* opaque) {
  BrotliState* state = 0;
  if (!alloc_func && !free_func) {
    state = (BrotliState*)malloc(sizeof(BrotliState));
  } else if (alloc_func && free_func) {
    state = (BrotliState*)alloc_func(opaque, sizeof(BrotliState));
  }
  if (state == 0) {
    BROTLI_DUMP();
    return 0;
  }
  BrotliStateInitWithCustomAllocators(state, alloc_func, free_func, opaque);
  return state;
}

/* Deinitializes and frees BrotliState instance. */
void BrotliDestroyState(BrotliState* state) {
  if (!state) {
    return;
  } else {
    brotli_free_func free_func = state->free_func;
    void* opaque = state->memory_manager_opaque;
    BrotliStateCleanup(state);
    free_func(opaque, state);
  }
}

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

static BROTLI_INLINE void memmove16(uint8_t* dst, uint8_t* src) {
#if defined(__ARM_NEON__)
  vst1q_u8(dst, vld1q_u8(src));
#else
  uint32_t buffer[4];
  memcpy(buffer, src, 16);
  memcpy(dst, buffer, 16);
#endif
}

/* Decodes a number in the range [0..255], by reading 1 - 11 bits. */
static BROTLI_NOINLINE BrotliResult DecodeVarLenUint8(BrotliState* s,
    BrotliBitReader* br, uint32_t* value) {
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
      *value = bits;
      /* No break, transit to the next state. */

    case BROTLI_STATE_DECODE_UINT8_LONG:
      if (PREDICT_FALSE(!BrotliSafeReadBits(br, *value, &bits))) {
        s->substate_decode_uint8 = BROTLI_STATE_DECODE_UINT8_LONG;
        return BROTLI_RESULT_NEEDS_MORE_INPUT;
      }
      *value = (1U << *value) + bits;
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
        if (!s->is_last_metablock) {
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
        ++s->meta_block_remaining_len;
        s->substate_metablock_header = BROTLI_STATE_METABLOCK_HEADER_NONE;
        return BROTLI_RESULT_SUCCESS;

      default:
        return BROTLI_FAILURE();
    }
  }
}

/* Decodes the Huffman code.
   This method doesn't read data from the bit reader, BUT drops the amount of
   bits that correspond to the decoded symbol.
   bits MUST contain at least 15 (BROTLI_HUFFMAN_MAX_CODE_LENGTH) valid bits. */
static BROTLI_INLINE uint32_t DecodeSymbol(uint32_t bits,
                                           const HuffmanCode* table,
                                           BrotliBitReader* br) {
  table += bits & HUFFMAN_TABLE_MASK;
  if (table->bits > HUFFMAN_TABLE_BITS) {
    uint32_t nbits = table->bits - HUFFMAN_TABLE_BITS;
    BrotliDropBits(br, HUFFMAN_TABLE_BITS);
    table += table->value;
    table += (bits >> HUFFMAN_TABLE_BITS) & BitMask(nbits);
  }
  BrotliDropBits(br, table->bits);
  return table->value;
}

/* Reads and decodes the next Huffman code from bit-stream.
   This method peeks 16 bits of input and drops 0 - 15 of them. */
static BROTLI_INLINE uint32_t ReadSymbol(const HuffmanCode* table,
                                         BrotliBitReader* br) {
  return DecodeSymbol(BrotliGet16BitsUnmasked(br), table, br);
}

/* Same as DecodeSymbol, but it is known that there is less than 15 bits of
   input are currently available. */
static BROTLI_NOINLINE int SafeDecodeSymbol(const HuffmanCode* table,
                                            BrotliBitReader* br,
                                            uint32_t* result) {
  uint32_t val;
  uint32_t available_bits = BrotliGetAvailableBits(br);
  if (available_bits == 0) {
    if (table->bits == 0) {
      *result = table->value;
      return 1;
    }
    return 0; /* No valid bits at all. */
  }
  val = (uint32_t)BrotliGetBitsUnmasked(br);
  table += val & HUFFMAN_TABLE_MASK;
  if (table->bits <= HUFFMAN_TABLE_BITS) {
    if (table->bits <= available_bits) {
      BrotliDropBits(br, table->bits);
      *result = table->value;
      return 1;
    } else {
      return 0; /* Not enough bits for the first level. */
    }
  }
  if (available_bits <= HUFFMAN_TABLE_BITS) {
    return 0; /* Not enough bits to move to the second level. */
  }

  /* Speculatively drop HUFFMAN_TABLE_BITS. */
  val = (val & BitMask(table->bits)) >> HUFFMAN_TABLE_BITS;
  available_bits -= HUFFMAN_TABLE_BITS;
  table += table->value + val;
  if (available_bits < table->bits) {
    return 0; /* Not enough bits for the second level. */
  }

  BrotliDropBits(br, HUFFMAN_TABLE_BITS + table->bits);
  *result = table->value;
  return 1;
}

static BROTLI_INLINE int SafeReadSymbol(const HuffmanCode* table,
                                        BrotliBitReader* br,
                                        uint32_t* result) {
  uint32_t val;
  if (PREDICT_TRUE(BrotliSafeGetBits(br, 15, &val))) {
    *result = DecodeSymbol(val, table, br);
    return 1;
  }
  return SafeDecodeSymbol(table, br, result);
}

/* Makes a look-up in first level Huffman table. Peeks 8 bits. */
static BROTLI_INLINE void PreloadSymbol(int safe,
                                        const HuffmanCode* table,
                                        BrotliBitReader* br,
                                        uint32_t* bits,
                                        uint32_t* value) {
  if (safe) {
    return;
  }
  table += BrotliGetBits(br, HUFFMAN_TABLE_BITS);
  *bits = table->bits;
  *value = table->value;
}

/* Decodes the next Huffman code using data prepared by PreloadSymbol.
   Reads 0 - 15 bits. Also peeks 8 following bits. */
static BROTLI_INLINE uint32_t ReadPreloadedSymbol(const HuffmanCode* table,
                                                  BrotliBitReader* br,
                                                  uint32_t* bits,
                                                  uint32_t* value) {
  uint32_t result = *value;
  if (PREDICT_FALSE(*bits > HUFFMAN_TABLE_BITS)) {
    uint32_t val = BrotliGet16BitsUnmasked(br);
    const HuffmanCode* ext = table + (val & HUFFMAN_TABLE_MASK) + *value;
    uint32_t mask = BitMask((*bits - HUFFMAN_TABLE_BITS));
    BrotliDropBits(br, HUFFMAN_TABLE_BITS);
    ext += (val >> HUFFMAN_TABLE_BITS) & mask;
    BrotliDropBits(br, ext->bits);
    result = ext->value;
  } else {
    BrotliDropBits(br, *bits);
  }
  PreloadSymbol(0, table, br, bits, value);
  return result;
}

static BROTLI_INLINE uint32_t Log2Floor(uint32_t x) {
  uint32_t result = 0;
  while (x) {
    x >>= 1;
    ++result;
  }
  return result;
}

/* Reads (s->symbol + 1) symbols.
   Totally 1..4 symbols are read, 1..10 bits each.
   The list of symbols MUST NOT contain duplicates.
 */
static BrotliResult ReadSimpleHuffmanSymbols(uint32_t alphabet_size,
                                             BrotliState* s) {
  /* max_bits == 1..10; symbol == 0..3; 1..40 bits will be read. */
  BrotliBitReader* br = &s->br;
  uint32_t max_bits = Log2Floor(alphabet_size - 1);
  uint32_t i = s->sub_loop_counter;
  uint32_t num_symbols = s->symbol;
  while (i <= num_symbols) {
    uint32_t v;
    if (PREDICT_FALSE(!BrotliSafeReadBits(br, max_bits, &v))) {
      s->sub_loop_counter = i;
      s->substate_huffman = BROTLI_STATE_HUFFMAN_SIMPLE_READ;
      return BROTLI_RESULT_NEEDS_MORE_INPUT;
    }
    if (v >= alphabet_size) {
      return BROTLI_FAILURE();
    }
    s->symbols_lists_array[i] = (uint16_t)v;
    BROTLI_LOG_UINT(s->symbols_lists_array[i]);
    ++i;
  }

  for (i = 0; i < num_symbols; ++i) {
    uint32_t k = i + 1;
    for (; k <= num_symbols; ++k) {
      if (s->symbols_lists_array[i] == s->symbols_lists_array[k]) {
        return BROTLI_FAILURE();
      }
    }
  }

  return BROTLI_RESULT_SUCCESS;
}

/* Process single decoded symbol code length:
    A) reset the repeat variable
    B) remember code length (if it is not 0)
    C) extend corredponding index-chain
    D) reduce the huffman space
    E) update the histogram
 */
static BROTLI_INLINE void ProcessSingleCodeLength(uint32_t code_len,
    uint32_t* symbol, uint32_t* repeat, uint32_t* space,
    uint32_t* prev_code_len, uint16_t* symbol_lists,
    uint16_t* code_length_histo, int* next_symbol) {
  *repeat = 0;
  if (code_len != 0) { /* code_len == 1..15 */
    symbol_lists[next_symbol[code_len]] = (uint16_t)(*symbol);
    next_symbol[code_len] = (int)(*symbol);
    *prev_code_len = code_len;
    *space -= 32768U >> code_len;
    code_length_histo[code_len]++;
    BROTLI_LOG(("[ReadHuffmanCode] code_length[%d] = %d\n", *symbol, code_len));
  }
  (*symbol)++;
}

/* Process repeated symbol code length.
    A) Check if it is the extension of previous repeat sequence; if the decoded
       value is not kCodeLengthRepeatCode, then it is a new symbol-skip
    B) Update repeat variable
    C) Check if operation is feasible (fits alphapet)
    D) For each symbol do the same operations as in ProcessSingleCodeLength

   PRECONDITION: code_len == kCodeLengthRepeatCode or kCodeLengthRepeatCode + 1
 */
static BROTLI_INLINE void ProcessRepeatedCodeLength(uint32_t code_len,
    uint32_t repeat_delta, uint32_t alphabet_size, uint32_t* symbol,
    uint32_t* repeat, uint32_t* space, uint32_t* prev_code_len,
    uint32_t* repeat_code_len, uint16_t* symbol_lists,
    uint16_t* code_length_histo, int* next_symbol) {
  uint32_t old_repeat;
  uint32_t new_len = 0;
  if (code_len == kCodeLengthRepeatCode) {
    new_len = *prev_code_len;
  }
  if (*repeat_code_len != new_len) {
    *repeat = 0;
    *repeat_code_len = new_len;
  }
  old_repeat = *repeat;
  if (*repeat > 0) {
    *repeat -= 2;
    *repeat <<= code_len - 14U;
  }
  *repeat += repeat_delta + 3U;
  repeat_delta = *repeat - old_repeat;
  if (*symbol + repeat_delta > alphabet_size) {
    BROTLI_DUMP();
    *symbol = alphabet_size;
    *space = 0xFFFFF;
    return;
  }
  BROTLI_LOG(("[ReadHuffmanCode] code_length[%d..%d] = %d\n",
              *symbol, *symbol + repeat_delta - 1, *repeat_code_len));
  if (*repeat_code_len != 0) {
    unsigned last = *symbol + repeat_delta;
    int next = next_symbol[*repeat_code_len];
    do {
      symbol_lists[next] = (uint16_t)*symbol;
      next = (int)*symbol;
    } while (++(*symbol) != last);
    next_symbol[*repeat_code_len] = next;
    *space -= repeat_delta << (15 - *repeat_code_len);
    code_length_histo[*repeat_code_len] =
        (uint16_t)(code_length_histo[*repeat_code_len] + repeat_delta);
  } else {
    *symbol += repeat_delta;
  }
}

/* Reads and decodes symbol codelengths. */
static BrotliResult ReadSymbolCodeLengths(
    uint32_t alphabet_size, BrotliState* s) {
  BrotliBitReader* br = &s->br;
  uint32_t symbol = s->symbol;
  uint32_t repeat = s->repeat;
  uint32_t space = s->space;
  uint32_t prev_code_len = s->prev_code_len;
  uint32_t repeat_code_len = s->repeat_code_len;
  uint16_t* symbol_lists = s->symbol_lists;
  uint16_t* code_length_histo = s->code_length_histo;
  int* next_symbol = s->next_symbol;
  if (!BrotliWarmupBitReader(br)) {
    return BROTLI_RESULT_NEEDS_MORE_INPUT;
  }
  while (symbol < alphabet_size && space > 0) {
    const HuffmanCode* p = s->table;
    uint32_t code_len;
    if (!BrotliCheckInputAmount(br, BROTLI_SHORT_FILL_BIT_WINDOW_READ)) {
      s->symbol = symbol;
      s->repeat = repeat;
      s->prev_code_len = prev_code_len;
      s->repeat_code_len = repeat_code_len;
      s->space = space;
      return BROTLI_RESULT_NEEDS_MORE_INPUT;
    }
    BrotliFillBitWindow16(br);
    p += BrotliGetBitsUnmasked(br) &
        BitMask(BROTLI_HUFFMAN_MAX_CODE_LENGTH_CODE_LENGTH);
    BrotliDropBits(br, p->bits);  /* Use 1..5 bits */
    code_len = p->value;  /* code_len == 0..17 */
    if (code_len < kCodeLengthRepeatCode) {
      ProcessSingleCodeLength(code_len, &symbol, &repeat, &space,
          &prev_code_len, symbol_lists, code_length_histo, next_symbol);
    } else { /* code_len == 16..17, extra_bits == 2..3 */
      uint32_t repeat_delta =
          (uint32_t)BrotliGetBitsUnmasked(br) & BitMask(code_len - 14U);
      BrotliDropBits(br, code_len - 14U);
      ProcessRepeatedCodeLength(code_len, repeat_delta, alphabet_size,
          &symbol, &repeat, &space, &prev_code_len, &repeat_code_len,
          symbol_lists, code_length_histo, next_symbol);
    }
  }
  s->space = space;
  return BROTLI_RESULT_SUCCESS;
}

static BrotliResult SafeReadSymbolCodeLengths(
    uint32_t alphabet_size, BrotliState* s) {
  BrotliBitReader* br = &s->br;
  while (s->symbol < alphabet_size && s->space > 0) {
    const HuffmanCode* p = s->table;
    uint32_t code_len;
    uint32_t bits = 0;
    uint32_t available_bits = BrotliGetAvailableBits(br);
    if (available_bits != 0) {
      bits = (uint32_t)BrotliGetBitsUnmasked(br);
    }
    p += bits & BitMask(BROTLI_HUFFMAN_MAX_CODE_LENGTH_CODE_LENGTH);
    if (p->bits > available_bits) goto pullMoreInput;
    code_len = p->value; /* code_len == 0..17 */
    if (code_len < kCodeLengthRepeatCode) {
      BrotliDropBits(br, p->bits);
      ProcessSingleCodeLength(code_len, &s->symbol, &s->repeat, &s->space,
          &s->prev_code_len, s->symbol_lists, s->code_length_histo,
          s->next_symbol);
    } else { /* code_len == 16..17, extra_bits == 2..3 */
      uint32_t extra_bits = code_len - 14U;
      uint32_t repeat_delta = (bits >> p->bits) & BitMask(extra_bits);
      if (available_bits < p->bits + extra_bits) goto pullMoreInput;
      BrotliDropBits(br, p->bits + extra_bits);
      ProcessRepeatedCodeLength(code_len, repeat_delta, alphabet_size,
          &s->symbol, &s->repeat, &s->space, &s->prev_code_len,
          &s->repeat_code_len, s->symbol_lists, s->code_length_histo,
          s->next_symbol);
    }
    continue;

pullMoreInput:
    if (!BrotliPullByte(br)) {
      return BROTLI_RESULT_NEEDS_MORE_INPUT;
    }
  }
  return BROTLI_RESULT_SUCCESS;
}

/* Reads and decodes 15..18 codes using static prefix code.
   Each code is 2..4 bits long. In total 30..72 bits are used. */
static BrotliResult ReadCodeLengthCodeLengths(BrotliState* s) {
  BrotliBitReader* br = &s->br;
  uint32_t num_codes = s->repeat;
  unsigned space = s->space;
  uint32_t i = s->sub_loop_counter;
  for (; i < CODE_LENGTH_CODES; ++i) {
    const uint8_t code_len_idx = kCodeLengthCodeOrder[i];
    uint32_t ix;
    uint32_t v;
    if (PREDICT_FALSE(!BrotliSafeGetBits(br, 4, &ix))) {
      uint32_t available_bits = BrotliGetAvailableBits(br);
      if (available_bits != 0) {
        ix = BrotliGetBitsUnmasked(br) & 0xF;
      } else {
        ix = 0;
      }
      if (kCodeLengthPrefixLength[ix] > available_bits) {
        s->sub_loop_counter = i;
        s->repeat = num_codes;
        s->space = space;
        s->substate_huffman = BROTLI_STATE_HUFFMAN_COMPLEX;
        return BROTLI_RESULT_NEEDS_MORE_INPUT;
      }
    }
    v = kCodeLengthPrefixValue[ix];
    BrotliDropBits(br, kCodeLengthPrefixLength[ix]);
    s->code_length_code_lengths[code_len_idx] = (uint8_t)v;
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
  return BROTLI_RESULT_SUCCESS;
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
static BrotliResult ReadHuffmanCode(uint32_t alphabet_size,
                                    HuffmanCode* table,
                                    uint32_t* opt_table_size,
                                    BrotliState* s) {
  BrotliBitReader* br = &s->br;
  /* Unnecessary masking, but might be good for safety. */
  alphabet_size &= 0x3ff;
  /* State machine */
  switch (s->substate_huffman) {
    case BROTLI_STATE_HUFFMAN_NONE:
      if (!BrotliSafeReadBits(br, 2, &s->sub_loop_counter)) {
        return BROTLI_RESULT_NEEDS_MORE_INPUT;
      }
      BROTLI_LOG_UINT(s->sub_loop_counter);
      /* The value is used as follows:
         1 for simple code;
         0 for no skipping, 2 skips 2 code lengths, 3 skips 3 code lengths */
      if (s->sub_loop_counter != 1) {
        s->space = 32;
        s->repeat = 0; /* num_codes */
        memset(&s->code_length_histo[0], 0, sizeof(s->code_length_histo[0]) *
            (BROTLI_HUFFMAN_MAX_CODE_LENGTH_CODE_LENGTH + 1));
        memset(&s->code_length_code_lengths[0], 0,
            sizeof(s->code_length_code_lengths));
        s->substate_huffman = BROTLI_STATE_HUFFMAN_COMPLEX;
        goto Complex;
      }
      /* No break, transit to the next state. */

    case BROTLI_STATE_HUFFMAN_SIMPLE_SIZE:
      /* Read symbols, codes & code lengths directly. */
      if (!BrotliSafeReadBits(br, 2, &s->symbol)) { /* num_symbols */
        s->substate_huffman = BROTLI_STATE_HUFFMAN_SIMPLE_SIZE;
        return BROTLI_RESULT_NEEDS_MORE_INPUT;
      }
      s->sub_loop_counter = 0;
      /* No break, transit to the next state. */
    case BROTLI_STATE_HUFFMAN_SIMPLE_READ: {
      BrotliResult result = ReadSimpleHuffmanSymbols(alphabet_size, s);
      if (result != BROTLI_RESULT_SUCCESS) {
        return result;
      }
      /* No break, transit to the next state. */
    }
    case BROTLI_STATE_HUFFMAN_SIMPLE_BUILD: {
      uint32_t table_size;
      if (s->symbol == 3) {
        uint32_t bits;
        if (!BrotliSafeReadBits(br, 1, &bits)) {
          s->substate_huffman = BROTLI_STATE_HUFFMAN_SIMPLE_BUILD;
          return BROTLI_RESULT_NEEDS_MORE_INPUT;
        }
        s->symbol += bits;
      }
      BROTLI_LOG_UINT(s->symbol);
      table_size = BrotliBuildSimpleHuffmanTable(
          table, HUFFMAN_TABLE_BITS, s->symbols_lists_array, s->symbol);
      if (opt_table_size) {
        *opt_table_size = table_size;
      }
      s->substate_huffman = BROTLI_STATE_HUFFMAN_NONE;
      return BROTLI_RESULT_SUCCESS;
    }

Complex: /* Decode Huffman-coded code lengths. */
    case BROTLI_STATE_HUFFMAN_COMPLEX: {
      uint32_t i;
      BrotliResult result = ReadCodeLengthCodeLengths(s);
      if (result != BROTLI_RESULT_SUCCESS) {
        return result;
      }
      BrotliBuildCodeLengthsHuffmanTable(s->table,
                                         s->code_length_code_lengths,
                                         s->code_length_histo);
      memset(&s->code_length_histo[0], 0, sizeof(s->code_length_histo));
      for (i = 0; i <= BROTLI_HUFFMAN_MAX_CODE_LENGTH; ++i) {
        s->next_symbol[i] = (int)i - (BROTLI_HUFFMAN_MAX_CODE_LENGTH + 1);
        s->symbol_lists[(int)i - (BROTLI_HUFFMAN_MAX_CODE_LENGTH + 1)] = 0xFFFF;
      }

      s->symbol = 0;
      s->prev_code_len = kDefaultCodeLength;
      s->repeat = 0;
      s->repeat_code_len = 0;
      s->space = 32768;
      s->substate_huffman = BROTLI_STATE_HUFFMAN_LENGTH_SYMBOLS;
      /* No break, transit to the next state. */
    }
    case BROTLI_STATE_HUFFMAN_LENGTH_SYMBOLS: {
      uint32_t table_size;
      BrotliResult result = ReadSymbolCodeLengths(alphabet_size, s);
      if (result == BROTLI_RESULT_NEEDS_MORE_INPUT) {
        result = SafeReadSymbolCodeLengths(alphabet_size, s);
      }
      if (result != BROTLI_RESULT_SUCCESS) {
        return result;
      }

      if (s->space != 0) {
        BROTLI_LOG(("[ReadHuffmanCode] space = %d\n", s->space));
        return BROTLI_FAILURE();
      }
      table_size = BrotliBuildHuffmanTable(
          table, HUFFMAN_TABLE_BITS, s->symbol_lists, s->code_length_histo);
      if (opt_table_size) {
        *opt_table_size = table_size;
      }
      s->substate_huffman = BROTLI_STATE_HUFFMAN_NONE;
      return BROTLI_RESULT_SUCCESS;
    }

    default:
      return BROTLI_FAILURE();
  }
}

/* Decodes a block length by reading 3..39 bits. */
static BROTLI_INLINE uint32_t ReadBlockLength(const HuffmanCode* table,
                                              BrotliBitReader* br) {
  uint32_t code;
  uint32_t nbits;
  code = ReadSymbol(table, br);
  nbits = kBlockLengthPrefixCode[code].nbits; /* nbits == 2..24 */
  return kBlockLengthPrefixCode[code].offset + BrotliReadBits(br, nbits);
}

/* WARNING: if state is not BROTLI_STATE_READ_BLOCK_LENGTH_NONE, then
   reading can't be continued with ReadBlockLength. */
static BROTLI_INLINE int SafeReadBlockLength(BrotliState* s,
                                             uint32_t* result,
                                             const HuffmanCode* table,
                                             BrotliBitReader* br) {
  uint32_t index;
  if (s->substate_read_block_length == BROTLI_STATE_READ_BLOCK_LENGTH_NONE) {
    if (!SafeReadSymbol(table, br, &index)) {
      return 0;
    }
  } else {
    index = s->block_length_index;
  }
  {
    uint32_t bits;
    uint32_t nbits = kBlockLengthPrefixCode[index].nbits; /* nbits == 2..24 */
    if (!BrotliSafeReadBits(br, nbits, &bits)) {
      s->block_length_index = index;
      s->substate_read_block_length = BROTLI_STATE_READ_BLOCK_LENGTH_SUFFIX;
      return 0;
    }
    *result = kBlockLengthPrefixCode[index].offset + bits;
    s->substate_read_block_length = BROTLI_STATE_READ_BLOCK_LENGTH_NONE;
    return 1;
  }
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
static BROTLI_NOINLINE void InverseMoveToFrontTransform(uint8_t* v,
    uint32_t v_len, BrotliState* state) {
  /* Reinitialize elements that could have been changed. */
  uint32_t i = 4;
  uint32_t upper_bound = state->mtf_upper_bound;
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
    upper_bound |= v[i];
    v[i] = value;
    do {
      index--;
      mtf[index + 1] = mtf[index];
    } while (index > 0);
    mtf[0] = value;
  }
  /* Remember amount of elements to be reinitialized. */
  state->mtf_upper_bound = upper_bound;
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
    uint32_t table_size;
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
static BrotliResult DecodeContextMap(uint32_t context_map_size,
                                     uint32_t* num_htrees,
                                     uint8_t** context_map_arg,
                                     BrotliState* s) {
  BrotliBitReader* br = &s->br;
  BrotliResult result = BROTLI_RESULT_SUCCESS;

  switch ((int)s->substate_context_map) {
    case BROTLI_STATE_CONTEXT_MAP_NONE:
      result = DecodeVarLenUint8(s, br, num_htrees);
      if (result != BROTLI_RESULT_SUCCESS) {
        return result;
      }
      (*num_htrees)++;
      s->context_index = 0;
      BROTLI_LOG_UINT(context_map_size);
      BROTLI_LOG_UINT(*num_htrees);
      *context_map_arg = (uint8_t*)BROTLI_ALLOC(s, (size_t)context_map_size);
      if (*context_map_arg == 0) {
        return BROTLI_FAILURE();
      }
      if (*num_htrees <= 1) {
        memset(*context_map_arg, 0, (size_t)context_map_size);
        return BROTLI_RESULT_SUCCESS;
      }
      s->substate_context_map = BROTLI_STATE_CONTEXT_MAP_READ_PREFIX;
      /* No break, continue to next state. */
    case BROTLI_STATE_CONTEXT_MAP_READ_PREFIX: {
      uint32_t bits;
      /* In next stage ReadHuffmanCode uses at least 4 bits, so it is safe
         to peek 4 bits ahead. */
      if (!BrotliSafeGetBits(br, 5, &bits)) {
        return BROTLI_RESULT_NEEDS_MORE_INPUT;
      }
      if ((bits & 1) != 0) { /* Use RLE for zeroes. */
        s->max_run_length_prefix = (bits >> 1) + 1;
        BrotliDropBits(br, 5);
      } else {
        s->max_run_length_prefix = 0;
        BrotliDropBits(br, 1);
      }
      BROTLI_LOG_UINT(s->max_run_length_prefix);
      s->substate_context_map = BROTLI_STATE_CONTEXT_MAP_HUFFMAN;
      /* No break, continue to next state. */
    }
    case BROTLI_STATE_CONTEXT_MAP_HUFFMAN:
      result = ReadHuffmanCode(*num_htrees + s->max_run_length_prefix,
                               s->context_map_table, NULL, s);
      if (result != BROTLI_RESULT_SUCCESS) return result;
      s->code = 0xFFFF;
      s->substate_context_map = BROTLI_STATE_CONTEXT_MAP_DECODE;
      /* No break, continue to next state. */
    case BROTLI_STATE_CONTEXT_MAP_DECODE: {
      uint32_t context_index = s->context_index;
      uint32_t max_run_length_prefix = s->max_run_length_prefix;
      uint8_t* context_map = *context_map_arg;
      uint32_t code = s->code;
      if (code != 0xFFFF) {
        goto rleCode;
      }
      while (context_index < context_map_size) {
        if (!SafeReadSymbol(s->context_map_table, br, &code)) {
          s->code = 0xFFFF;
          s->context_index = context_index;
          return BROTLI_RESULT_NEEDS_MORE_INPUT;
        }
        BROTLI_LOG_UINT(code);

        if (code == 0) {
          context_map[context_index++] = 0;
          continue;
        }
        if (code > max_run_length_prefix) {
          context_map[context_index++] =
              (uint8_t)(code - max_run_length_prefix);
          continue;
        }
rleCode:
        {
          uint32_t reps;
          if (!BrotliSafeReadBits(br, code, &reps)) {
            s->code = code;
            s->context_index = context_index;
            return BROTLI_RESULT_NEEDS_MORE_INPUT;
          }
          reps += 1U << code;
          BROTLI_LOG_UINT(reps);
          if (context_index + reps > context_map_size) {
            return BROTLI_FAILURE();
          }
          do {
            context_map[context_index++] = 0;
          } while (--reps);
        }
      }
      /* No break, continue to next state. */
    }
    case BROTLI_STATE_CONTEXT_MAP_TRANSFORM: {
      uint32_t bits;
      if (!BrotliSafeReadBits(br, 1, &bits)) {
        s->substate_context_map = BROTLI_STATE_CONTEXT_MAP_TRANSFORM;
        return BROTLI_RESULT_NEEDS_MORE_INPUT;
      }
      if (bits != 0) {
        InverseMoveToFrontTransform(*context_map_arg, context_map_size, s);
      }
      s->substate_context_map = BROTLI_STATE_CONTEXT_MAP_NONE;
      return BROTLI_RESULT_SUCCESS;
    }
    default:
      return BROTLI_FAILURE();
  }
}

/* Decodes a command or literal and updates block type ringbuffer.
   Reads 3..54 bits. */
static BROTLI_INLINE int DecodeBlockTypeAndLength(int safe,
    BrotliState* s, int tree_type) {
  uint32_t max_block_type = s->num_block_types[tree_type];
  const HuffmanCode* type_tree = &s->block_type_trees[
      tree_type * BROTLI_HUFFMAN_MAX_SIZE_258];
  const HuffmanCode* len_tree = &s->block_len_trees[
      tree_type * BROTLI_HUFFMAN_MAX_SIZE_26];
  BrotliBitReader* br = &s->br;
  uint32_t* ringbuffer = &s->block_type_rb[tree_type * 2];
  uint32_t block_type;

  /* Read 0..15 + 3..39 bits */
  if (!safe) {
    block_type = ReadSymbol(type_tree, br);
    s->block_length[tree_type] = ReadBlockLength(len_tree, br);
  } else {
    BrotliBitReaderState memento;
    BrotliBitReaderSaveState(br, &memento);
    if (!SafeReadSymbol(type_tree, br, &block_type)) return 0;
    if (!SafeReadBlockLength(s, &s->block_length[tree_type], len_tree, br)) {
      s->substate_read_block_length = BROTLI_STATE_READ_BLOCK_LENGTH_NONE;
      BrotliBitReaderRestoreState(br, &memento);
      return 0;
    }
  }

  if (block_type == 1) {
    block_type = ringbuffer[1] + 1;
  } else if (block_type == 0) {
    block_type = ringbuffer[0];
  } else {
    block_type -= 2;
  }
  if (block_type >= max_block_type) {
    block_type -= max_block_type;
  }
  ringbuffer[0] = ringbuffer[1];
  ringbuffer[1] = block_type;
  return 1;
}

/* Decodes the block type and updates the state for literal context.
   Reads 3..54 bits. */
static BROTLI_INLINE int DecodeLiteralBlockSwitchInternal(int safe,
    BrotliState* s) {
  uint8_t context_mode;
  uint32_t context_offset;
  if (!DecodeBlockTypeAndLength(safe, s, 0)) {
    return 0;
  }
  context_offset = s->block_type_rb[1] << kLiteralContextBits;
  s->context_map_slice = s->context_map + context_offset;
  s->literal_htree_index = s->context_map_slice[0];
  s->literal_htree = s->literal_hgroup.htrees[s->literal_htree_index];
  context_mode = s->context_modes[s->block_type_rb[1]];
  s->context_lookup1 = &kContextLookup[kContextLookupOffsets[context_mode]];
  s->context_lookup2 = &kContextLookup[kContextLookupOffsets[context_mode + 1]];
  return 1;
}

static void BROTLI_NOINLINE DecodeLiteralBlockSwitch(BrotliState* s) {
  DecodeLiteralBlockSwitchInternal(0, s);
}

static int BROTLI_NOINLINE SafeDecodeLiteralBlockSwitch(BrotliState* s) {
  return DecodeLiteralBlockSwitchInternal(1, s);
}

/* Block switch for insert/copy length.
   Reads 3..54 bits. */
static BROTLI_INLINE int DecodeCommandBlockSwitchInternal(int safe,
    BrotliState* s) {
  if (!DecodeBlockTypeAndLength(safe, s, 1)) {
    return 0;
  }
  s->htree_command = s->insert_copy_hgroup.htrees[s->block_type_rb[3]];
  return 1;
}

static void BROTLI_NOINLINE DecodeCommandBlockSwitch(BrotliState* s) {
  DecodeCommandBlockSwitchInternal(0, s);
}
static int BROTLI_NOINLINE SafeDecodeCommandBlockSwitch(BrotliState* s) {
  return DecodeCommandBlockSwitchInternal(1, s);
}

/* Block switch for distance codes.
   Reads 3..54 bits. */
static BROTLI_INLINE int DecodeDistanceBlockSwitchInternal(int safe,
    BrotliState* s) {
  if (!DecodeBlockTypeAndLength(safe, s, 2)) {
    return 0;
  }
  s->dist_context_map_slice =
      s->dist_context_map + (s->block_type_rb[5] << kDistanceContextBits);
  s->dist_htree_index = s->dist_context_map_slice[s->distance_context];
  return 1;
}

static void BROTLI_NOINLINE DecodeDistanceBlockSwitch(BrotliState* s) {
  DecodeDistanceBlockSwitchInternal(0, s);
}

static int BROTLI_NOINLINE SafeDecodeDistanceBlockSwitch(BrotliState* s) {
  return DecodeDistanceBlockSwitchInternal(1, s);
}

static BrotliResult WriteRingBuffer(size_t* available_out, uint8_t** next_out,
    size_t* total_out, BrotliState* s) {
  size_t pos = (s->pos > s->ringbuffer_size) ? (size_t)s->ringbuffer_size
                                             : (size_t)(s->pos);
  uint8_t* start =
      s->ringbuffer + (s->partial_pos_out & (size_t)s->ringbuffer_mask);
  size_t partial_pos_rb = (s->rb_roundtrips * (size_t)s->ringbuffer_size) + pos;
  size_t to_write = (partial_pos_rb - s->partial_pos_out);
  size_t num_written = *available_out;
  if (num_written > to_write) {
    num_written = to_write;
  }
  if (s->meta_block_remaining_len < 0) {
    return BROTLI_FAILURE();
  }
  memcpy(*next_out, start, num_written);
  *next_out += num_written;
  *available_out -= num_written;
  BROTLI_LOG_UINT(to_write);
  BROTLI_LOG_UINT(num_written);
  s->partial_pos_out += num_written;
  *total_out = s->partial_pos_out;
  if (num_written < to_write) {
    return BROTLI_RESULT_NEEDS_MORE_OUTPUT;
  }
  return BROTLI_RESULT_SUCCESS;
}

/* Allocates ringbuffer.

  s->ringbuffer_size MUST be updated by BrotliCalculateRingBufferSize before
  this function is called.

   Last two bytes of ringbuffer are initialized to 0, so context calculation
   could be done uniformly for the first two and all other positions.

   Custom dictionary, if any, is copied to the end of ringbuffer.
*/
static int BROTLI_NOINLINE BrotliAllocateRingBuffer(BrotliState* s) {
  /* We need the slack region for the following reasons:
      - doing up to two 16-byte copies for fast backward copying
      - inserting transformed dictionary word (5 prefix + 24 base + 8 suffix) */
  static const int kRingBufferWriteAheadSlack = 42;
  s->ringbuffer = (uint8_t*)BROTLI_ALLOC(s, (size_t)(s->ringbuffer_size +
      kRingBufferWriteAheadSlack));
  if (s->ringbuffer == 0) {
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

static BrotliResult BROTLI_NOINLINE CopyUncompressedBlockToOutput(
    size_t* available_out, uint8_t** next_out, size_t* total_out,
    BrotliState* s) {
  /* TODO: avoid allocation for single uncompressed block. */
  if (!s->ringbuffer && !BrotliAllocateRingBuffer(s)) {
    return BROTLI_FAILURE();
  }

  /* State machine */
  for (;;) {
    switch (s->substate_uncompressed) {
      case BROTLI_STATE_UNCOMPRESSED_NONE: {
        int nbytes = (int)BrotliGetRemainingBytes(&s->br);
        if (nbytes > s->meta_block_remaining_len) {
          nbytes = s->meta_block_remaining_len;
        }
        if (s->pos + nbytes > s->ringbuffer_size) {
          nbytes = s->ringbuffer_size - s->pos;
        }
        /* Copy remaining bytes from s->br.buf_ to ringbuffer. */
        BrotliCopyBytes(&s->ringbuffer[s->pos], &s->br, (size_t)nbytes);
        s->pos += nbytes;
        s->meta_block_remaining_len -= nbytes;
        if (s->pos < s->ringbuffer_size) {
          if (s->meta_block_remaining_len == 0) {
            return BROTLI_RESULT_SUCCESS;
          }
          return BROTLI_RESULT_NEEDS_MORE_INPUT;
        }
        s->substate_uncompressed = BROTLI_STATE_UNCOMPRESSED_WRITE;
        /* No break, continue to next state */
      }
      case BROTLI_STATE_UNCOMPRESSED_WRITE: {
        BrotliResult result =
            WriteRingBuffer(available_out, next_out, total_out, s);
        if (result != BROTLI_RESULT_SUCCESS) {
          return result;
        }
        s->pos = 0;
        s->rb_roundtrips++;
        s->max_distance = s->max_backward_distance;
        s->substate_uncompressed = BROTLI_STATE_UNCOMPRESSED_NONE;
        break;
      }
    }
  }
  BROTLI_DCHECK(0);  /* Unreachable */
}

int BrotliDecompressedSize(size_t encoded_size,
                           const uint8_t* encoded_buffer,
                           size_t* decoded_size) {
  BrotliState s;
  int next_block_header;
  BrotliStateInit(&s);
  s.br.next_in = encoded_buffer;
  s.br.avail_in = encoded_size;
  if (!BrotliWarmupBitReader(&s.br)) {
    return 0;
  }
  DecodeWindowBits(&s.br);
  if (DecodeMetaBlockLength(&s, &s.br) != BROTLI_RESULT_SUCCESS) {
    return 0;
  }
  *decoded_size = (size_t)s.meta_block_remaining_len;
  if (s.is_last_metablock) {
    return 1;
  }
  if (!s.is_uncompressed || !BrotliJumpToByteBoundary(&s.br)) {
    return 0;
  }
  next_block_header = BrotliPeekByte(&s.br, (size_t)s.meta_block_remaining_len);
  return (next_block_header != -1) && ((next_block_header & 3) == 3);
}

/* Calculates the smallest feasible ring buffer.

   If we know the data size is small, do not allocate more ringbuffer
   size than needed to reduce memory usage.

   When this method is called, metablock size and flags MUST be decoded.
*/
static void BROTLI_NOINLINE BrotliCalculateRingBufferSize(BrotliState* s,
    BrotliBitReader* br) {
  int is_last = s->is_last_metablock;
  int window_size = 1 << s->window_bits;
  s->ringbuffer_size = window_size;

  if (s->is_uncompressed) {
    int next_block_header =
        BrotliPeekByte(br, (size_t)s->meta_block_remaining_len);
    if (next_block_header != -1) {  /* Peek succeeded */
      if ((next_block_header & 3) == 3) {  /* ISLAST and ISEMPTY */
        is_last = 1;
      }
    }
  }

  /* Limit custom dictionary size to stream window size. */
  if (s->custom_dict_size >= window_size) {
    s->custom_dict += s->custom_dict_size - window_size;
    s->custom_dict_size = window_size;
  }

  /* We need at least 2 bytes of ring buffer size to get the last two
     bytes for context from there */
  if (is_last) {
    int min_size_x2 = (s->meta_block_remaining_len + s->custom_dict_size) * 2;
    while (s->ringbuffer_size >= min_size_x2 && s->ringbuffer_size > 32) {
      s->ringbuffer_size >>= 1;
    }
  }

  s->ringbuffer_mask = s->ringbuffer_size - 1;
}

/* Reads 1..256 2-bit context modes. */
static BrotliResult ReadContextModes(BrotliState* s) {
  BrotliBitReader* br = &s->br;
  int i = s->loop_counter;

  while (i < (int)s->num_block_types[0]) {
    uint32_t bits;
    if (!BrotliSafeReadBits(br, 2, &bits)) {
      s->loop_counter = i;
      return BROTLI_RESULT_NEEDS_MORE_INPUT;
    }
    s->context_modes[i] = (uint8_t)(bits << 1);
    BROTLI_LOG_ARRAY_INDEX(s->context_modes, i);
    i++;
  }
  return BROTLI_RESULT_SUCCESS;
}

static BROTLI_INLINE void TakeDistanceFromRingBuffer(BrotliState* s) {
  if (s->distance_code == 0) {
    --s->dist_rb_idx;
    s->distance_code = s->dist_rb[s->dist_rb_idx & 3];
  } else {
    int distance_code = s->distance_code << 1;
    /* kDistanceShortCodeIndexOffset has 2-bit values from LSB: */
    /* 3, 2, 1, 0, 3, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2 */
    const uint32_t kDistanceShortCodeIndexOffset = 0xaaafff1b;
    /* kDistanceShortCodeValueOffset has 2-bit values from LSB: */
    /*-0, 0,-0, 0,-1, 1,-2, 2,-3, 3,-1, 1,-2, 2,-3, 3 */
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
}

static BROTLI_INLINE int SafeReadBits(
    BrotliBitReader* const br, uint32_t n_bits, uint32_t* val) {
  if (n_bits != 0) {
    return BrotliSafeReadBits(br, n_bits, val);
  } else {
    *val = 0;
    return 1;
  }
}

/* Precondition: s->distance_code < 0 */
static BROTLI_INLINE int ReadDistanceInternal(int safe,
    BrotliState* s, BrotliBitReader* br) {
  int distval;
  BrotliBitReaderState memento;
  HuffmanCode* distance_tree = s->distance_hgroup.htrees[s->dist_htree_index];
  if (!safe) {
    s->distance_code = (int)ReadSymbol(distance_tree, br);
  } else {
    uint32_t code;
    BrotliBitReaderSaveState(br, &memento);
    if (!SafeReadSymbol(distance_tree, br, &code)) {
      return 0;
    }
    s->distance_code = (int)code;
  }
  /* Convert the distance code to the actual distance by possibly */
  /* looking up past distances from the s->ringbuffer. */
  if ((s->distance_code & ~0xf) == 0) {
    TakeDistanceFromRingBuffer(s);
    --s->block_length[2];
    return 1;
  }
  distval = s->distance_code - (int)s->num_direct_distance_codes;
  if (distval >= 0) {
    uint32_t nbits;
    int postfix;
    int offset;
    if (!safe && (s->distance_postfix_bits == 0)) {
      nbits = ((uint32_t)distval >> 1) + 1;
      offset = ((2 + (distval & 1)) << nbits) - 4;
      s->distance_code = (int)s->num_direct_distance_codes + offset +
                         (int)BrotliReadBits(br, nbits);
    } else {
      /* This branch also works well when s->distance_postfix_bits == 0 */
      uint32_t bits;
      postfix = distval & s->distance_postfix_mask;
      distval >>= s->distance_postfix_bits;
      nbits = ((uint32_t)distval >> 1) + 1;
      if (safe) {
        if (!SafeReadBits(br, nbits, &bits)) {
          s->distance_code = -1; /* Restore precondition. */
          BrotliBitReaderRestoreState(br, &memento);
          return 0;
        }
      } else {
        bits = BrotliReadBits(br, nbits);
      }
      offset = ((2 + (distval & 1)) << nbits) - 4;
      s->distance_code = (int)s->num_direct_distance_codes +
          ((offset + (int)bits) << s->distance_postfix_bits) + postfix;
    }
  }
  s->distance_code = s->distance_code - NUM_DISTANCE_SHORT_CODES + 1;
  --s->block_length[2];
  return 1;
}

static BROTLI_INLINE void ReadDistance(BrotliState* s, BrotliBitReader* br) {
  ReadDistanceInternal(0, s, br);
}

static BROTLI_INLINE int SafeReadDistance(BrotliState* s, BrotliBitReader* br) {
  return ReadDistanceInternal(1, s, br);
}

static BROTLI_INLINE int ReadCommandInternal(int safe,
    BrotliState* s, BrotliBitReader* br, int* insert_length) {
  uint32_t cmd_code;
  uint32_t insert_len_extra = 0;
  uint32_t copy_length;
  CmdLutElement v;
  BrotliBitReaderState memento;
  if (!safe) {
    cmd_code = ReadSymbol(s->htree_command, br);
  } else {
    BrotliBitReaderSaveState(br, &memento);
    if (!SafeReadSymbol(s->htree_command, br, &cmd_code)) {
      return 0;
    }
  }
  v = kCmdLut[cmd_code];
  s->distance_code = v.distance_code;
  s->distance_context = v.context;
  s->dist_htree_index = s->dist_context_map_slice[s->distance_context];
  *insert_length = v.insert_len_offset;
  if (!safe) {
    if (PREDICT_FALSE(v.insert_len_extra_bits != 0)) {
      insert_len_extra = BrotliReadBits(br, v.insert_len_extra_bits);
    }
    copy_length = BrotliReadBits(br, v.copy_len_extra_bits);
  } else {
    if (!SafeReadBits(br, v.insert_len_extra_bits, &insert_len_extra) ||
        !SafeReadBits(br, v.copy_len_extra_bits, &copy_length)) {
      BrotliBitReaderRestoreState(br, &memento);
      return 0;
    }
  }
  s->copy_length = (int)copy_length + v.copy_len_offset;
  --s->block_length[1];
  *insert_length += (int)insert_len_extra;
  return 1;
}

static BROTLI_INLINE void ReadCommand(BrotliState* s, BrotliBitReader* br,
    int* insert_length) {
  ReadCommandInternal(0, s, br, insert_length);
}

static BROTLI_INLINE int SafeReadCommand(BrotliState* s, BrotliBitReader* br,
    int* insert_length) {
  return ReadCommandInternal(1, s, br, insert_length);
}

static BROTLI_INLINE int CheckInputAmount(int safe,
    BrotliBitReader* const br, size_t num) {
  if (safe) {
    return 1;
  }
  return BrotliCheckInputAmount(br, num);
}

#define BROTLI_SAFE(METHOD)                      \
  {                                              \
    if (safe) {                                  \
      if (!Safe##METHOD) {                       \
        result = BROTLI_RESULT_NEEDS_MORE_INPUT; \
        goto saveStateAndReturn;                 \
      }                                          \
    } else {                                     \
      METHOD;                                    \
    }                                            \
  }

static BROTLI_INLINE BrotliResult ProcessCommandsInternal(int safe,
    BrotliState* s) {
  int pos = s->pos;
  int i = s->loop_counter;
  BrotliResult result = BROTLI_RESULT_SUCCESS;
  BrotliBitReader* br = &s->br;

  if (!CheckInputAmount(safe, br, 28)) {
    result = BROTLI_RESULT_NEEDS_MORE_INPUT;
    goto saveStateAndReturn;
  }
  if (!safe) {
    BROTLI_UNUSED(BrotliWarmupBitReader(br));
  }

  /* Jump into state machine. */
  if (s->state == BROTLI_STATE_COMMAND_BEGIN) {
    goto CommandBegin;
  } else if (s->state == BROTLI_STATE_COMMAND_INNER) {
    goto CommandInner;
  } else if (s->state == BROTLI_STATE_COMMAND_POST_DECODE_LITERALS) {
    goto CommandPostDecodeLiterals;
  } else if (s->state == BROTLI_STATE_COMMAND_POST_WRAP_COPY) {
    goto CommandPostWrapCopy;
  } else {
    return BROTLI_FAILURE();
  }

CommandBegin:
  if (safe) {
    s->state = BROTLI_STATE_COMMAND_BEGIN;
  }
  if (!CheckInputAmount(safe, br, 28)) { /* 156 bits + 7 bytes */
    s->state = BROTLI_STATE_COMMAND_BEGIN;
    result = BROTLI_RESULT_NEEDS_MORE_INPUT;
    goto saveStateAndReturn;
  }
  if (PREDICT_FALSE(s->block_length[1] == 0)) {
    BROTLI_SAFE(DecodeCommandBlockSwitch(s));
    goto CommandBegin;
  }
  /* Read the insert/copy length in the command */
  BROTLI_SAFE(ReadCommand(s, br, &i));
  BROTLI_LOG(("[ProcessCommandsInternal] pos = %d insert = %d copy = %d\n",
              pos, i, s->copy_length));
  if (i == 0) {
    goto CommandPostDecodeLiterals;
  }
  s->meta_block_remaining_len -= i;

CommandInner:
  if (safe) {
    s->state = BROTLI_STATE_COMMAND_INNER;
  }
  /* Read the literals in the command */
  if (s->trivial_literal_context) {
    uint32_t bits;
    uint32_t value;
    PreloadSymbol(safe, s->literal_htree, br, &bits, &value);
    do {
      if (!CheckInputAmount(safe, br, 28)) { /* 162 bits + 7 bytes */
        s->state = BROTLI_STATE_COMMAND_INNER;
        result = BROTLI_RESULT_NEEDS_MORE_INPUT;
        goto saveStateAndReturn;
      }
      if (PREDICT_FALSE(s->block_length[0] == 0)) {
        BROTLI_SAFE(DecodeLiteralBlockSwitch(s));
        PreloadSymbol(safe, s->literal_htree, br, &bits, &value);
      }
      if (!safe) {
        s->ringbuffer[pos] =
            (uint8_t)ReadPreloadedSymbol(s->literal_htree, br, &bits, &value);
      } else {
        uint32_t literal;
        if (!SafeReadSymbol(s->literal_htree, br, &literal)) {
          result = BROTLI_RESULT_NEEDS_MORE_INPUT;
          goto saveStateAndReturn;
        }
        s->ringbuffer[pos] = (uint8_t)literal;
      }
      --s->block_length[0];
      BROTLI_LOG_UINT(s->literal_htree_index);
      BROTLI_LOG_ARRAY_INDEX(s->ringbuffer, pos);
      ++pos;
      if (PREDICT_FALSE(pos == s->ringbuffer_size)) {
        s->state = BROTLI_STATE_COMMAND_INNER_WRITE;
        --i;
        goto saveStateAndReturn;
      }
    } while (--i != 0);
  } else {
    uint8_t p1 = s->ringbuffer[(pos - 1) & s->ringbuffer_mask];
    uint8_t p2 = s->ringbuffer[(pos - 2) & s->ringbuffer_mask];
    do {
      const HuffmanCode* hc;
      uint8_t context;
      if (!CheckInputAmount(safe, br, 28)) { /* 162 bits + 7 bytes */
        s->state = BROTLI_STATE_COMMAND_INNER;
        result = BROTLI_RESULT_NEEDS_MORE_INPUT;
        goto saveStateAndReturn;
      }
      if (PREDICT_FALSE(s->block_length[0] == 0)) {
        BROTLI_SAFE(DecodeLiteralBlockSwitch(s));
      }
      context = s->context_lookup1[p1] | s->context_lookup2[p2];
      BROTLI_LOG_UINT(context);
      hc = s->literal_hgroup.htrees[s->context_map_slice[context]];
      p2 = p1;
      if (!safe) {
        p1 = (uint8_t)ReadSymbol(hc, br);
      } else {
        uint32_t literal;
        if (!SafeReadSymbol(hc, br, &literal)) {
          result = BROTLI_RESULT_NEEDS_MORE_INPUT;
          goto saveStateAndReturn;
        }
        p1 = (uint8_t)literal;
      }
      s->ringbuffer[pos] = p1;
      --s->block_length[0];
      BROTLI_LOG_UINT(s->context_map_slice[context]);
      BROTLI_LOG_ARRAY_INDEX(s->ringbuffer, pos & s->ringbuffer_mask);
      ++pos;
      if (PREDICT_FALSE(pos == s->ringbuffer_size)) {
        s->state = BROTLI_STATE_COMMAND_INNER_WRITE;
        --i;
        goto saveStateAndReturn;
      }
    } while (--i != 0);
  }
  BROTLI_LOG_UINT(s->meta_block_remaining_len);
  if (s->meta_block_remaining_len <= 0) {
    s->state = BROTLI_STATE_METABLOCK_DONE;
    goto saveStateAndReturn;
  }

CommandPostDecodeLiterals:
  if (safe) {
    s->state = BROTLI_STATE_COMMAND_POST_DECODE_LITERALS;
  }
  if (s->distance_code >= 0) {
    --s->dist_rb_idx;
    s->distance_code = s->dist_rb[s->dist_rb_idx & 3];
    goto postReadDistance;  /* We already have the implicit distance */
  }
  /* Read distance code in the command, unless it was implicitly zero. */
  if (PREDICT_FALSE(s->block_length[2] == 0)) {
    BROTLI_SAFE(DecodeDistanceBlockSwitch(s));
  }
  BROTLI_SAFE(ReadDistance(s, br));
postReadDistance:
  BROTLI_LOG(("[ProcessCommandsInternal] pos = %d distance = %d\n",
              pos, s->distance_code));
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
      int offset = (int)kBrotliDictionaryOffsetsByLength[i];
      int word_id = s->distance_code - s->max_distance - 1;
      uint32_t shift = kBrotliDictionarySizeBitsByLength[i];
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
          /*s->partial_pos_rb += (size_t)s->ringbuffer_size;*/
          s->state = BROTLI_STATE_COMMAND_POST_WRITE_1;
          goto saveStateAndReturn;
        }
      } else {
        BROTLI_LOG(("Invalid backward reference. pos: %d distance: %d "
            "len: %d bytes left: %d\n",
            pos, s->distance_code, i, s->meta_block_remaining_len));
        return BROTLI_FAILURE();
      }
    } else {
      BROTLI_LOG(("Invalid backward reference. pos: %d distance: %d "
          "len: %d bytes left: %d\n",
          pos, s->distance_code, i, s->meta_block_remaining_len));
      return BROTLI_FAILURE();
    }
  } else {
    int src_start = (pos - s->distance_code) & s->ringbuffer_mask;
    uint8_t* copy_dst = &s->ringbuffer[pos];
    uint8_t* copy_src = &s->ringbuffer[src_start];
    int dst_end = pos + i;
    int src_end = src_start + i;
    /* update the recent distances cache */
    s->dist_rb[s->dist_rb_idx & 3] = s->distance_code;
    ++s->dist_rb_idx;
    s->meta_block_remaining_len -= i;
    if (PREDICT_FALSE(s->meta_block_remaining_len < 0)) {
      BROTLI_LOG(("Invalid backward reference. pos: %d distance: %d "
          "len: %d bytes left: %d\n",
          pos, s->distance_code, i, s->meta_block_remaining_len));
      return BROTLI_FAILURE();
    }
    /* There are 32+ bytes of slack in the ringbuffer allocation.
       Also, we have 16 short codes, that make these 16 bytes irrelevant
       in the ringbuffer. Let's copy over them as a first guess.
     */
    memmove16(copy_dst, copy_src);
    if (src_end > pos && dst_end > src_start) {
      /* Regions intersect. */
      goto CommandPostWrapCopy;
    }
    if (dst_end >= s->ringbuffer_size || src_end >= s->ringbuffer_size) {
      /* At least one region wraps. */
      goto CommandPostWrapCopy;
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
  BROTLI_LOG_UINT(s->meta_block_remaining_len);
  if (s->meta_block_remaining_len <= 0) {
    /* Next metablock, if any */
    s->state = BROTLI_STATE_METABLOCK_DONE;
    goto saveStateAndReturn;
  } else {
    goto CommandBegin;
  }
CommandPostWrapCopy:
  {
    int wrap_guard = s->ringbuffer_size - pos;
    while (--i >= 0) {
      s->ringbuffer[pos] =
          s->ringbuffer[(pos - s->distance_code) & s->ringbuffer_mask];
      ++pos;
      if (PREDICT_FALSE(--wrap_guard == 0)) {
        s->state = BROTLI_STATE_COMMAND_POST_WRITE_2;
        goto saveStateAndReturn;
      }
    }
  }
  if (s->meta_block_remaining_len <= 0) {
    /* Next metablock, if any */
    s->state = BROTLI_STATE_METABLOCK_DONE;
    goto saveStateAndReturn;
  } else {
    goto CommandBegin;
  }

saveStateAndReturn:
  s->pos = pos;
  s->loop_counter = i;
  return result;
}

#undef BROTLI_SAFE

static BROTLI_NOINLINE BrotliResult ProcessCommands(BrotliState* s) {
  return ProcessCommandsInternal(0, s);
}

static BROTLI_NOINLINE BrotliResult SafeProcessCommands(BrotliState* s) {
  return ProcessCommandsInternal(1, s);
}

BrotliResult BrotliDecompressBuffer(size_t encoded_size,
                                    const uint8_t* encoded_buffer,
                                    size_t* decoded_size,
                                    uint8_t* decoded_buffer) {
  BrotliState s;
  BrotliResult result;
  size_t total_out = 0;
  size_t available_in = encoded_size;
  const uint8_t* next_in = encoded_buffer;
  size_t available_out = *decoded_size;
  uint8_t* next_out = decoded_buffer;
  BrotliStateInit(&s);
  result = BrotliDecompressStream(&available_in, &next_in, &available_out,
      &next_out, &total_out, &s);
  *decoded_size = total_out;
  BrotliStateCleanup(&s);
  if (result != BROTLI_RESULT_SUCCESS) {
    result = BROTLI_RESULT_ERROR;
  }
  return result;
}

/* Invariant: input stream is never overconsumed:
    * invalid input implies that the whole stream is invalid -> any amount of
      input could be read and discarded
    * when result is "needs more input", then at leat one more byte is REQUIRED
      to complete decoding; all input data MUST be consumed by decoder, so
      client could swap the input buffer
    * when result is "needs more output" decoder MUST ensure that it doesn't
      hold more than 7 bits in bit reader; this saves client from swapping input
      buffer ahead of time
    * when result is "success" decoder MUST return all unused data back to input
      buffer; this is possible because the invariant is hold on enter
*/
BrotliResult BrotliDecompressStream(size_t* available_in,
    const uint8_t** next_in, size_t* available_out, uint8_t** next_out,
    size_t* total_out, BrotliState* s) {
  BrotliResult result = BROTLI_RESULT_SUCCESS;
  BrotliBitReader* br = &s->br;
  if (s->buffer_length == 0) { /* Just connect bit reader to input stream. */
    br->avail_in = *available_in;
    br->next_in = *next_in;
  } else {
    /* At least one byte of input is required. More than one byte of input may
       be required to complete the transaction -> reading more data must be
       done in a loop -> do it in a main loop. */
    result = BROTLI_RESULT_NEEDS_MORE_INPUT;
    br->next_in = &s->buffer.u8[0];
  }
  /* State machine */
  for (;;) {
    if (result != BROTLI_RESULT_SUCCESS) { /* Error | needs more input/output */
      if (result == BROTLI_RESULT_NEEDS_MORE_INPUT) {
        if (s->ringbuffer != 0) { /* Proactively push output. */
          WriteRingBuffer(available_out, next_out, total_out, s);
        }
        if (s->buffer_length != 0) { /* Used with internal buffer. */
          if (br->avail_in == 0) { /* Successfully finished read transaction. */
            /* Accamulator contains less than 8 bits, because internal buffer
               is expanded byte-by-byte until it is enough to complete read. */
            s->buffer_length = 0;
            /* Switch to input stream and restart. */
            result = BROTLI_RESULT_SUCCESS;
            br->avail_in = *available_in;
            br->next_in = *next_in;
            continue;
          } else if (*available_in != 0) {
            /* Not enough data in buffer, but can take one more byte from
               input stream. */
            result = BROTLI_RESULT_SUCCESS;
            s->buffer.u8[s->buffer_length] = **next_in;
            s->buffer_length++;
            br->avail_in = s->buffer_length;
            (*next_in)++;
            (*available_in)--;
            /* Retry with more data in buffer. */
            continue;
          }
          /* Can't finish reading and no more input.*/
          break;
        } else { /* Input stream doesn't contain enough input. */
          /* Copy tail to internal buffer and return. */
          *next_in = br->next_in;
          *available_in = br->avail_in;
          while (*available_in) {
            s->buffer.u8[s->buffer_length] = **next_in;
            s->buffer_length++;
            (*next_in)++;
            (*available_in)--;
          }
          break;
        }
        /* Unreachable. */
      }

      /* Fail or needs more output. */

      if (s->buffer_length != 0) {
        /* Just consumed the buffered input and produced some output. Otherwise
           it would result in "needs more input". Reset internal buffer.*/
        s->buffer_length = 0;
      } else {
        /* Using input stream in last iteration. When decoder switches to input
           stream it has less than 8 bits in accamulator, so it is safe to
           return unused accamulator bits there. */
        BrotliBitReaderUnload(br);
        *available_in = br->avail_in;
        *next_in = br->next_in;
      }
      break;
    }
    switch (s->state) {
      case BROTLI_STATE_UNINITED:
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
        s->block_type_trees = (HuffmanCode*)BROTLI_ALLOC(s,
            sizeof(HuffmanCode) * 3 *
                (BROTLI_HUFFMAN_MAX_SIZE_258 + BROTLI_HUFFMAN_MAX_SIZE_26));
        if (s->block_type_trees == 0) {
          result = BROTLI_FAILURE();
          break;
        }
        s->block_len_trees =
            s->block_type_trees + 3 * BROTLI_HUFFMAN_MAX_SIZE_258;

        s->state = BROTLI_STATE_METABLOCK_BEGIN;
        /* No break, continue to next state */
      case BROTLI_STATE_METABLOCK_BEGIN:
        BrotliStateMetablockBegin(s);
        BROTLI_LOG_UINT(s->pos);
        s->state = BROTLI_STATE_METABLOCK_HEADER;
        /* No break, continue to next state */
      case BROTLI_STATE_METABLOCK_HEADER:
        result = DecodeMetaBlockLength(s, br); /* Reads 2 - 31 bits. */
        if (result != BROTLI_RESULT_SUCCESS) {
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
          BrotliCalculateRingBufferSize(s, br);
        }
        if (s->is_uncompressed) {
          s->state = BROTLI_STATE_UNCOMPRESSED;
          break;
        }
        s->loop_counter = 0;
        s->state = BROTLI_STATE_HUFFMAN_CODE_0;
        break;
      case BROTLI_STATE_UNCOMPRESSED: {
        int bytes_copied = s->meta_block_remaining_len;
        result = CopyUncompressedBlockToOutput(
            available_out, next_out, total_out, s);
        bytes_copied -= s->meta_block_remaining_len;
        if (result != BROTLI_RESULT_SUCCESS) {
          break;
        }
        s->state = BROTLI_STATE_METABLOCK_DONE;
        break;
      }
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
        if (s->loop_counter >= 3) {
          s->state = BROTLI_STATE_METABLOCK_HEADER_2;
          break;
        }
        /* Reads 1..11 bits. */
        result = DecodeVarLenUint8(s, br, &s->num_block_types[s->loop_counter]);
        if (result != BROTLI_RESULT_SUCCESS) {
          break;
        }
        s->num_block_types[s->loop_counter]++;
        BROTLI_LOG_UINT(s->num_block_types[s->loop_counter]);
        if (s->num_block_types[s->loop_counter] < 2) {
          s->loop_counter++;
          break;
        }
        s->state = BROTLI_STATE_HUFFMAN_CODE_1;
        /* No break, continue to next state */
      case BROTLI_STATE_HUFFMAN_CODE_1: {
        int tree_offset = s->loop_counter * BROTLI_HUFFMAN_MAX_SIZE_258;
        result = ReadHuffmanCode(s->num_block_types[s->loop_counter] + 2,
            &s->block_type_trees[tree_offset], NULL, s);
        if (result != BROTLI_RESULT_SUCCESS) break;
        s->state = BROTLI_STATE_HUFFMAN_CODE_2;
        /* No break, continue to next state */
      }
      case BROTLI_STATE_HUFFMAN_CODE_2: {
        int tree_offset = s->loop_counter * BROTLI_HUFFMAN_MAX_SIZE_26;
        result = ReadHuffmanCode(kNumBlockLengthCodes,
            &s->block_len_trees[tree_offset], NULL, s);
        if (result != BROTLI_RESULT_SUCCESS) break;
        s->state = BROTLI_STATE_HUFFMAN_CODE_3;
        /* No break, continue to next state */
      }
      case BROTLI_STATE_HUFFMAN_CODE_3: {
        int tree_offset = s->loop_counter * BROTLI_HUFFMAN_MAX_SIZE_26;
        if (!SafeReadBlockLength(s, &s->block_length[s->loop_counter],
            &s->block_len_trees[tree_offset], br)) {
          result = BROTLI_RESULT_NEEDS_MORE_INPUT;
          break;
        }
        BROTLI_LOG_UINT(s->block_length[s->loop_counter]);
        s->loop_counter++;
        s->state = BROTLI_STATE_HUFFMAN_CODE_0;
        break;
      }
      case BROTLI_STATE_METABLOCK_HEADER_2: {
        uint32_t bits;
        if (!BrotliSafeReadBits(br, 6, &bits)) {
          result = BROTLI_RESULT_NEEDS_MORE_INPUT;
          break;
        }
        s->distance_postfix_bits = bits & BitMask(2);
        bits >>= 2;
        s->num_direct_distance_codes =
            NUM_DISTANCE_SHORT_CODES + (bits << s->distance_postfix_bits);
        BROTLI_LOG_UINT(s->num_direct_distance_codes);
        BROTLI_LOG_UINT(s->distance_postfix_bits);
        s->distance_postfix_mask = (int)BitMask(s->distance_postfix_bits);
        s->context_modes =
            (uint8_t*)BROTLI_ALLOC(s, (size_t)s->num_block_types[0]);
        if (s->context_modes == 0) {
          result = BROTLI_FAILURE();
          break;
        }
        s->loop_counter = 0;
        s->state = BROTLI_STATE_CONTEXT_MODES;
        /* No break, continue to next state */
      }
      case BROTLI_STATE_CONTEXT_MODES:
        result = ReadContextModes(s);
        if (result != BROTLI_RESULT_SUCCESS) {
          break;
        }
        s->state = BROTLI_STATE_CONTEXT_MAP_1;
        /* No break, continue to next state */
      case BROTLI_STATE_CONTEXT_MAP_1: {
        uint32_t j;
        result = DecodeContextMap(s->num_block_types[0] << kLiteralContextBits,
                                  &s->num_literal_htrees, &s->context_map, s);
        if (result != BROTLI_RESULT_SUCCESS) {
          break;
        }
        s->trivial_literal_context = 1;
        for (j = 0; j < s->num_block_types[0] << kLiteralContextBits; j++) {
          if (s->context_map[j] != j >> kLiteralContextBits) {
            s->trivial_literal_context = 0;
            break;
          }
        }
        s->state = BROTLI_STATE_CONTEXT_MAP_2;
        /* No break, continue to next state */
      }
      case BROTLI_STATE_CONTEXT_MAP_2:
        {
          uint32_t num_distance_codes =
              s->num_direct_distance_codes + (48U << s->distance_postfix_bits);
          result = DecodeContextMap(
              s->num_block_types[2] << kDistanceContextBits,
              &s->num_dist_htrees, &s->dist_context_map, s);
          if (result != BROTLI_RESULT_SUCCESS) {
            break;
          }
          BrotliHuffmanTreeGroupInit(s, &s->literal_hgroup, kNumLiteralCodes,
                                     s->num_literal_htrees);
          BrotliHuffmanTreeGroupInit(s, &s->insert_copy_hgroup,
                                     kNumInsertAndCopyCodes,
                                     s->num_block_types[1]);
          BrotliHuffmanTreeGroupInit(s, &s->distance_hgroup, num_distance_codes,
                                     s->num_dist_htrees);
          if (s->literal_hgroup.codes == 0 ||
              s->insert_copy_hgroup.codes == 0 ||
              s->distance_hgroup.codes == 0) {
            return BROTLI_FAILURE();
          }
        }
        s->loop_counter = 0;
        s->state = BROTLI_STATE_TREE_GROUP;
        /* No break, continue to next state */
      case BROTLI_STATE_TREE_GROUP:
        {
          HuffmanTreeGroup* hgroup = NULL;
          switch (s->loop_counter) {
            case 0:
              hgroup = &s->literal_hgroup;
              break;
            case 1:
              hgroup = &s->insert_copy_hgroup;
              break;
            case 2:
              hgroup = &s->distance_hgroup;
              break;
            default:
              return BROTLI_FAILURE();
          }
          result = HuffmanTreeGroupDecode(hgroup, s);
        }
        if (result != BROTLI_RESULT_SUCCESS) break;
        s->loop_counter++;
        if (s->loop_counter >= 3) {
          uint8_t context_mode = s->context_modes[s->block_type_rb[1]];
          s->context_map_slice = s->context_map;
          s->dist_context_map_slice = s->dist_context_map;
          s->context_lookup1 =
              &kContextLookup[kContextLookupOffsets[context_mode]];
          s->context_lookup2 =
              &kContextLookup[kContextLookupOffsets[context_mode + 1]];
          s->htree_command = s->insert_copy_hgroup.htrees[0];
          s->literal_htree = s->literal_hgroup.htrees[s->literal_htree_index];
          if (!s->ringbuffer && !BrotliAllocateRingBuffer(s)) {
            result = BROTLI_FAILURE();
            break;
          }
          s->state = BROTLI_STATE_COMMAND_BEGIN;
        }
        break;
      case BROTLI_STATE_COMMAND_BEGIN:
      case BROTLI_STATE_COMMAND_INNER:
      case BROTLI_STATE_COMMAND_POST_DECODE_LITERALS:
      case BROTLI_STATE_COMMAND_POST_WRAP_COPY:
        result = ProcessCommands(s);
        if (result == BROTLI_RESULT_NEEDS_MORE_INPUT) {
          result = SafeProcessCommands(s);
        }
        break;
      case BROTLI_STATE_COMMAND_INNER_WRITE:
      case BROTLI_STATE_COMMAND_POST_WRITE_1:
      case BROTLI_STATE_COMMAND_POST_WRITE_2:
        result = WriteRingBuffer(available_out, next_out, total_out, s);
        if (result != BROTLI_RESULT_SUCCESS) {
          break;
        }
        s->pos -= s->ringbuffer_size;
        s->rb_roundtrips++;
        s->max_distance = s->max_backward_distance;
        if (s->state == BROTLI_STATE_COMMAND_POST_WRITE_1) {
          memcpy(s->ringbuffer, s->ringbuffer_end, (size_t)s->pos);
          if (s->meta_block_remaining_len == 0) {
            /* Next metablock, if any */
            s->state = BROTLI_STATE_METABLOCK_DONE;
          } else {
            s->state = BROTLI_STATE_COMMAND_BEGIN;
          }
          break;
        } else if (s->state == BROTLI_STATE_COMMAND_POST_WRITE_2) {
          s->state = BROTLI_STATE_COMMAND_POST_WRAP_COPY;
        } else {  /* BROTLI_STATE_COMMAND_INNER_WRITE */
          if (s->loop_counter == 0) {
            if (s->meta_block_remaining_len == 0) {
              s->state = BROTLI_STATE_METABLOCK_DONE;
            } else {
              s->state = BROTLI_STATE_COMMAND_POST_DECODE_LITERALS;
            }
            break;
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
        if (!BrotliJumpToByteBoundary(br)) {
          result = BROTLI_FAILURE();
        }
        if (s->buffer_length == 0) {
          BrotliBitReaderUnload(br);
          *available_in = br->avail_in;
          *next_in = br->next_in;
        }
        s->state = BROTLI_STATE_DONE;
        /* No break, continue to next state */
      case BROTLI_STATE_DONE:
        if (s->ringbuffer != 0) {
          result = WriteRingBuffer(available_out, next_out, total_out, s);
          if (result != BROTLI_RESULT_SUCCESS) {
            break;
          }
        }
        return result;
    }
  }
  return result;
}

void BrotliSetCustomDictionary(
    size_t size, const uint8_t* dict, BrotliState* s) {
  s->custom_dict = dict;
  s->custom_dict_size = (int)size;
}


#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif
