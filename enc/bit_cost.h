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
//
// Functions to estimate the bit cost of Huffman trees.

#ifndef BROTLI_ENC_BIT_COST_H_
#define BROTLI_ENC_BIT_COST_H_

#include <stdint.h>

#include "./entropy_encode.h"
#include "./fast_log.h"

namespace brotli {

static const int kHuffmanExtraBits[kCodeLengthCodes] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 3, 7,
};

static inline int HuffmanTreeBitCost(const int* counts, const uint8_t* depth) {
  int nbits = 0;
  for (int i = 0; i < kCodeLengthCodes; ++i) {
    nbits += counts[i] * (depth[i] + kHuffmanExtraBits[i]);
  }
  return nbits;
}

static inline int HuffmanTreeBitCost(
    const Histogram<kCodeLengthCodes>& histogram,
    const EntropyCode<kCodeLengthCodes>& entropy) {
  return HuffmanTreeBitCost(&histogram.data_[0], &entropy.depth_[0]);
}

static inline int HuffmanBitCost(const uint8_t* depth, int length) {
  int max_depth = 1;
  int histogram[kCodeLengthCodes] = { 0 };
  int tail_start = 0;
  // compute histogram of compacted huffman tree
  for (int i = 0; i < length;) {
    const int value = depth[i];
    if (value > max_depth) {
      max_depth = value;
    }
    int reps = 1;
    for (int k = i + 1; k < length && depth[k] == value; ++k) {
      ++reps;
    }
    i += reps;
    if (value == 0) {
      while (reps > 10) {
        ++histogram[18];
        reps -= 138;
      }
      if (reps > 2) {
        ++histogram[17];
      } else if (reps > 0) {
        histogram[0] += reps;
      }
    } else {
      tail_start = i;
      ++histogram[value];
      --reps;
      while (reps > 2) {
        ++histogram[16];
        reps -= 6;
      }
      if (reps > 0) {
        histogram[value] += reps;
      }
    }
  }

  // create huffman tree of huffman tree
  uint8_t cost[kCodeLengthCodes] = { 0 };
  CreateHuffmanTree(histogram, kCodeLengthCodes, 7, cost);
  // account for rle extra bits
  cost[16] += 2;
  cost[17] += 3;
  cost[18] += 7;

  int tree_size = 0;
  int bits = 6 + 3 * max_depth;  // huffman tree of huffman tree cost
  for (int i = 0; i < kCodeLengthCodes; ++i) {
    bits += histogram[i] * cost[i];  // huffman tree bit cost
    tree_size += histogram[i];
  }
  // bit cost adjustment for long trailing zero sequence
  int tail_size = length - tail_start;
  int tail_bits = 0;
  while (tail_size >= 1) {
    if (tail_size < 3) {
      tail_bits += tail_size * cost[0];
      tree_size -= tail_size;
      break;
    } else if (tail_size < 11) {
      tail_bits += cost[17];
      --tree_size;
      break;
    } else {
      tail_bits += cost[18];
      tail_size -= 138;
      --tree_size;
    }
  }
  if (tail_bits > 12) {
    bits += ((Log2Ceiling(tree_size - 1) + 1) & ~1) + 3 - tail_bits;
  }
  return bits;
}

template<int kSize>
double PopulationCost(const Histogram<kSize>& histogram) {
  if (histogram.total_count_ == 0) {
    return 4;
  }
  int symbols[2] = { 0 };
  int count = 0;
  for (int i = 0; i < kSize && count < 3; ++i) {
    if (histogram.data_[i] > 0) {
      if (count < 2) symbols[count] = i;
      ++count;
    }
  }
  if (count <= 2 && symbols[0] < 256 && symbols[1] < 256) {
    return ((symbols[0] <= 1 ? 4 : 11) +
            (count == 2 ? 8 + histogram.total_count_ : 0));
  }
  uint8_t depth[kSize] = { 0 };
  CreateHuffmanTree(&histogram.data_[0], kSize, 15, depth);
  int bits = HuffmanBitCost(depth, kSize);
  for (int i = 0; i < kSize; ++i) {
    bits += histogram.data_[i] * depth[i];
  }
  return bits;
}

}  // namespace brotli

#endif  // BROTLI_ENC_BIT_COST_H_
