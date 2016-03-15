/* Copyright 2010 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

// Entropy encoding (Huffman) utilities.

#ifndef BROTLI_ENC_ENTROPY_ENCODE_H_
#define BROTLI_ENC_ENTROPY_ENCODE_H_

#include <string.h>
#include "./histogram.h"
#include "./prefix.h"
#include "./types.h"

namespace brotli {

// A node of a Huffman tree.
struct HuffmanTree {
  HuffmanTree() {}
  HuffmanTree(uint32_t count, int16_t left, int16_t right)
      : total_count_(count),
        index_left_(left),
        index_right_or_value_(right) {
  }
  uint32_t total_count_;
  int16_t index_left_;
  int16_t index_right_or_value_;
};

void SetDepth(const HuffmanTree &p, HuffmanTree *pool,
              uint8_t *depth, uint8_t level);

// This function will create a Huffman tree.
//
// The (data,length) contains the population counts.
// The tree_limit is the maximum bit depth of the Huffman codes.
//
// The depth contains the tree, i.e., how many bits are used for
// the symbol.
//
// The actual Huffman tree is constructed in the tree[] array, which has to
// be at least 2 * length + 1 long.
//
// See http://en.wikipedia.org/wiki/Huffman_coding
void CreateHuffmanTree(const uint32_t *data,
                       const size_t length,
                       const int tree_limit,
                       HuffmanTree* tree,
                       uint8_t *depth);

// Change the population counts in a way that the consequent
// Huffman tree compression, especially its rle-part will be more
// likely to compress this data more efficiently.
//
// length contains the size of the histogram.
// counts contains the population counts.
// good_for_rle is a buffer of at least length size
void OptimizeHuffmanCountsForRle(size_t length, uint32_t* counts,
                                 uint8_t* good_for_rle);

// Write a Huffman tree from bit depths into the bitstream representation
// of a Huffman tree. The generated Huffman tree is to be compressed once
// more using a Huffman tree
void WriteHuffmanTree(const uint8_t* depth,
                      size_t num,
                      size_t* tree_size,
                      uint8_t* tree,
                      uint8_t* extra_bits_data);

// Get the actual bit values for a tree of bit depths.
void ConvertBitDepthsToSymbols(const uint8_t *depth,
                               size_t len,
                               uint16_t *bits);

template<int kSize>
struct EntropyCode {
  // How many bits for symbol.
  uint8_t depth_[kSize];
  // Actual bits used to represent the symbol.
  uint16_t bits_[kSize];
  // How many non-zero depth.
  int count_;
  // First four symbols with non-zero depth.
  int symbols_[4];
};

static const int kCodeLengthCodes = 18;

// Literal entropy code.
typedef EntropyCode<256> EntropyCodeLiteral;
// Prefix entropy codes.
typedef EntropyCode<kNumCommandPrefixes> EntropyCodeCommand;
typedef EntropyCode<kNumDistancePrefixes> EntropyCodeDistance;
typedef EntropyCode<kNumBlockLenPrefixes> EntropyCodeBlockLength;
// Context map entropy code, 256 Huffman tree indexes + 16 run length codes.
typedef EntropyCode<272> EntropyCodeContextMap;
// Block type entropy code, 256 block types + 2 special symbols.
typedef EntropyCode<258> EntropyCodeBlockType;

}  // namespace brotli

#endif  // BROTLI_ENC_ENTROPY_ENCODE_H_
