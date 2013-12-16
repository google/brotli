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

#ifndef BROTLI_DEC_HUFFMAN_H_
#define BROTLI_DEC_HUFFMAN_H_

#include <assert.h>
#include "./types.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/* A node of a Huffman tree. */
typedef struct {
  int symbol_;
  int children_;  /* delta offset to both children (contiguous) or 0 if leaf. */
} HuffmanTreeNode;

/* Huffman Tree. */
#define HUFF_LUT_BITS 7
#define HUFF_LUT (1U << HUFF_LUT_BITS)
typedef struct HuffmanTree HuffmanTree;
struct HuffmanTree {
  /* Fast lookup for short bit lengths. */
  uint8_t lut_bits_[HUFF_LUT];
  int16_t lut_symbol_[HUFF_LUT];
  int16_t lut_jump_[HUFF_LUT];
  /* Complete tree for lookups. */
  HuffmanTreeNode* root_;   /* all the nodes, starting at root. */
  int max_nodes_;           /* max number of nodes */
  int num_nodes_;           /* number of currently occupied nodes */
};

/* Returns true if the given node is not a leaf of the Huffman tree. */
static BROTLI_INLINE int HuffmanTreeNodeIsNotLeaf(
    const HuffmanTreeNode* const node) {
  return node->children_;
}

/* Go down one level. Most critical function. 'right_child' must be 0 or 1. */
static BROTLI_INLINE const HuffmanTreeNode* HuffmanTreeNextNode(
    const HuffmanTreeNode* node, int right_child) {
  return node + node->children_ + right_child;
}

/* Releases the nodes of the Huffman tree. */
/* Note: It does NOT free 'tree' itself. */
void BrotliHuffmanTreeRelease(HuffmanTree* const tree);

/* Builds Huffman tree assuming code lengths are implicitly in symbol order. */
/* Returns false in case of error (invalid tree or memory error). */
int BrotliHuffmanTreeBuildImplicit(HuffmanTree* const tree,
                                   const uint8_t* const code_lengths,
                                   int code_lengths_size);

#if defined(__cplusplus) || defined(c_plusplus)
}    /* extern "C" */
#endif

#endif  /* BROTLI_DEC_HUFFMAN_H_ */
