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

   Utilities for building and looking up Huffman trees.
*/

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "./huffman.h"
#include "./safe_malloc.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#define NON_EXISTENT_SYMBOL (-1)
#define MAX_ALLOWED_CODE_LENGTH      15

static void TreeNodeInit(HuffmanTreeNode* const node) {
  node->children_ = -1;   /* means: 'unassigned so far' */
}

static int NodeIsEmpty(const HuffmanTreeNode* const node) {
  return (node->children_ < 0);
}

static int IsFull(const HuffmanTree* const tree) {
  return (tree->num_nodes_ == tree->max_nodes_);
}

static void AssignChildren(HuffmanTree* const tree,
                           HuffmanTreeNode* const node) {
  HuffmanTreeNode* const children = tree->root_ + tree->num_nodes_;
  node->children_ = (int)(children - node);
  assert(children - node == (int)(children - node));
  tree->num_nodes_ += 2;
  TreeNodeInit(children + 0);
  TreeNodeInit(children + 1);
}

static int TreeInit(HuffmanTree* const tree, int num_leaves) {
  assert(tree != NULL);
  tree->root_ = NULL;
  if (num_leaves == 0) return 0;
  /* We allocate maximum possible nodes in the tree at once. */
  /* Note that a Huffman tree is a full binary tree; and in a full binary */
  /* tree with L leaves, the total number of nodes N = 2 * L - 1. */
  tree->max_nodes_ = 2 * num_leaves - 1;
  assert(tree->max_nodes_ < (1 << 16));   /* limit for the lut_jump_ table */
  tree->root_ = (HuffmanTreeNode*)BrotliSafeMalloc((uint64_t)tree->max_nodes_,
                                                  sizeof(*tree->root_));
  if (tree->root_ == NULL) return 0;
  TreeNodeInit(tree->root_);  /* Initialize root. */
  tree->num_nodes_ = 1;
  memset(tree->lut_bits_, 255, sizeof(tree->lut_bits_));
  memset(tree->lut_jump_, 0, sizeof(tree->lut_jump_));
  return 1;
}

void BrotliHuffmanTreeRelease(HuffmanTree* const tree) {
  if (tree != NULL) {
    if (tree->root_ != NULL) {
      free(tree->root_);
    }
    tree->root_ = NULL;
    tree->max_nodes_ = 0;
    tree->num_nodes_ = 0;
  }
}

/* Utility: converts Huffman code lengths to corresponding Huffman codes. */
/* 'huff_codes' should be pre-allocated. */
/* Returns false in case of error (memory allocation, invalid codes). */
static int HuffmanCodeLengthsToCodes(const uint8_t* const code_lengths,
                                     int code_lengths_size,
                                     int* const huff_codes) {
  int symbol;
  int code_len;
  int code_length_hist[MAX_ALLOWED_CODE_LENGTH + 1] = { 0 };
  int curr_code;
  int next_codes[MAX_ALLOWED_CODE_LENGTH + 1] = { 0 };
  int max_code_length = 0;

  assert(code_lengths != NULL);
  assert(code_lengths_size > 0);
  assert(huff_codes != NULL);

  /* Calculate max code length. */
  for (symbol = 0; symbol < code_lengths_size; ++symbol) {
    if (code_lengths[symbol] > max_code_length) {
      max_code_length = code_lengths[symbol];
    }
  }
  if (max_code_length > MAX_ALLOWED_CODE_LENGTH) return 0;

  /* Calculate code length histogram. */
  for (symbol = 0; symbol < code_lengths_size; ++symbol) {
    ++code_length_hist[code_lengths[symbol]];
  }
  code_length_hist[0] = 0;

  /* Calculate the initial values of 'next_codes' for each code length. */
  /* next_codes[code_len] denotes the code to be assigned to the next symbol */
  /* of code length 'code_len'. */
  curr_code = 0;
  next_codes[0] = -1;  /* Unused, as code length = 0 implies */
                       /* code doesn't exist. */
  for (code_len = 1; code_len <= max_code_length; ++code_len) {
    curr_code = (curr_code + code_length_hist[code_len - 1]) << 1;
    next_codes[code_len] = curr_code;
  }

  /* Get symbols. */
  for (symbol = 0; symbol < code_lengths_size; ++symbol) {
    if (code_lengths[symbol] > 0) {
      huff_codes[symbol] = next_codes[code_lengths[symbol]]++;
    } else {
      huff_codes[symbol] = NON_EXISTENT_SYMBOL;
    }
  }
  return 1;
}

static const uint8_t kReverse7[128] = {
  0, 64, 32, 96, 16, 80, 48, 112, 8, 72, 40, 104, 24, 88, 56, 120,
  4, 68, 36, 100, 20, 84, 52, 116, 12, 76, 44, 108, 28, 92, 60, 124,
  2, 66, 34, 98, 18, 82, 50, 114, 10, 74, 42, 106, 26, 90, 58, 122,
  6, 70, 38, 102, 22, 86, 54, 118, 14, 78, 46, 110, 30, 94, 62, 126,
  1, 65, 33, 97, 17, 81, 49, 113, 9, 73, 41, 105, 25, 89, 57, 121,
  5, 69, 37, 101, 21, 85, 53, 117, 13, 77, 45, 109, 29, 93, 61, 125,
  3, 67, 35, 99, 19, 83, 51, 115, 11, 75, 43, 107, 27, 91, 59, 123,
  7, 71, 39, 103, 23, 87, 55, 119, 15, 79, 47, 111, 31, 95, 63, 127
};

static int ReverseBitsShort(int bits, int num_bits) {
  return kReverse7[bits] >> (7 - num_bits);
}

static int TreeAddSymbol(HuffmanTree* const tree,
                         int symbol, int code, int code_length) {
  int step = HUFF_LUT_BITS;
  int base_code;
  HuffmanTreeNode* node = tree->root_;
  const HuffmanTreeNode* const max_node = tree->root_ + tree->max_nodes_;
  assert(symbol == (int16_t)symbol);
  if (code_length <= HUFF_LUT_BITS) {
    int i = 1 << (HUFF_LUT_BITS - code_length);
    base_code = ReverseBitsShort(code, code_length);
    do {
      int idx;
      --i;
      idx = base_code | (i << code_length);
      tree->lut_symbol_[idx] = (int16_t)symbol;
      tree->lut_bits_[idx] = (uint8_t)code_length;
    } while (i > 0);
  } else {
    base_code = ReverseBitsShort((code >> (code_length - HUFF_LUT_BITS)),
                                 HUFF_LUT_BITS);
  }
  while (code_length-- > 0) {
    if (node >= max_node) {
      return 0;
    }
    if (NodeIsEmpty(node)) {
      if (IsFull(tree)) return 0;    /* error: too many symbols. */
      AssignChildren(tree, node);
    } else if (!HuffmanTreeNodeIsNotLeaf(node)) {
      return 0;  /* leaf is already occupied. */
    }
    node += node->children_ + ((code >> code_length) & 1);
    if (--step == 0) {
      tree->lut_jump_[base_code] = (int16_t)(node - tree->root_);
    }
  }
  if (NodeIsEmpty(node)) {
    node->children_ = 0;      /* turn newly created node into a leaf. */
  } else if (HuffmanTreeNodeIsNotLeaf(node)) {
    return 0;   /* trying to assign a symbol to already used code. */
  }
  node->symbol_ = symbol;  /* Add symbol in this node. */
  return 1;
}

int BrotliHuffmanTreeBuildImplicit(HuffmanTree* const tree,
                                   const uint8_t* const code_lengths,
                                   int code_lengths_size) {
  int symbol;
  int num_symbols = 0;
  int root_symbol = 0;

  assert(tree != NULL);
  assert(code_lengths != NULL);

  /* Find out number of symbols and the root symbol. */
  for (symbol = 0; symbol < code_lengths_size; ++symbol) {
    if (code_lengths[symbol] > 0) {
      /* Note: code length = 0 indicates non-existent symbol. */
      ++num_symbols;
      root_symbol = symbol;
    }
  }

  /* Initialize the tree. Will fail for num_symbols = 0 */
  if (!TreeInit(tree, num_symbols)) return 0;

  /* Build tree. */
  if (num_symbols == 1) {  /* Trivial case. */
    const int max_symbol = code_lengths_size;
    if (root_symbol < 0 || root_symbol >= max_symbol) {
      BrotliHuffmanTreeRelease(tree);
      return 0;
    }
    return TreeAddSymbol(tree, root_symbol, 0, 0);
  } else {  /* Normal case. */
    int ok = 0;

    /* Get Huffman codes from the code lengths. */
    int* const codes =
        (int*)BrotliSafeMalloc((uint64_t)code_lengths_size, sizeof(*codes));
    if (codes == NULL) goto End;

    if (!HuffmanCodeLengthsToCodes(code_lengths, code_lengths_size, codes)) {
      goto End;
    }

    /* Add symbols one-by-one. */
    for (symbol = 0; symbol < code_lengths_size; ++symbol) {
      if (code_lengths[symbol] > 0) {
        if (!TreeAddSymbol(tree, symbol, codes[symbol], code_lengths[symbol])) {
          goto End;
        }
      }
    }
    ok = 1;
 End:
    free(codes);
    ok = ok && IsFull(tree);
    if (!ok) BrotliHuffmanTreeRelease(tree);
    return ok;
  }
}

#if defined(__cplusplus) || defined(c_plusplus)
}    /* extern "C" */
#endif
