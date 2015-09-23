// Copyright 2010 Google Inc. All Rights Reserved.
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
//
// Entropy encoding (Huffman) utilities.

#ifndef BROTLI_ENC_ENTROPY_ENCODE_H_
#define BROTLI_ENC_ENTROPY_ENCODE_H_

#include <stdint.h>
#include <string.h>
#include <vector>
#include "./histogram.h"
#include "./prefix.h"

namespace brotli {

// This function will create a Huffman tree.
//
// The (data,length) contains the population counts.
// The tree_limit is the maximum bit depth of the Huffman codes.
//
// The depth contains the tree, i.e., how many bits are used for
// the symbol.
//
// See http://en.wikipedia.org/wiki/Huffman_coding
void CreateHuffmanTree(const int *data,
                       const int length,
                       const int tree_limit,
                       uint8_t *depth);

// Change the population counts in a way that the consequent
// Huffman tree compression, especially its rle-part will be more
// likely to compress this data more efficiently.
//
// length contains the size of the histogram.
// counts contains the population counts.
int OptimizeHuffmanCountsForRle(int length, int* counts);

// Write a Huffman tree from bit depths into the bitstream representation
// of a Huffman tree. The generated Huffman tree is to be compressed once
// more using a Huffman tree
void WriteHuffmanTree(const uint8_t* depth,
                      uint32_t num,
                      std::vector<uint8_t> *tree,
                      std::vector<uint8_t> *extra_bits_data);

// Get the actual bit values for a tree of bit depths.
void ConvertBitDepthsToSymbols(const uint8_t *depth, int len, uint16_t *bits);

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
