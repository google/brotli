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

static inline double BitsEntropy(const int *population, int size) {
  int sum = 0;
  double retval = 0;
  const int *population_end = population + size;
  int p;
  if (size & 1) {
    goto odd_number_of_elements_left;
  }
  while (population < population_end) {
    p = *population++;
    sum += p;
    retval -= p * FastLog2(p);
 odd_number_of_elements_left:
    p = *population++;
    sum += p;
    retval -= p * FastLog2(p);
  }
  if (sum) retval += sum * FastLog2(sum);
  if (retval < sum) {
    // At least one bit per literal is needed.
    retval = sum;
  }
  return retval;
}

static const int kHuffmanExtraBits[kCodeLengthCodes] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 3,
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
  int prev_value = 8;
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
    if (i == length && value == 0)
      break;
    if (value == 0) {
      if (reps < 3) {
        histogram[0] += reps;
      } else {
        reps -= 2;
        while (reps > 0) {
          ++histogram[17];
          reps >>= 3;
        }
      }
    } else {
      tail_start = i;
      if (value != prev_value) {
        ++histogram[value];
        --reps;
      }
      prev_value = value;
      if (reps < 3) {
        histogram[value] += reps;
      } else {
        reps -= 2;
        while (reps > 0) {
          ++histogram[16];
          reps >>= 2;
        }
      }
    }
  }

  // create huffman tree of huffman tree
  uint8_t cost[kCodeLengthCodes] = { 0 };
  CreateHuffmanTree(histogram, kCodeLengthCodes, 7, cost);
  // account for rle extra bits
  cost[16] += 2;
  cost[17] += 3;

  int tree_size = 0;
  int bits = 18 + 2 * max_depth;  // huffman tree of huffman tree cost
  for (int i = 0; i < kCodeLengthCodes; ++i) {
    bits += histogram[i] * cost[i];  // huffman tree bit cost
    tree_size += histogram[i];
  }
  return bits;
}

template<int kSize>
double PopulationCost(const Histogram<kSize>& histogram) {
  if (histogram.total_count_ == 0) {
    return 12;
  }
  int count = 0;
  for (int i = 0; i < kSize && count < 5; ++i) {
    if (histogram.data_[i] > 0) {
      ++count;
    }
  }
  if (count == 1) {
    return 12;
  }
  if (count == 2) {
    return 20 + histogram.total_count_;
  }
  uint8_t depth[kSize] = { 0 };
  CreateHuffmanTree(&histogram.data_[0], kSize, 15, depth);
  int bits = 0;
  for (int i = 0; i < kSize; ++i) {
    bits += histogram.data_[i] * depth[i];
  }
  if (count == 3) {
    bits += 28;
  } else if (count == 4) {
    bits += 37;
  } else {
    bits += HuffmanBitCost(depth, kSize);
  }
  return bits;
}

}  // namespace brotli

#endif  // BROTLI_ENC_BIT_COST_H_
