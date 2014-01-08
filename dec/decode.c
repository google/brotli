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
#include "./huffman.h"
#include "./prefix.h"
#include "./safe_malloc.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#ifdef BROTLI_DECODE_DEBUG
#define BROTLI_LOG_UINT(name)                                    \
  printf("[%s] %s = %lu\n", __func__, #name, (unsigned long)(name))
#define BROTLI_LOG_ARRAY_INDEX(array_name, idx)                  \
  printf("[%s] %s[%lu] = %lu\n", __func__, #array_name, \
         (unsigned long)(idx), (unsigned long)array_name[idx])
#else
#define BROTLI_LOG_UINT(name)
#define BROTLI_LOG_ARRAY_INDEX(array_name, idx)
#endif

static const uint8_t kDefaultCodeLength = 8;
static const uint8_t kCodeLengthRepeatCode = 16;
static const int kNumLiteralCodes = 256;
static const int kNumInsertAndCopyCodes = 704;
static const int kNumBlockLengthCodes = 26;
static const int kLiteralContextBits = 6;
static const int kDistanceContextBits = 2;

#define CODE_LENGTH_CODES 18
static const uint8_t kCodeLengthCodeOrder[CODE_LENGTH_CODES] = {
  1, 2, 3, 4, 0, 17, 5, 6, 16, 7, 8, 9, 10, 11, 12, 13, 14, 15,
};

#define NUM_DISTANCE_SHORT_CODES 16
static const int kDistanceShortCodeIndexOffset[NUM_DISTANCE_SHORT_CODES] = {
  3, 2, 1, 0, 3, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2
};

static const int kDistanceShortCodeValueOffset[NUM_DISTANCE_SHORT_CODES] = {
  0, 0, 0, 0, -1, 1, -2, 2, -3, 3, -1, 1, -2, 2, -3, 3
};

static BROTLI_INLINE int DecodeWindowBits(BrotliBitReader* br) {
  if (BrotliReadBits(br, 1)) {
    return 17 + (int)BrotliReadBits(br, 3);
  } else {
    return 16;
  }
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

static void DecodeMetaBlockLength(BrotliBitReader* br,
                                  int* meta_block_length,
                                  int* input_end,
                                  int* is_uncompressed) {
  int size_nibbles;
  int i;
  *input_end = (int)BrotliReadBits(br, 1);
  *meta_block_length = 0;
  *is_uncompressed = 0;
  if (*input_end && BrotliReadBits(br, 1)) {
    return;
  }
  size_nibbles = (int)BrotliReadBits(br, 2) + 4;
  for (i = 0; i < size_nibbles; ++i) {
    *meta_block_length |= (int)BrotliReadBits(br, 4) << (i * 4);
  }
  ++(*meta_block_length);
  if (!*input_end) {
    *is_uncompressed = (int)BrotliReadBits(br, 1);
  }
}

/* Decodes the next Huffman code from bit-stream. */
static BROTLI_INLINE int ReadSymbol(const HuffmanTree* tree,
                                    BrotliBitReader* br) {
  uint32_t bits;
  uint32_t bitpos;
  int lut_ix;
  uint8_t lut_bits;
  const HuffmanTreeNode* node = tree->root_;
  BrotliFillBitWindow(br);
  bits = BrotliPrefetchBits(br);
  bitpos = br->bit_pos_;
  /* Check if we find the bit combination from the Huffman lookup table. */
  lut_ix = bits & (HUFF_LUT - 1);
  lut_bits = tree->lut_bits_[lut_ix];
  if (lut_bits <= HUFF_LUT_BITS) {
    BrotliSetBitPos(br, bitpos + lut_bits);
    return tree->lut_symbol_[lut_ix];
  }
  node += tree->lut_jump_[lut_ix];
  bitpos += HUFF_LUT_BITS;
  bits >>= HUFF_LUT_BITS;

  /* Decode the value from a binary tree. */
  assert(node != NULL);
  do {
    node = HuffmanTreeNextNode(node, bits & 1);
    bits >>= 1;
    ++bitpos;
  } while (HuffmanTreeNodeIsNotLeaf(node));
  BrotliSetBitPos(br, bitpos);
  return node->symbol_;
}

static void PrintUcharVector(const uint8_t* v, int len) {
  while (len-- > 0) printf(" %d", *v++);
  printf("\n");
}

static int ReadHuffmanCodeLengths(
    const uint8_t* code_length_code_lengths,
    int num_symbols, uint8_t* code_lengths,
    BrotliBitReader* br) {
  int ok = 0;
  int symbol;
  uint8_t prev_code_len = kDefaultCodeLength;
  int repeat = 0;
  uint8_t repeat_length = 0;
  int space = 32768;
  HuffmanTree tree;

  if (!BrotliHuffmanTreeBuildImplicit(&tree, code_length_code_lengths,
                                      CODE_LENGTH_CODES)) {
    printf("[ReadHuffmanCodeLengths] Building code length tree failed: ");
    PrintUcharVector(code_length_code_lengths, CODE_LENGTH_CODES);
    return 0;
  }

  if (!BrotliReadMoreInput(br)) {
    printf("[ReadHuffmanCodeLengths] Unexpected end of input.\n");
    return 0;
  }

  symbol = 0;
  while (symbol + repeat < num_symbols && space > 0) {
    uint8_t code_len;
    if (!BrotliReadMoreInput(br)) {
      printf("[ReadHuffmanCodeLengths] Unexpected end of input.\n");
      goto End;
    }
    code_len = (uint8_t)ReadSymbol(&tree, br);
    BROTLI_LOG_UINT(symbol);
    BROTLI_LOG_UINT(repeat);
    BROTLI_LOG_UINT(repeat_length);
    BROTLI_LOG_UINT(code_len);
    if ((code_len < kCodeLengthRepeatCode) ||
        (code_len == kCodeLengthRepeatCode && repeat_length == 0) ||
        (code_len > kCodeLengthRepeatCode && repeat_length > 0)) {
      while (repeat > 0) {
        code_lengths[symbol++] = repeat_length;
        --repeat;
      }
    }
    if (code_len < kCodeLengthRepeatCode) {
      code_lengths[symbol++] = code_len;
      if (code_len != 0) {
        prev_code_len = code_len;
        space -= 32768 >> code_len;
      }
    } else {
      const int extra_bits = code_len - 14;
      int i = repeat;
      if (repeat > 0) {
        repeat -= 2;
        repeat <<= extra_bits;
      }
      repeat += (int)BrotliReadBits(br, extra_bits) + 3;
      if (repeat + symbol > num_symbols) {
        goto End;
      }
      if (code_len == kCodeLengthRepeatCode) {
        repeat_length = prev_code_len;
        for (; i < repeat; ++i) {
          space -= 32768 >> repeat_length;
        }
      } else {
        repeat_length = 0;
      }
    }
  }
  if (space != 0) {
    printf("[ReadHuffmanCodeLengths] space = %d\n", space);
    goto End;
  }
  if (symbol + repeat > num_symbols) {
    printf("[ReadHuffmanCodeLengths] symbol + repeat > num_symbols "
           "(%d + %d vs %d)\n", symbol, repeat, num_symbols);
    goto End;
  }
  while (repeat-- > 0) code_lengths[symbol++] = repeat_length;
  while (symbol < num_symbols) code_lengths[symbol++] = 0;
  ok = 1;

 End:
  BrotliHuffmanTreeRelease(&tree);
  return ok;
}

static int ReadHuffmanCode(int alphabet_size,
                           HuffmanTree* tree,
                           BrotliBitReader* br) {
  int ok = 1;
  int simple_code_or_skip;
  uint8_t* code_lengths = NULL;

  code_lengths =
      (uint8_t*)BrotliSafeMalloc((uint64_t)alphabet_size,
                                 sizeof(*code_lengths));
  if (code_lengths == NULL) {
    return 0;
  }
  if (!BrotliReadMoreInput(br)) {
    printf("[ReadHuffmanCode] Unexpected end of input.\n");
    return 0;
  }
  /* simple_code_or_skip is used as follows:
     1 for simple code;
     0 for no skipping, 2 skips 2 code lengths, 3 skips 3 code lengths */
  simple_code_or_skip = (int)BrotliReadBits(br, 2);
  BROTLI_LOG_UINT(simple_code_or_skip);
  if (simple_code_or_skip == 1) {
    /* Read symbols, codes & code lengths directly. */
    int i;
    int max_bits_counter = alphabet_size - 1;
    int max_bits = 0;
    int symbols[4] = { 0 };
    const int num_symbols = (int)BrotliReadBits(br, 2) + 1;
    while (max_bits_counter) {
      max_bits_counter >>= 1;
      ++max_bits;
    }
    memset(code_lengths, 0, (size_t)alphabet_size);
    for (i = 0; i < num_symbols; ++i) {
      symbols[i] = (int)BrotliReadBits(br, max_bits) % alphabet_size;
      code_lengths[symbols[i]] = 2;
    }
    code_lengths[symbols[0]] = 1;
    switch (num_symbols) {
      case 1:
      case 3:
        break;
      case 2:
        code_lengths[symbols[1]] = 1;
        break;
      case 4:
        if (BrotliReadBits(br, 1)) {
          code_lengths[symbols[2]] = 3;
          code_lengths[symbols[3]] = 3;
        } else {
          code_lengths[symbols[0]] = 2;
        }
        break;
    }
    BROTLI_LOG_UINT(num_symbols);
  } else {  /* Decode Huffman-coded code lengths. */
    int i;
    uint8_t code_length_code_lengths[CODE_LENGTH_CODES] = { 0 };
    int space = 32;
    for (i = simple_code_or_skip;
         i < CODE_LENGTH_CODES && space > 0; ++i) {
      int code_len_idx = kCodeLengthCodeOrder[i];
      uint8_t v = (uint8_t)BrotliReadBits(br, 2);
      if (v == 1) {
        v = (uint8_t)BrotliReadBits(br, 1);
        if (v == 0) {
          v = 2;
        } else {
          v = (uint8_t)BrotliReadBits(br, 1);
          if (v == 0) {
            v = 1;
          } else {
            v = 5;
          }
        }
      } else if (v == 2) {
        v = 4;
      }
      code_length_code_lengths[code_len_idx] = v;
      BROTLI_LOG_ARRAY_INDEX(code_length_code_lengths, code_len_idx);
      if (v != 0) {
        space -= (32 >> v);
      }
    }
    ok = ReadHuffmanCodeLengths(code_length_code_lengths, alphabet_size,
                                code_lengths, br);
  }
  if (ok) {
    ok = BrotliHuffmanTreeBuildImplicit(tree, code_lengths, alphabet_size);
    if (!ok) {
      printf("[ReadHuffmanCode] HuffmanTreeBuildImplicit failed: ");
      PrintUcharVector(code_lengths, alphabet_size);
    }
  }
  free(code_lengths);
  return ok;
}

static int ReadCopyDistance(const HuffmanTree* tree,
                            int num_direct_codes,
                            int postfix_bits,
                            int postfix_mask,
                            BrotliBitReader* br) {
  int code;
  int nbits;
  int postfix;
  int offset;
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
          ((offset + (int)BrotliReadBits(br, nbits)) << postfix_bits) +
          postfix);
}

static int ReadBlockLength(const HuffmanTree* tree, BrotliBitReader* br) {
  int code;
  int nbits;
  code = ReadSymbol(tree, br);
  nbits = kBlockLengthPrefixCode[code].nbits;
  return kBlockLengthPrefixCode[code].offset + (int)BrotliReadBits(br, nbits);
}

static void ReadInsertAndCopy(const HuffmanTree* tree,
                              int* insert_len,
                              int* copy_len,
                              int* copy_dist,
                              BrotliBitReader* br) {
  int code;
  int range_idx;
  int insert_code;
  int insert_extra_bits;
  int copy_code;
  int copy_extra_bits;
  code = ReadSymbol(tree, br);
  range_idx = code >> 6;
  if (range_idx >= 2) {
    range_idx -= 2;
    *copy_dist = -1;
  } else {
    *copy_dist = 0;
  }
  insert_code = kInsertRangeLut[range_idx] + ((code >> 3) & 7);
  copy_code = kCopyRangeLut[range_idx] + (code & 7);
  *insert_len = kInsertLengthPrefixCode[insert_code].offset;
  insert_extra_bits = kInsertLengthPrefixCode[insert_code].nbits;
  if (insert_extra_bits > 0) {
    *insert_len += (int)BrotliReadBits(br, insert_extra_bits);
  }
  *copy_len = kCopyLengthPrefixCode[copy_code].offset;
  copy_extra_bits = kCopyLengthPrefixCode[copy_code].nbits;
  if (copy_extra_bits > 0) {
    *copy_len += (int)BrotliReadBits(br, copy_extra_bits);
  }
}

static int TranslateShortCodes(int code, int* ringbuffer, int index) {
  int val;
  if (code < NUM_DISTANCE_SHORT_CODES) {
    index += kDistanceShortCodeIndexOffset[code];
    index &= 3;
    val = ringbuffer[index] + kDistanceShortCodeValueOffset[code];
  } else {
    val = code - NUM_DISTANCE_SHORT_CODES + 1;
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
    mtf[i] = (uint8_t)i;
  }
  for (i = 0; i < v_len; ++i) {
    uint8_t index = v[i];
    v[i] = mtf[index];
    if (index) MoveToFront(mtf, index);
  }
}

/* Contains a collection of huffman trees with the same alphabet size. */
typedef struct {
  int alphabet_size;
  int num_htrees;
  HuffmanTree* htrees;
} HuffmanTreeGroup;

static void HuffmanTreeGroupInit(HuffmanTreeGroup* group, int alphabet_size,
                                 int ntrees) {
  int i;
  group->alphabet_size = alphabet_size;
  group->num_htrees = ntrees;
  group->htrees = (HuffmanTree*)malloc(sizeof(HuffmanTree) * (size_t)ntrees);
  for (i = 0; i < ntrees; ++i) {
    group->htrees[i].root_ = NULL;
  }
}

static void HuffmanTreeGroupRelease(HuffmanTreeGroup* group) {
  int i;
  for (i = 0; i < group->num_htrees; ++i) {
    BrotliHuffmanTreeRelease(&group->htrees[i]);
  }
  if (group->htrees) {
    free(group->htrees);
  }
}

static int HuffmanTreeGroupDecode(HuffmanTreeGroup* group,
                                  BrotliBitReader* br) {
  int i;
  for (i = 0; i < group->num_htrees; ++i) {
    if (!ReadHuffmanCode(group->alphabet_size, &group->htrees[i], br)) {
      return 0;
    }
  }
  return 1;
}

static int DecodeContextMap(int context_map_size,
                            int* num_htrees,
                            uint8_t** context_map,
                            BrotliBitReader* br) {
  int ok = 1;
  if (!BrotliReadMoreInput(br)) {
    printf("[DecodeContextMap] Unexpected end of input.\n");
    return 0;
  }
  *num_htrees = DecodeVarLenUint8(br) + 1;

  BROTLI_LOG_UINT(context_map_size);
  BROTLI_LOG_UINT(*num_htrees);

  *context_map = (uint8_t*)malloc((size_t)context_map_size);
  if (*context_map == 0) {
    return 0;
  }
  if (*num_htrees <= 1) {
    memset(*context_map, 0, (size_t)context_map_size);
    return 1;
  }

  {
    HuffmanTree tree_index_htree;
    int use_rle_for_zeros = (int)BrotliReadBits(br, 1);
    int max_run_length_prefix = 0;
    int i;
    if (use_rle_for_zeros) {
      max_run_length_prefix = (int)BrotliReadBits(br, 4) + 1;
    }
    if (!ReadHuffmanCode(*num_htrees + max_run_length_prefix,
                         &tree_index_htree, br)) {
      return 0;
    }
    for (i = 0; i < context_map_size;) {
      int code;
      if (!BrotliReadMoreInput(br)) {
        printf("[DecodeContextMap] Unexpected end of input.\n");
        ok = 0;
        goto End;
      }
      code = ReadSymbol(&tree_index_htree, br);
      if (code == 0) {
        (*context_map)[i] = 0;
        ++i;
      } else if (code <= max_run_length_prefix) {
        int reps = 1 + (1 << code) + (int)BrotliReadBits(br, code);
        while (--reps) {
          if (i >= context_map_size) {
            ok = 0;
            goto End;
          }
          (*context_map)[i] = 0;
          ++i;
        }
      } else {
        (*context_map)[i] = (uint8_t)(code - max_run_length_prefix);
        ++i;
      }
    }
   End:
    BrotliHuffmanTreeRelease(&tree_index_htree);
  }
  if (BrotliReadBits(br, 1)) {
    InverseMoveToFrontTransform(*context_map, context_map_size);
  }
  return ok;
}

static BROTLI_INLINE void DecodeBlockType(const int max_block_type,
                                          const HuffmanTree* trees,
                                          int tree_type,
                                          int* block_types,
                                          int* ringbuffers,
                                          int* indexes,
                                          BrotliBitReader* br) {
  int* ringbuffer = ringbuffers + tree_type * 2;
  int* index = indexes + tree_type;
  int type_code = ReadSymbol(trees + tree_type, br);
  int block_type;
  if (type_code == 0) {
    block_type = ringbuffer[*index & 1];
  } else if (type_code == 1) {
    block_type = ringbuffer[(*index - 1) & 1] + 1;
  } else {
    block_type = type_code - 2;
  }
  if (block_type >= max_block_type) {
    block_type -= max_block_type;
  }
  block_types[tree_type] = block_type;
  ringbuffer[(*index) & 1] = block_type;
  ++(*index);
}

/* Copy len bytes from src to dst. It can write up to ten extra bytes
   after the end of the copy.

   The main part of this loop is a simple copy of eight bytes at a time until
   we've copied (at least) the requested amount of bytes.  However, if dst and
   src are less than eight bytes apart (indicating a repeating pattern of
   length < 8), we first need to expand the pattern in order to get the correct
   results. For instance, if the buffer looks like this, with the eight-byte
   <src> and <dst> patterns marked as intervals:

      abxxxxxxxxxxxx
      [------]           src
        [------]         dst

   a single eight-byte copy from <src> to <dst> will repeat the pattern once,
   after which we can move <dst> two bytes without moving <src>:

      ababxxxxxxxxxx
      [------]           src
          [------]       dst

   and repeat the exercise until the two no longer overlap.

   This allows us to do very well in the special case of one single byte
   repeated many times, without taking a big hit for more general cases.

   The worst case of extra writing past the end of the match occurs when
   dst - src == 1 and len == 1; the last copy will read from byte positions
   [0..7] and write to [4..11], whereas it was only supposed to write to
   position 1. Thus, ten excess bytes.
*/
static BROTLI_INLINE void IncrementalCopyFastPath(
    uint8_t* dst, const uint8_t* src, int len) {
  if (src < dst) {
    while (dst - src < 8) {
      UNALIGNED_COPY64(dst, src);
      len -= (int)(dst - src);
      dst += dst - src;
    }
  }
  while (len > 0) {
    UNALIGNED_COPY64(dst, src);
    src += 8;
    dst += 8;
    len -= 8;
  }
}

int BrotliDecompressedSize(size_t encoded_size,
                           const uint8_t* encoded_buffer,
                           size_t* decoded_size) {
  BrotliMemInput memin;
  BrotliInput input = BrotliInitMemInput(encoded_buffer, encoded_size, &memin);
  BrotliBitReader br;
  int meta_block_len;
  int input_end;
  int is_uncompressed;
  if (!BrotliInitBitReader(&br, input)) {
    return 0;
  }
  DecodeWindowBits(&br);
  DecodeMetaBlockLength(&br, &meta_block_len, &input_end, &is_uncompressed);
  if (!input_end) {
    return 0;
  }
  *decoded_size = (size_t)meta_block_len;
  return 1;
}

int BrotliDecompressBuffer(size_t encoded_size,
                           const uint8_t* encoded_buffer,
                           size_t* decoded_size,
                           uint8_t* decoded_buffer) {
  BrotliMemInput memin;
  BrotliInput in = BrotliInitMemInput(encoded_buffer, encoded_size, &memin);
  BrotliMemOutput mout;
  BrotliOutput out = BrotliInitMemOutput(decoded_buffer, *decoded_size, &mout);
  int success = BrotliDecompress(in, out);
  *decoded_size = mout.pos;
  return success;
}

int BrotliDecompress(BrotliInput input, BrotliOutput output) {
  int ok = 1;
  int i;
  int pos = 0;
  int input_end = 0;
  int window_bits = 0;
  int max_backward_distance;
  int max_distance = 0;
  int ringbuffer_size;
  int ringbuffer_mask;
  uint8_t* ringbuffer;
  uint8_t* ringbuffer_end;
  /* This ring buffer holds a few past copy distances that will be used by */
  /* some special distance codes. */
  int dist_rb[4] = { 16, 15, 11, 4 };
  int dist_rb_idx = 0;
  /* The previous 2 bytes used for context. */
  uint8_t prev_byte1 = 0;
  uint8_t prev_byte2 = 0;
  HuffmanTreeGroup hgroup[3];
  BrotliBitReader br;

  /* 16 bytes would be enough, but we add some more slack for transforms */
  /* to work at the end of the ringbuffer. */
  static const int kRingBufferWriteAheadSlack = 128;

  static const int kMaxDictionaryWordLength = 0;

  if (!BrotliInitBitReader(&br, input)) {
    return 0;
  }

  /* Decode window size. */
  window_bits = DecodeWindowBits(&br);
  max_backward_distance = (1 << window_bits) - 16;

  ringbuffer_size = 1 << window_bits;
  ringbuffer_mask = ringbuffer_size - 1;
  ringbuffer = (uint8_t*)malloc((size_t)(ringbuffer_size +
                                         kRingBufferWriteAheadSlack +
                                         kMaxDictionaryWordLength));
  if (!ringbuffer) {
    ok = 0;
  }
  ringbuffer_end = ringbuffer + ringbuffer_size;

  while (!input_end && ok) {
    int meta_block_remaining_len = 0;
    int is_uncompressed;
    int block_length[3] = { 1 << 28, 1 << 28, 1 << 28 };
    int block_type[3] = { 0 };
    int num_block_types[3] = { 1, 1, 1 };
    int block_type_rb[6] = { 0, 1, 0, 1, 0, 1 };
    int block_type_rb_index[3] = { 0 };
    HuffmanTree block_type_trees[3];
    HuffmanTree block_len_trees[3];
    int distance_postfix_bits;
    int num_direct_distance_codes;
    int distance_postfix_mask;
    int num_distance_codes;
    uint8_t* context_map = NULL;
    uint8_t* context_modes = NULL;
    int num_literal_htrees;
    uint8_t* dist_context_map = NULL;
    int num_dist_htrees;
    int context_offset = 0;
    uint8_t* context_map_slice = NULL;
    uint8_t literal_htree_index = 0;
    int dist_context_offset = 0;
    uint8_t* dist_context_map_slice = NULL;
    uint8_t dist_htree_index = 0;
    int context_lookup_offset1 = 0;
    int context_lookup_offset2 = 0;
    uint8_t context_mode;

    for (i = 0; i < 3; ++i) {
      hgroup[i].num_htrees = 0;
      hgroup[i].htrees = NULL;
      block_type_trees[i].root_ = NULL;
      block_len_trees[i].root_ = NULL;
    }

    if (!BrotliReadMoreInput(&br)) {
      printf("[BrotliDecompress] Unexpected end of input.\n");
      ok = 0;
      goto End;
    }
    BROTLI_LOG_UINT(pos);
    DecodeMetaBlockLength(&br, &meta_block_remaining_len,
                          &input_end, &is_uncompressed);
    BROTLI_LOG_UINT(meta_block_remaining_len);
    if (meta_block_remaining_len == 0) {
      goto End;
    }
    if (is_uncompressed) {
      BrotliSetBitPos(&br, (br.bit_pos_ + 7) & (uint32_t)(~7UL));
      while (meta_block_remaining_len) {
        ringbuffer[pos & ringbuffer_mask] = (uint8_t)BrotliReadBits(&br, 8);
        if ((pos & ringbuffer_mask) == ringbuffer_mask) {
          if (BrotliWrite(output, ringbuffer, (size_t)ringbuffer_size) < 0) {
            ok = 0;
            goto End;
          }
        }
        ++pos;
        --meta_block_remaining_len;
      }
      goto End;
    }
    for (i = 0; i < 3; ++i) {
      block_type_trees[i].root_ = NULL;
      block_len_trees[i].root_ = NULL;
      num_block_types[i] = DecodeVarLenUint8(&br) + 1;
      if (num_block_types[i] >= 2) {
        if (!ReadHuffmanCode(
                num_block_types[i] + 2, &block_type_trees[i], &br) ||
            !ReadHuffmanCode(kNumBlockLengthCodes, &block_len_trees[i], &br)) {
          ok = 0;
          goto End;
        }
        block_length[i] = ReadBlockLength(&block_len_trees[i], &br);
        block_type_rb_index[i] = 1;
      }
    }

    BROTLI_LOG_UINT(num_block_types[0]);
    BROTLI_LOG_UINT(num_block_types[1]);
    BROTLI_LOG_UINT(num_block_types[2]);
    BROTLI_LOG_UINT(block_length[0]);
    BROTLI_LOG_UINT(block_length[1]);
    BROTLI_LOG_UINT(block_length[2]);

    if (!BrotliReadMoreInput(&br)) {
      printf("[BrotliDecompress] Unexpected end of input.\n");
      ok = 0;
      goto End;
    }
    distance_postfix_bits = (int)BrotliReadBits(&br, 2);
    num_direct_distance_codes = NUM_DISTANCE_SHORT_CODES +
        ((int)BrotliReadBits(&br, 4) << distance_postfix_bits);
    distance_postfix_mask = (1 << distance_postfix_bits) - 1;
    num_distance_codes = (num_direct_distance_codes +
                          (48 << distance_postfix_bits));
    context_modes = (uint8_t*)malloc((size_t)num_block_types[0]);
    if (context_modes == 0) {
      ok = 0;
      goto End;
    }
    for (i = 0; i < num_block_types[0]; ++i) {
      context_modes[i] = (uint8_t)(BrotliReadBits(&br, 2) << 1);
      BROTLI_LOG_ARRAY_INDEX(context_modes, i);
    }
    BROTLI_LOG_UINT(num_direct_distance_codes);
    BROTLI_LOG_UINT(distance_postfix_bits);

    if (!DecodeContextMap(num_block_types[0] << kLiteralContextBits,
                          &num_literal_htrees, &context_map, &br) ||
        !DecodeContextMap(num_block_types[2] << kDistanceContextBits,
                          &num_dist_htrees, &dist_context_map, &br)) {
      ok = 0;
      goto End;
    }

    HuffmanTreeGroupInit(&hgroup[0], kNumLiteralCodes, num_literal_htrees);
    HuffmanTreeGroupInit(&hgroup[1], kNumInsertAndCopyCodes,
                         num_block_types[1]);
    HuffmanTreeGroupInit(&hgroup[2], num_distance_codes, num_dist_htrees);

    for (i = 0; i < 3; ++i) {
      if (!HuffmanTreeGroupDecode(&hgroup[i], &br)) {
        ok = 0;
        goto End;
      }
    }

    context_map_slice = context_map;
    dist_context_map_slice = dist_context_map;
    context_mode = context_modes[block_type[0]];
    context_lookup_offset1 = kContextLookupOffsets[context_mode];
    context_lookup_offset2 = kContextLookupOffsets[context_mode + 1];

    while (meta_block_remaining_len > 0) {
      int insert_length;
      int copy_length;
      int distance_code;
      int distance;
      uint8_t context;
      int j;
      const uint8_t* copy_src;
      uint8_t* copy_dst;
      if (!BrotliReadMoreInput(&br)) {
        printf("[BrotliDecompress] Unexpected end of input.\n");
        ok = 0;
        goto End;
      }
      if (block_length[1] == 0) {
        DecodeBlockType(num_block_types[1],
                        block_type_trees, 1, block_type, block_type_rb,
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
        if (!BrotliReadMoreInput(&br)) {
          printf("[BrotliDecompress] Unexpected end of input.\n");
          ok = 0;
          goto End;
        }
        if (block_length[0] == 0) {
          DecodeBlockType(num_block_types[0],
                          block_type_trees, 0, block_type, block_type_rb,
                          block_type_rb_index, &br);
          block_length[0] = ReadBlockLength(&block_len_trees[0], &br);
          context_offset = block_type[0] << kLiteralContextBits;
          context_map_slice = context_map + context_offset;
          context_mode = context_modes[block_type[0]];
          context_lookup_offset1 = kContextLookupOffsets[context_mode];
          context_lookup_offset2 = kContextLookupOffsets[context_mode + 1];
        }
        context = (kContextLookup[context_lookup_offset1 + prev_byte1] |
                   kContextLookup[context_lookup_offset2 + prev_byte2]);
        BROTLI_LOG_UINT(context);
        literal_htree_index = context_map_slice[context];
        --block_length[0];
        prev_byte2 = prev_byte1;
        prev_byte1 = (uint8_t)ReadSymbol(&hgroup[0].htrees[literal_htree_index],
                                         &br);
        ringbuffer[pos & ringbuffer_mask] = prev_byte1;
        BROTLI_LOG_UINT(literal_htree_index);
        BROTLI_LOG_ARRAY_INDEX(ringbuffer, pos & ringbuffer_mask);
        if ((pos & ringbuffer_mask) == ringbuffer_mask) {
          if (BrotliWrite(output, ringbuffer, (size_t)ringbuffer_size) < 0) {
            ok = 0;
            goto End;
          }
        }
        ++pos;
      }
      meta_block_remaining_len -= insert_length;
      if (meta_block_remaining_len <= 0) break;

      if (distance_code < 0) {
        uint8_t context;
        if (!BrotliReadMoreInput(&br)) {
          printf("[BrotliDecompress] Unexpected end of input.\n");
          ok = 0;
          goto End;
        }
        if (block_length[2] == 0) {
          DecodeBlockType(num_block_types[2],
                          block_type_trees, 2, block_type, block_type_rb,
                          block_type_rb_index, &br);
          block_length[2] = ReadBlockLength(&block_len_trees[2], &br);
          dist_htree_index = (uint8_t)block_type[2];
          dist_context_offset = block_type[2] << kDistanceContextBits;
          dist_context_map_slice = dist_context_map + dist_context_offset;
        }
        --block_length[2];
        context = (uint8_t)(copy_length > 4 ? 3 : copy_length - 2);
        dist_htree_index = dist_context_map_slice[context];
        distance_code = ReadCopyDistance(&hgroup[2].htrees[dist_htree_index],
                                         num_direct_distance_codes,
                                         distance_postfix_bits,
                                         distance_postfix_mask,
                                         &br);
      }

      /* Convert the distance code to the actual distance by possibly looking */
      /* up past distnaces from the ringbuffer. */
      distance = TranslateShortCodes(distance_code, dist_rb, dist_rb_idx);
      if (distance < 0) {
        ok = 0;
        goto End;
      }
      if (distance_code > 0) {
        dist_rb[dist_rb_idx & 3] = distance;
        ++dist_rb_idx;
      }
      BROTLI_LOG_UINT(distance);

      if (pos < max_backward_distance &&
          max_distance != max_backward_distance) {
        max_distance = pos;
      } else {
        max_distance = max_backward_distance;
      }

      copy_dst = &ringbuffer[pos & ringbuffer_mask];

      if (distance > max_distance) {
        printf("Invalid backward reference. pos: %d distance: %d "
               "len: %d bytes left: %d\n", pos, distance, copy_length,
               meta_block_remaining_len);
        ok = 0;
        goto End;
      } else {
        if (copy_length > meta_block_remaining_len) {
          printf("Invalid backward reference. pos: %d distance: %d "
                 "len: %d bytes left: %d\n", pos, distance, copy_length,
                 meta_block_remaining_len);
          ok = 0;
          goto End;
        }

        copy_src = &ringbuffer[(pos - distance) & ringbuffer_mask];

#if (defined(__x86_64__) || defined(_M_X64))
        if (copy_src + copy_length <= ringbuffer_end &&
            copy_dst + copy_length < ringbuffer_end) {
          if (copy_length <= 16 && distance >= 8) {
            UNALIGNED_COPY64(copy_dst, copy_src);
            UNALIGNED_COPY64(copy_dst + 8, copy_src + 8);
          } else {
            IncrementalCopyFastPath(copy_dst, copy_src, copy_length);
          }
          pos += copy_length;
          meta_block_remaining_len -= copy_length;
          copy_length = 0;
        }
#endif

        for (j = 0; j < copy_length; ++j) {
          ringbuffer[pos & ringbuffer_mask] =
              ringbuffer[(pos - distance) & ringbuffer_mask];
          if ((pos & ringbuffer_mask) == ringbuffer_mask) {
            if (BrotliWrite(output, ringbuffer, (size_t)ringbuffer_size) < 0) {
              ok = 0;
              goto End;
            }
          }
          ++pos;
          --meta_block_remaining_len;
        }
      }

      /* When we get here, we must have inserted at least one literal and */
      /* made a copy of at least length two, therefore accessing the last 2 */
      /* bytes is valid. */
      prev_byte1 = ringbuffer[(pos - 1) & ringbuffer_mask];
      prev_byte2 = ringbuffer[(pos - 2) & ringbuffer_mask];
    }

    /* Protect pos from overflow, wrap it around at every GB of input data */
    pos &= 0x3fffffff;

 End:
    if (context_modes != 0) {
      free(context_modes);
    }
    if (context_map != 0) {
      free(context_map);
    }
    if (dist_context_map != 0) {
      free(dist_context_map);
    }
    for (i = 0; i < 3; ++i) {
      HuffmanTreeGroupRelease(&hgroup[i]);
      BrotliHuffmanTreeRelease(&block_type_trees[i]);
      BrotliHuffmanTreeRelease(&block_len_trees[i]);
    }
  }

  if (ringbuffer != 0) {
    if (BrotliWrite(output, ringbuffer, (size_t)(pos & ringbuffer_mask)) < 0) {
      ok = 0;
    }
    free(ringbuffer);
  }
  return ok;
}

#if defined(__cplusplus) || defined(c_plusplus)
}    /* extern "C" */
#endif
