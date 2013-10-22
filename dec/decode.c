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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "./bit_reader.h"
#include "./context.h"
#include "./decode.h"
#include "./huffman.h"
#include "./prefix.h"
#include "./safe_malloc.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#ifdef BROTLI_DECODE_DEBUG
#define BROTLI_LOG_UINT(name)                                    \
  printf("[%s] %s = %lu\n", __func__, #name, (unsigned long)name)
#define BROTLI_LOG_ARRAY_INDEX(array_name, idx)                  \
  printf("[%s] %s[%lu] = %lu\n", __func__, #array_name, \
         (unsigned long)idx, (unsigned long)array_name[idx])
#else
#define BROTLI_LOG_UINT(name)
#define BROTLI_LOG_ARRAY_INDEX(array_name, idx)
#endif

static const int kDefaultCodeLength = 8;
static const int kCodeLengthLiterals = 16;
static const int kCodeLengthRepeatCode = 16;
static const int kCodeLengthExtraBits[3] = { 2, 3, 7 };
static const int kCodeLengthRepeatOffsets[3] = { 3, 3, 11 };

static const int kNumLiteralCodes = 256;
static const int kNumInsertAndCopyCodes = 704;
static const int kNumBlockLengthCodes = 26;

#define CODE_LENGTH_CODES 19
static const uint8_t kCodeLengthCodeOrder[CODE_LENGTH_CODES] = {
  17, 18, 0, 1, 2, 3, 4, 5, 16, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
};

#define NUM_DISTANCE_SHORT_CODES 16
static const int kDistanceShortCodeIndexOffset[NUM_DISTANCE_SHORT_CODES] = {
  3, 2, 1, 0, 3, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2
};

static const int kDistanceShortCodeValueOffset[NUM_DISTANCE_SHORT_CODES] = {
  0, 0, 0, 0, -1, 1, -2, 2, -3, 3, -1, 1, -2, 2, -3, 3
};

static int DecodeSize(BrotliBitReader* br, size_t* len) {
  int size_bytes = BrotliReadBits(br, 3);
  int i = 0;
  *len = 0;
  for (; i < size_bytes; ++i) {
    *len |= BrotliReadBits(br, 8) << (i * 8);
  }
  return !br->error_;
}

static int DecodeMetaBlockLength(int input_size_bits,
                                 size_t remaining_length,
                                 BrotliBitReader* br,
                                 size_t* meta_block_length) {
  if (BrotliReadBits(br, 1)) {
    *meta_block_length = remaining_length;
    return 1;
  } else {
    int shift = 0;
    *meta_block_length = 0;
    while (input_size_bits > 0) {
      *meta_block_length |= BrotliReadBits(br, 8) << shift;
      input_size_bits -= 8;
      shift += 8;
    }
    if (input_size_bits > 0) {
      *meta_block_length |= BrotliReadBits(br, input_size_bits) << shift;
    }
    ++(*meta_block_length);
    return !br->error_;
  }
}

// Decodes the next Huffman code from bit-stream.
// FillBitWindow(br) needs to be called at minimum every second call
// to ReadSymbol, in order to pre-fetch enough bits.
static BROTLI_INLINE int ReadSymbol(const HuffmanTree* tree,
                                    BrotliBitReader* br) {
  if (tree->fixed_bit_length_ > 0) {
    return BrotliReadBits(br, tree->fixed_bit_length_);
  } else {
    const HuffmanTreeNode* node = tree->root_;
    uint32_t bits = BrotliPrefetchBits(br);
    int bitpos = br->bit_pos_;
    // Check if we find the bit combination from the Huffman lookup table.
    const int lut_ix = bits & (HUFF_LUT - 1);
    const int lut_bits = tree->lut_bits_[lut_ix];
    if (lut_bits <= HUFF_LUT_BITS) {
      BrotliSetBitPos(br, bitpos + lut_bits);
      return tree->lut_symbol_[lut_ix];
    }
    node += tree->lut_jump_[lut_ix];
    bitpos += HUFF_LUT_BITS;
    bits >>= HUFF_LUT_BITS;

    // Decode the value from a binary tree.
    assert(node != NULL);
    do {
      node = HuffmanTreeNextNode(node, bits & 1);
      bits >>= 1;
      ++bitpos;
    } while (HuffmanTreeNodeIsNotLeaf(node));
    BrotliSetBitPos(br, bitpos);
    return node->symbol_;
  }
}

static void PrintIntVector(const int* v, int len) {
  while (len-- > 0) printf(" %d", *v++);
  printf("\n");
}

static int ReadHuffmanCodeLengths(
    const int* code_length_code_lengths,
    int num_symbols, int* code_lengths,
    BrotliBitReader* br) {
  int ok = 0;
  int symbol;
  int max_symbol;
  int decode_number_of_code_length_codes;
  int prev_code_len = kDefaultCodeLength;
  HuffmanTree tree;

  if (!BrotliHuffmanTreeBuildImplicit(&tree, code_length_code_lengths,
                                      CODE_LENGTH_CODES)) {
    printf("[ReadHuffmanCodeLengths] Building code length tree failed: ");
    PrintIntVector(code_length_code_lengths, CODE_LENGTH_CODES);
    return 0;
  }

  decode_number_of_code_length_codes = BrotliReadBits(br, 1);
  BROTLI_LOG_UINT(decode_number_of_code_length_codes);
  if (decode_number_of_code_length_codes) {
    const int length_nbits = 2 + 2 * BrotliReadBits(br, 3);
    max_symbol = 2 + BrotliReadBits(br, length_nbits);
    BROTLI_LOG_UINT(length_nbits);
    if (max_symbol > num_symbols) {
      printf("[ReadHuffmanCodeLengths] max_symbol > num_symbols (%d vs %d)\n",
             max_symbol, num_symbols);
      goto End;
    }
  } else {
    max_symbol = num_symbols;
  }
  BROTLI_LOG_UINT(max_symbol);

  symbol = 0;
  while (symbol < num_symbols) {
    int code_len;
    if (max_symbol-- == 0) break;
    BrotliFillBitWindow(br);
    code_len = ReadSymbol(&tree, br);
    BROTLI_LOG_UINT(symbol);
    BROTLI_LOG_UINT(code_len);
    if (code_len < kCodeLengthLiterals) {
      code_lengths[symbol++] = code_len;
      if (code_len != 0) prev_code_len = code_len;
    } else {
      const int use_prev = (code_len == kCodeLengthRepeatCode);
      const int slot = code_len - kCodeLengthLiterals;
      const int extra_bits = kCodeLengthExtraBits[slot];
      const int repeat_offset = kCodeLengthRepeatOffsets[slot];
      const int length = use_prev ? prev_code_len : 0;
      int repeat = BrotliReadBits(br, extra_bits) + repeat_offset;
      BROTLI_LOG_UINT(repeat);
      BROTLI_LOG_UINT(length);
      if (symbol + repeat > num_symbols) {
        printf("[ReadHuffmanCodeLengths] symbol + repeat > num_symbols "
               "(%d + %d vs %d)\n", symbol, repeat, num_symbols);
        goto End;
      } else {
        while (repeat-- > 0) {
          code_lengths[symbol++] = length;
        }
      }
    }
  }
  while (symbol < num_symbols) code_lengths[symbol++] = 0;
  ok = 1;

 End:
  BrotliHuffmanTreeRelease(&tree);
  return ok;
}

static const int64_t kUnitInterval = 1LL<<30;

static int RepairHuffmanCodeLengths(int num_symbols, int* code_lengths) {
  int i;
  int64_t space = kUnitInterval;
  int max_length = 0;
  for(i = 0; i < num_symbols; i++)
    if (code_lengths[i] != 0) {
      if (code_lengths[i] > max_length)
        max_length = code_lengths[i];
      space -= kUnitInterval >> code_lengths[i];
    }
  // The code which contains one symbol of length one cannot be made optimal.
  if (max_length == 1)
    return 1;
  if (space < 0) {
    int count_longest = 0;
    int new_length = max_length;
    for(i = 0; i < num_symbols; i++) {
      if (code_lengths[i] == max_length)
        count_longest++;
    }
    // Substitute all longest codes with sufficiently longer ones, so that all
    // code words fit into the unit interval. Leftover space will be
    // redistributed later.
    space += count_longest * (kUnitInterval >> max_length);
    if (space < 0)
      return 0;
    while (space < count_longest * (kUnitInterval >> new_length))
      new_length++;
    space -= count_longest * (kUnitInterval >> new_length);
    for(i = 0; i < num_symbols; i++) {
      if (code_lengths[i] == max_length)
        code_lengths[i] = new_length;
    }
  }

  while (space > 0) {
    // Redistribute leftover space in an approximation of a uniform fashion.
    for(i = 0; i < num_symbols; i++) {
      if (code_lengths[i] > 1 && space >= (kUnitInterval >> code_lengths[i])) {
        space -= kUnitInterval >> code_lengths[i];
        code_lengths[i]--;
      }
      if (space == 0)
        break;
    }
  }
  return 1;
}

static int ReadHuffmanCode(int alphabet_size,
                           HuffmanTree* tree,
                           BrotliBitReader* br) {
  int ok = 0;
  const int simple_code = BrotliReadBits(br, 1);
  BROTLI_LOG_UINT(simple_code);

  if (simple_code) {  // Read symbols, codes & code lengths directly.
    int symbols[2] = { 0 };
    int codes[2];
    int code_lengths[2];
    const int num_symbols = BrotliReadBits(br, 1) + 1;
    const int first_symbol_len_code = BrotliReadBits(br, 1);
    // The first code is either 1 bit or 8 bit code.
    symbols[0] = BrotliReadBits(br, (first_symbol_len_code == 0) ? 1 : 8);
    codes[0] = 0;
    code_lengths[0] = num_symbols - 1;
    // The second code (if present), is always 8 bit long.
    if (num_symbols == 2) {
      symbols[1] = BrotliReadBits(br, 8);
      codes[1] = 1;
      code_lengths[1] = num_symbols - 1;
    }
    BROTLI_LOG_UINT(num_symbols);
    BROTLI_LOG_UINT(first_symbol_len_code);
    BROTLI_LOG_UINT(symbols[0]);
    BROTLI_LOG_UINT(symbols[1]);
    ok = BrotliHuffmanTreeBuildExplicit(tree, code_lengths, codes, symbols,
                                        alphabet_size, num_symbols);
    if (!ok) {
      printf("[ReadHuffmanCode] HuffmanTreeBuildExplicit failed: ");
      PrintIntVector(code_lengths, num_symbols);
    }
  } else {  // Decode Huffman-coded code lengths.
    int* code_lengths = NULL;
    int i;
    int code_length_code_lengths[CODE_LENGTH_CODES] = { 0 };
    const int num_codes = BrotliReadBits(br, 4) + 4;
    BROTLI_LOG_UINT(num_codes);
    if (num_codes > CODE_LENGTH_CODES) {
      return 0;
    }

    code_lengths =
        (int*)BrotliSafeMalloc((uint64_t)alphabet_size, sizeof(*code_lengths));
    if (code_lengths == NULL) {
      return 0;
    }

    for (i = 0; i < num_codes; ++i) {
      int code_len_idx = kCodeLengthCodeOrder[i];
      code_length_code_lengths[code_len_idx] = BrotliReadBits(br, 3);
      BROTLI_LOG_ARRAY_INDEX(code_length_code_lengths, code_len_idx);
    }
    ok = ReadHuffmanCodeLengths(code_length_code_lengths, alphabet_size,
                                code_lengths, br) &&
         RepairHuffmanCodeLengths(alphabet_size, code_lengths);
    if (ok) {
      ok = BrotliHuffmanTreeBuildImplicit(tree, code_lengths, alphabet_size);
      if (!ok) {
        printf("[ReadHuffmanCode] HuffmanTreeBuildImplicit failed: ");
        PrintIntVector(code_lengths, alphabet_size);
      }
    }
    free(code_lengths);
  }
  ok = ok && !br->error_;
  if (!ok) {
    return 0;
  }
  return 1;
}

static int ReadCopyDistance(const HuffmanTree* tree,
                            int num_direct_codes,
                            int postfix_bits,
                            uint32_t postfix_mask,
                            BrotliBitReader* br) {
  int code;
  int nbits;
  int postfix;
  int offset;
  BrotliFillBitWindow(br);
  code = ReadSymbol(tree, br);
  if (code < num_direct_codes) {
    return code;
  }
  code -= num_direct_codes;
  postfix = code & postfix_mask;
  code >>= postfix_bits;
  nbits = (code >> 1) + 1;
  offset = ((2 + (code & 1)) << nbits) - 4;
  return (num_direct_codes +
          ((offset + BrotliReadBits(br, nbits)) << postfix_bits) +
          postfix);
}

static int ReadBlockLength(const HuffmanTree* tree, BrotliBitReader* br) {
  int code;
  int nbits;
  BrotliFillBitWindow(br);
  code = ReadSymbol(tree, br);
  nbits = kBlockLengthPrefixCode[code].nbits;
  return kBlockLengthPrefixCode[code].offset + BrotliReadBits(br, nbits);
}

static void ReadInsertAndCopy(const HuffmanTree* tree,
                              int* insert_len,
                              int* copy_len,
                              int* copy_dist,
                              BrotliBitReader* br) {
  int code;
  int range_idx;
  int insert_code;
  int copy_code;
  BrotliFillBitWindow(br);
  code = ReadSymbol(tree, br);
  range_idx = code >> 6;
  if (range_idx >= 2) {
    range_idx -= 2;
    *copy_dist = -1;
  } else {
    *copy_dist = 0;
  }
  insert_code = (kInsertRangeLut[range_idx] << 3) + ((code >> 3) & 7);
  copy_code = (kCopyRangeLut[range_idx] << 3) + (code & 7);
  *insert_len =
      kInsertLengthPrefixCode[insert_code].offset +
      BrotliReadBits(br, kInsertLengthPrefixCode[insert_code].nbits);
  *copy_len =
      kCopyLengthPrefixCode[copy_code].offset +
      BrotliReadBits(br, kCopyLengthPrefixCode[copy_code].nbits);
}

static int TranslateShortCodes(int code, int* ringbuffer, size_t* index) {
  int val;
  if (code < NUM_DISTANCE_SHORT_CODES) {
    int index_offset = kDistanceShortCodeIndexOffset[code];
    int value_offset = kDistanceShortCodeValueOffset[code];
    val = ringbuffer[(*index + index_offset) & 3] + value_offset;
  } else {
    val = code - NUM_DISTANCE_SHORT_CODES + 1;
  }
  if (code > 0) {
    ringbuffer[*index & 3] = val;
    ++(*index);
  }
  return val;
}

static void MoveToFront(uint8_t* v, uint8_t index) {
  uint8_t value = v[index];
  uint8_t i = index;
  for (; i; --i) v[i] = v[i - 1];
  v[0] = value;
}

static void InverseMoveToFrontTransform(uint8_t* v, int v_len) {
  uint8_t mtf[256];
  int i;
  for (i = 0; i < 256; ++i) {
    mtf[i] = i;
  }
  for (i = 0; i < v_len; ++i) {
    uint8_t index = v[i];
    v[i] = mtf[index];
    if (index) MoveToFront(mtf, index);
  }
}

// Contains a collection of huffman trees with the same alphabet size.
typedef struct {
  int alphabet_size;
  int num_htrees;
  HuffmanTree* htrees;
} HuffmanTreeGroup;

static void HuffmanTreeGroupInit(HuffmanTreeGroup* group, int alphabet_size,
                                 int ntrees) {
  group->alphabet_size = alphabet_size;
  group->num_htrees = ntrees;
  group->htrees = (HuffmanTree*)malloc(sizeof(HuffmanTree) * ntrees);
}

static void HuffmanTreeGroupRelease(HuffmanTreeGroup* group) {
  int i;
  for (i = 0; i < group->num_htrees; ++i) {
    BrotliHuffmanTreeRelease(&group->htrees[i]);
  }
  free(group->htrees);
}

static int HuffmanTreeGroupDecode(HuffmanTreeGroup* group,
                                  BrotliBitReader* br) {
  int i;
  for (i = 0; i < group->num_htrees; ++i) {
    ReadHuffmanCode(group->alphabet_size, &group->htrees[i], br);
  }
  return 1;
}

static int DecodeContextMap(int num_block_types,
                            int stream_type,
                            int* context_mode,
                            int* contexts_per_block,
                            int* num_htrees,
                            uint8_t** context_map,
                            BrotliBitReader* br) {
  int context_map_size;
  int use_context = BrotliReadBits(br, 1);
  if (!use_context) {
    *context_mode = 0;
    *contexts_per_block = 1;
    *context_map = NULL;
    *num_htrees = num_block_types;
    return 1;
  }
  switch (stream_type) {
    case 0:
      *context_mode = BrotliReadBits(br, 4);
      *contexts_per_block = NumContexts(*context_mode);
      break;
    case 2:
      *context_mode = 1;
      *contexts_per_block = 4;
      break;
  }
  context_map_size = *contexts_per_block * num_block_types;
  *num_htrees = BrotliReadBits(br, 8) + 1;

  BROTLI_LOG_UINT(*context_mode);
  BROTLI_LOG_UINT(context_map_size);
  BROTLI_LOG_UINT(*num_htrees);

  *context_map = (uint8_t*)malloc(context_map_size);
  if (*num_htrees <= 1) {
    memset(*context_map, 0, context_map_size);
    return 1;
  }

  if (*num_htrees == context_map_size) {
    int i;
    for (i = 0; i < context_map_size; ++i) {
      (*context_map)[i] = i;
    }
    return 1;
  }
  {
    HuffmanTree tree_index_htree;
    int use_rle_for_zeros = BrotliReadBits(br, 1);
    int max_run_length_prefix = 0;
    if (use_rle_for_zeros) {
      max_run_length_prefix = BrotliReadBits(br, 4) + 1;
    }
    ReadHuffmanCode(*num_htrees + max_run_length_prefix,
                    &tree_index_htree, br);
    if (use_rle_for_zeros) {
      int i;
      for (i = 0; i < context_map_size;) {
        int code;
        BrotliFillBitWindow(br);
        code = ReadSymbol(&tree_index_htree, br);
        if (code == 0) {
          (*context_map)[i] = 0;
          ++i;
        } else if (code <= max_run_length_prefix) {
          int reps = 1 + (1 << code) + BrotliReadBits(br, code);
          while (--reps) {
            (*context_map)[i] = 0;
            ++i;
          }
        } else {
          (*context_map)[i] = code - max_run_length_prefix;
          ++i;
        }
      }
    } else {
      int i;
      for (i = 0; i < context_map_size; ++i) {
        BrotliFillBitWindow(br);
        (*context_map)[i] = ReadSymbol(&tree_index_htree, br);
      }
    }
    BrotliHuffmanTreeRelease(&tree_index_htree);
  }
  if (BrotliReadBits(br, 1)) {
    InverseMoveToFrontTransform(*context_map, context_map_size);
  }
  return 1;
}

static BROTLI_INLINE void DecodeBlockType(const HuffmanTree* trees,
                                         int tree_type,
                                         int* block_types,
                                         int* ringbuffers,
                                         size_t* indexes,
                                         BrotliBitReader* br) {
  int* ringbuffer = ringbuffers + tree_type * 2;
  size_t* index = indexes + tree_type;
  int type_code = ReadSymbol(trees + tree_type, br);
  int block_type;
  if (type_code == 0) {
    block_type = ringbuffer[*index & 1];
  } else if (type_code == 1) {
    block_type = ringbuffer[(*index - 1) & 1] + 1;
  } else {
    block_type = type_code - 2;
  }
  block_types[tree_type] = block_type;
  ringbuffer[(*index) & 1] = block_type;
  ++(*index);
}

int BrotliDecompressedSize(size_t encoded_size,
                           const uint8_t* encoded_buffer,
                           size_t* decoded_size) {
  BrotliBitReader br;
  BrotliInitBitReader(&br, encoded_buffer, encoded_size);
  return DecodeSize(&br, decoded_size);
}

int BrotliDecompressBuffer(size_t encoded_size,
                           const uint8_t* encoded_buffer,
                           size_t* decoded_size,
                           uint8_t* decoded_buffer) {
  int ok = 1;
  int i;
  size_t pos = 0;
  uint8_t* data = decoded_buffer;
  int input_size_bits;
  // This ring buffer holds a few past copy distances that will be used by
  // some special distance codes.
  int dist_rb[4] = { 4, 11, 15, 16 };
  size_t dist_rb_idx = 0;
  HuffmanTreeGroup hgroup[3];
  BrotliBitReader br;
  BrotliInitBitReader(&br, encoded_buffer, encoded_size);

  ok = DecodeSize(&br, decoded_size);
  if (!ok) return 0;

  if (*decoded_size == 0) {
    return 1;
  }
  {
    size_t n = *decoded_size;
    input_size_bits = (n == (n &~ (n - 1))) ? -1 : 0;
    while (n) {
      ++input_size_bits;
      n >>= 1;
    }
  }

  BROTLI_LOG_UINT(*decoded_size);
  BROTLI_LOG_UINT(input_size_bits);

  while (pos < *decoded_size && ok) {
    size_t meta_block_len = 0;
    size_t meta_block_end;
    size_t block_length[3] = { 0 };
    int block_type[3] = { 0 };
    int num_block_types[3] = { 0 };
    int block_type_rb[6] = { 0, 1, 0, 1, 0, 1 };
    size_t block_type_rb_index[3] = { 0 };
    HuffmanTree block_type_trees[3];
    HuffmanTree block_len_trees[3];
    int distance_postfix_bits;
    int num_direct_distance_codes;
    uint32_t distance_postfix_mask;
    int num_distance_codes;
    uint8_t* context_map = NULL;
    int context_mode;
    int contexts_per_block;
    int num_literal_htrees;
    uint8_t* dist_context_map = NULL;
    int dist_context_mode;
    int dist_contexts_per_block;
    int num_dist_htrees;
    int context_offset = 0;
    uint8_t* context_map_slice = NULL;
    uint8_t literal_htree_index = 0;
    int dist_context_offset = 0;
    uint8_t* dist_context_map_slice = NULL;
    uint8_t dist_htree_index = 0;

    BROTLI_LOG_UINT(pos);
    if (!DecodeMetaBlockLength(input_size_bits, *decoded_size - pos,
                               &br, &meta_block_len)) {
      printf("Could not decode meta-block length.\n");
      ok = 0;
      goto End;
    }
    BROTLI_LOG_UINT(meta_block_len);
    meta_block_end = pos + meta_block_len;
    for (i = 0; i < 3; ++i) {
      block_type_trees[i].root_ = NULL;
      block_len_trees[i].root_ = NULL;
      if (BrotliReadBits(&br, 1)) {
        num_block_types[i] = BrotliReadBits(&br, 8) + 1;
        ReadHuffmanCode(num_block_types[i] + 2, &block_type_trees[i], &br);
        ReadHuffmanCode(kNumBlockLengthCodes, &block_len_trees[i], &br);
        block_length[i] = ReadBlockLength(&block_len_trees[i], &br);
        block_type_rb_index[i] = 1;
      } else {
        num_block_types[i] = 1;
        block_length[i] = meta_block_len;
      }
    }

    BROTLI_LOG_UINT(num_block_types[0]);
    BROTLI_LOG_UINT(num_block_types[1]);
    BROTLI_LOG_UINT(num_block_types[2]);
    BROTLI_LOG_UINT(block_length[0]);
    BROTLI_LOG_UINT(block_length[1]);
    BROTLI_LOG_UINT(block_length[2]);

    distance_postfix_bits = BrotliReadBits(&br, 2);
    num_direct_distance_codes = NUM_DISTANCE_SHORT_CODES +
        (BrotliReadBits(&br, 4) << distance_postfix_bits);
    distance_postfix_mask = (1 << distance_postfix_bits) - 1;
    num_distance_codes = (num_direct_distance_codes +
                          (48 << distance_postfix_bits));
    BROTLI_LOG_UINT(num_direct_distance_codes);
    BROTLI_LOG_UINT(distance_postfix_bits);

    DecodeContextMap(num_block_types[0], 0, &context_mode, &contexts_per_block,
                     &num_literal_htrees, &context_map, &br);

    DecodeContextMap(num_block_types[2], 2, &dist_context_mode,
                     &dist_contexts_per_block,
                     &num_dist_htrees, &dist_context_map, &br);

    HuffmanTreeGroupInit(&hgroup[0], kNumLiteralCodes, num_literal_htrees);
    HuffmanTreeGroupInit(&hgroup[1], kNumInsertAndCopyCodes,
                         num_block_types[1]);
    HuffmanTreeGroupInit(&hgroup[2], num_distance_codes, num_dist_htrees);

    for (i = 0; i < 3; ++i) {
      HuffmanTreeGroupDecode(&hgroup[i], &br);
    }

    context_map_slice = context_map;
    dist_context_map_slice = dist_context_map;

    while (pos < meta_block_end) {
      int insert_length;
      int copy_length;
      int distance_code;
      int distance;
      int j;
      if (block_length[1] == 0) {
        DecodeBlockType(block_type_trees, 1, block_type, block_type_rb,
                        block_type_rb_index, &br);
        block_length[1] = ReadBlockLength(&block_len_trees[1], &br);
      }
      --block_length[1];
      ReadInsertAndCopy(&hgroup[1].htrees[block_type[1]],
                        &insert_length, &copy_length, &distance_code, &br);
      BROTLI_LOG_UINT(insert_length);
      BROTLI_LOG_UINT(copy_length);
      BROTLI_LOG_UINT(distance_code);
      for (j = 0; j < insert_length; ++j) {
        if (block_length[0] == 0) {
          DecodeBlockType(block_type_trees, 0, block_type, block_type_rb,
                          block_type_rb_index, &br);
          block_length[0] = ReadBlockLength(&block_len_trees[0], &br);
          literal_htree_index = block_type[0];
          context_offset = block_type[0] * contexts_per_block;
          context_map_slice = context_map + context_offset;
        }
        --block_length[0];
        BrotliFillBitWindow(&br);
        // Figure out htree
        if (contexts_per_block > 1) {
          uint8_t prev_byte = pos > 0 ? data[pos - 1] : 0;
          uint8_t prev_byte2 = pos > 1 ? data[pos - 2] : 0;
          uint8_t prev_byte3 = pos > 2 ? data[pos - 3] : 0;
          uint8_t context = Context(prev_byte, prev_byte2, prev_byte3,
                                    context_mode);
          BROTLI_LOG_UINT(context);
          literal_htree_index = context_map_slice[context];
        }
        data[pos] = ReadSymbol(&hgroup[0].htrees[literal_htree_index], &br);
        BROTLI_LOG_UINT(literal_htree_index);
        BROTLI_LOG_ARRAY_INDEX(data, pos);
        ++pos;
      }
      if (br.error_) {
        printf("Read error after decoding literal sequence.\n");
        ok = 0;
        goto End;
      }

      if (pos == meta_block_end) break;

      if (distance_code < 0) {
        if (block_length[2] == 0) {
          DecodeBlockType(block_type_trees, 2, block_type, block_type_rb,
                          block_type_rb_index, &br);
          block_length[2] = ReadBlockLength(&block_len_trees[2], &br);
          dist_htree_index = block_type[2];
          dist_context_offset = block_type[2] * dist_contexts_per_block;
          dist_context_map_slice = dist_context_map + dist_context_offset;
        }
        --block_length[2];
        if (dist_contexts_per_block > 1) {
          uint8_t context = copy_length > 4 ? 3 : copy_length - 2;
          dist_htree_index = dist_context_map_slice[context];
        }
        distance_code = ReadCopyDistance(&hgroup[2].htrees[dist_htree_index],
                                         num_direct_distance_codes,
                                         distance_postfix_bits,
                                         distance_postfix_mask,
                                         &br);
        if (br.error_) {
          printf("Could not read copy distance.\n");
          ok = 0;
          goto End;
        }
      }

      // Convert the distance code to the actual distance by possibly looking
      // up past distnaces from the ringbuffer.
      distance = TranslateShortCodes(distance_code, dist_rb, &dist_rb_idx);
      BROTLI_LOG_UINT(distance);

      // Do the actual copy if it is valid.
      if (distance > 0 && pos >= (size_t)distance &&
          pos + copy_length <= *decoded_size) {
        int j;
        for (j = 0; j < copy_length; ++j) {
          data[pos + j] = data[pos + j - distance];
        }
        pos += copy_length;
      } else {
        printf("Invalid backward reference. pos: %lu distance: %d "
               "len: %d end: %lu\n", (unsigned long)pos, distance, copy_length,
               (unsigned long)*decoded_size);
        ok = 0;
        goto End;
      }
    }
 End:
    free(context_map);
    free(dist_context_map);
    for (i = 0; i < 3; ++i) {
      HuffmanTreeGroupRelease(&hgroup[i]);
      BrotliHuffmanTreeRelease(&block_type_trees[i]);
      BrotliHuffmanTreeRelease(&block_len_trees[i]);
    }
  }

  return ok;
}

#if defined(__cplusplus) || defined(c_plusplus)
}    // extern "C"
#endif
