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

/* Utilities for building Huffman decoding tables. */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "./huffman.h"
#include "./port.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/* Returns reverse(reverse(key, len) + 1, len), where reverse(key, len) is the
   bit-wise reversal of the len least significant bits of key. */
static BROTLI_INLINE uint32_t GetNextKey(uint32_t key, int len) {
#ifdef BROTLI_RBIT
  return BROTLI_RBIT(BROTLI_RBIT(key) + (1 << (8 * sizeof(unsigned) - len)));
#else
  unsigned step = (unsigned)(1 << (len - 1));
  while (key & step) {
    step >>= 1;
  }
  return (key & (step - 1)) + step;
#endif
}

/* Stores code in table[0], table[step], table[2*step], ..., table[end] */
/* Assumes that end is an integer multiple of step */
static BROTLI_INLINE void ReplicateValue(HuffmanCode* table,
                                         int step, int end,
                                         HuffmanCode code) {
  do {
    end -= step;
    table[end] = code;
  } while (end > 0);
}

/* Returns the table width of the next 2nd level table. count is the histogram
   of bit lengths for the remaining symbols, len is the code length of the next
   processed symbol */
static BROTLI_INLINE int NextTableBitSize(const uint16_t* const count,
                                          int len, int root_bits) {
  int left = 1 << (len - root_bits);
  while (len < BROTLI_HUFFMAN_MAX_CODE_LENGTH) {
    left -= count[len];
    if (left <= 0) break;
    ++len;
    left <<= 1;
  }
  return len - root_bits;
}


void BrotliBuildCodeLengthsHuffmanTable(HuffmanCode* table,
                                        const uint8_t* const code_lengths,
                                        uint16_t *count) {
  HuffmanCode code;    /* current table entry */
  int symbol;          /* symbol index in original or sorted table */
  unsigned key;        /* reversed prefix code */
  int step;            /* step size to replicate values in current table */
  int table_size;      /* size of current table */
  int sorted[18];      /* symbols sorted by code length */
  /* offsets in sorted table for each length */
  int offset[BROTLI_HUFFMAN_MAX_CODE_LENGTH_CODE_LENGTH + 1];
  int bits;
  int bits_count;

  /* generate offsets into sorted symbol table by code length */
  symbol = -1;
  bits = 1;
  BROTLI_REPEAT(BROTLI_HUFFMAN_MAX_CODE_LENGTH_CODE_LENGTH, {
    symbol += count[bits];
    offset[bits] = symbol;
    bits++;
  });
  /* Symbols with code length 0 are placed after all other symbols. */
  offset[0] = 17;

  /* sort symbols by length, by symbol order within each length */
  symbol = 18;
  do {
    BROTLI_REPEAT(6, {
      symbol--;
      sorted[offset[code_lengths[symbol]]--] = symbol;
    });
  } while (symbol != 0);

  table_size = 1 << BROTLI_HUFFMAN_MAX_CODE_LENGTH_CODE_LENGTH;

  /* Special case: all symbols but one have 0 code length. */
  if (offset[0] == 0) {
    code.bits = 0;
    code.value = (uint16_t)sorted[0];
    for (key = 0; key < table_size; ++key) {
      table[key] = code;
    }
    return;
  }

  /* fill in table */
  key = 0;
  symbol = 0;
  bits = 1;
  step = 2;
  do {
    code.bits = (uint8_t)bits;
    for (bits_count = count[bits]; bits_count != 0; --bits_count) {
      code.value = (uint16_t)sorted[symbol++];
      ReplicateValue(&table[key], step, table_size, code);
      key = GetNextKey(key, bits);
    }
    step <<= 1;
  } while (++bits <= BROTLI_HUFFMAN_MAX_CODE_LENGTH_CODE_LENGTH);
}

int BrotliBuildHuffmanTable(HuffmanCode* root_table,
                            int root_bits,
                            const uint16_t* const symbol_lists,
                            uint16_t *count) {
  HuffmanCode code;    /* current table entry */
  HuffmanCode* table;  /* next available space in table */
  int len;             /* current code length */
  int symbol;          /* symbol index in original or sorted table */
  unsigned key;        /* reversed prefix code */
  int step;            /* step size to replicate values in current table */
  unsigned low;        /* low bits for current root entry */
  unsigned mask;       /* mask for low bits */
  int table_bits;      /* key length of current table */
  int table_size;      /* size of current table */
  int total_size;      /* sum of root table size and 2nd level table sizes */
  int max_length = -1;
  int bits;
  int bits_count;

  while (symbol_lists[max_length] == 0xFFFF) max_length--;
  max_length += BROTLI_HUFFMAN_MAX_CODE_LENGTH + 1;

  table = root_table;
  table_bits = root_bits;
  table_size = 1 << table_bits;
  total_size = table_size;

  /* fill in root table */
  /* let's reduce the table size to a smaller size if possible, and */
  /* create the repetitions by memcpy if possible in the coming loop */
  if (table_bits > max_length) {
    table_bits = max_length;
    table_size = 1 << table_bits;
  }
  key = 0;
  bits = 1;
  step = 2;
  do {
    code.bits = (uint8_t)bits;
    symbol = bits - (BROTLI_HUFFMAN_MAX_CODE_LENGTH + 1);
    for (bits_count = count[bits]; bits_count != 0; --bits_count) {
      symbol = symbol_lists[symbol];
      code.value = (uint16_t)symbol;
      ReplicateValue(&table[key], step, table_size, code);
      key = GetNextKey(key, bits);
    }
    step <<= 1;
  } while (++bits <= table_bits);

  /* if root_bits != table_bits we only created one fraction of the */
  /* table, and we need to replicate it now. */
  while (total_size != table_size) {
    memcpy(&table[table_size], &table[0],
           (size_t)table_size * sizeof(table[0]));
    table_size <<= 1;
  }

  /* fill in 2nd level tables and add pointers to root table */
  mask = (unsigned)(total_size - 1);
  low = (unsigned)-1;
  for (len = root_bits + 1, step = 2; len <= max_length; ++len, step <<= 1) {
    symbol = len - (BROTLI_HUFFMAN_MAX_CODE_LENGTH + 1);
    for (; count[len] != 0; --count[len]) {
      if ((key & mask) != low) {
        table += table_size;
        table_bits = NextTableBitSize(count, len, root_bits);
        table_size = 1 << table_bits;
        total_size += table_size;
        low = key & mask;
        root_table[low].bits = (uint8_t)(table_bits + root_bits);
        root_table[low].value = (uint16_t)(
            ((size_t)(table - root_table)) - low);
      }
      code.bits = (uint8_t)(len - root_bits);
      symbol = symbol_lists[symbol];
      code.value = (uint16_t)symbol;
      ReplicateValue(&table[key >> root_bits], step, table_size, code);
      key = GetNextKey(key, len);
    }
  }
  return total_size;
}

int BrotliBuildSimpleHuffmanTable(HuffmanCode* table,
                                  int root_bits,
                                  uint16_t *val,
                                  uint32_t num_symbols) {
  int table_size = 1;
  const int goal_size = 1 << root_bits;
  switch (num_symbols) {
    case 0:
      table[0].bits = 0;
      table[0].value = val[0];
      break;
    case 1:
      table[0].bits = 1;
      table[1].bits = 1;
      if (val[1] > val[0]) {
        table[0].value = val[0];
        table[1].value = val[1];
      } else {
        table[0].value = val[1];
        table[1].value = val[0];
      }
      table_size = 2;
      break;
    case 2:
      table[0].bits = 1;
      table[0].value = val[0];
      table[2].bits = 1;
      table[2].value = val[0];
      if (val[2] > val[1]) {
        table[1].value = val[1];
        table[3].value = val[2];
      } else {
        table[1].value = val[2];
        table[3].value = val[1];
      }
      table[1].bits = 2;
      table[3].bits = 2;
      table_size = 4;
      break;
    case 3:
      {
        int i, k;
        for (i = 0; i < 3; ++i) {
          for (k = i + 1; k < 4; ++k) {
            if (val[k] < val[i]) {
              uint16_t t = val[k];
              val[k] = val[i];
              val[i] = t;
            }
          }
        }
        for (i = 0; i < 4; ++i) {
          table[i].bits = 2;
        }
        table[0].value = val[0];
        table[2].value = val[1];
        table[1].value = val[2];
        table[3].value = val[3];
        table_size = 4;
      }
      break;
    case 4:
      {
        int i;
        if (val[3] < val[2]) {
          uint16_t t = val[3];
          val[3] = val[2];
          val[2] = t;
        }
        for (i = 0; i < 7; ++i) {
          table[i].value = val[0];
          table[i].bits = (uint8_t)(1 + (i & 1));
        }
        table[1].value = val[1];
        table[3].value = val[2];
        table[5].value = val[1];
        table[7].value = val[3];
        table[3].bits = 3;
        table[7].bits = 3;
        table_size = 8;
      }
      break;
  }
  while (table_size != goal_size) {
    memcpy(&table[table_size], &table[0],
           (size_t)table_size * sizeof(table[0]));
    table_size <<= 1;
  }
  return goal_size;
}

void BrotliHuffmanTreeGroupInit(HuffmanTreeGroup* group, int alphabet_size,
                                int ntrees) {
  /* Pack two mallocs into one */
  const size_t code_size =
      sizeof(HuffmanCode) * (size_t)(ntrees * BROTLI_HUFFMAN_MAX_TABLE_SIZE);
  const size_t htree_size = sizeof(HuffmanCode*) * (size_t)ntrees;
  char *p = (char*)malloc(code_size + htree_size);
  group->alphabet_size = (int16_t)alphabet_size;
  group->num_htrees = (int16_t)ntrees;
  group->codes = (HuffmanCode*)p;
  group->htrees = (HuffmanCode**)(p + code_size);
}

void BrotliHuffmanTreeGroupRelease(HuffmanTreeGroup* group) {
  if (group->codes) {
    free(group->codes);
  }
}

#if defined(__cplusplus) || defined(c_plusplus)
}    /* extern "C" */
#endif
