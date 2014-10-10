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
// Hufmann tree compression, especially its rle-part will be more
// likely to compress this data more efficiently.
//
// length contains the size of the histogram.
// counts contains the population counts.
int OptimizeHuffmanCountsForRle(int length, int* counts);


// Write a huffman tree from bit depths into the bitstream representation
// of a Huffman tree. The generated Huffman tree is to be compressed once
// more using a Huffman tree
void WriteHuffmanTree(const uint8_t* depth, const int length,
                      uint8_t* tree,
                      uint8_t* extra_bits_data,
                      int* huffman_tree_size);

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

template<int kSize>
void BuildEntropyCode(const Histogram<kSize>& histogram,
                      const int tree_limit,
                      const int alphabet_size,
                      EntropyCode<kSize>* code) {
  memset(code->depth_, 0, sizeof(code->depth_));
  memset(code->bits_, 0, sizeof(code->bits_));
  memset(code->symbols_, 0, sizeof(code->symbols_));
  code->count_ = 0;
  if (histogram.total_count_ == 0) return;
  for (int i = 0; i < kSize; ++i) {
    if (histogram.data_[i] > 0) {
      if (code->count_ < 4) code->symbols_[code->count_] = i;
      ++code->count_;
    }
  }
  if (alphabet_size >= 50 && code->count_ >= 16) {
    int counts[kSize];
    memcpy(counts, &histogram.data_[0], sizeof(counts[0]) * kSize);
    OptimizeHuffmanCountsForRle(alphabet_size, counts);
    CreateHuffmanTree(counts, alphabet_size, tree_limit, &code->depth_[0]);
  } else {
    CreateHuffmanTree(&histogram.data_[0], alphabet_size, tree_limit,
                      &code->depth_[0]);
  }
  ConvertBitDepthsToSymbols(&code->depth_[0], alphabet_size, &code->bits_[0]);
}

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
