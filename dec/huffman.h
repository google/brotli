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

#ifndef BROTLI_DEC_HUFFMAN_H_
#define BROTLI_DEC_HUFFMAN_H_

#include "./types.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#define BROTLI_HUFFMAN_MAX_CODE_LENGTH 15

/* For current format this constant equals to kNumInsertAndCopyCodes */
#define BROTLI_HUFFMAN_MAX_CODE_LENGTHS_SIZE 704

/* Maximum possible Huffman table size for an alphabet size of 704, max code
 * length 15 and root table bits 8. */
#define BROTLI_HUFFMAN_MAX_TABLE_SIZE 1080

#define BROTLI_HUFFMAN_MAX_CODE_LENGTH_CODE_LENGTH 5

typedef struct {
  uint8_t bits;     /* number of bits used for this symbol */
  uint16_t value;   /* symbol value or table offset */
} HuffmanCode;


/* Builds Huffman lookup table assuming code lengths are in symbol order. */
void BrotliBuildCodeLengthsHuffmanTable(HuffmanCode* root_table,
                                        const uint8_t* const code_lengths,
                                        uint16_t *count);

/* Builds Huffman lookup table assuming code lengths are in symbol order. */
/* Returns size of resulting table. */
int BrotliBuildHuffmanTable(HuffmanCode* root_table,
                            int root_bits,
                            const uint16_t* const symbol_lists,
                            uint16_t *count_arg);

int BrotliBuildSimpleHuffmanTable(HuffmanCode* table,
                                  int root_bits,
                                  uint16_t *symbols,
                                  uint32_t num_symbols);

/* Contains a collection of huffman trees with the same alphabet size. */
typedef struct {
  HuffmanCode** htrees;
  HuffmanCode* codes;
  int16_t alphabet_size;
  int16_t num_htrees;
} HuffmanTreeGroup;

void BrotliHuffmanTreeGroupInit(HuffmanTreeGroup* group,
                                int alphabet_size, int ntrees);
void BrotliHuffmanTreeGroupRelease(HuffmanTreeGroup* group);

#if defined(__cplusplus) || defined(c_plusplus)
}    /* extern "C" */
#endif

#endif  /* BROTLI_DEC_HUFFMAN_H_ */
