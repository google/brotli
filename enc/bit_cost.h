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

static inline double ShannonEntropy(const int *population, int size,
                                    int *total) {
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
  *total = sum;
  return retval;
}

static inline double BitsEntropy(const int *population, int size) {
  int sum;
  double retval = ShannonEntropy(population, size, &sum);
  if (retval < sum) {
    // At least one bit per literal is needed.
    retval = sum;
  }
  return retval;
}


template<int kSize>
double PopulationCost(const Histogram<kSize>& histogram) {
  if (histogram.total_count_ == 0) {
    return 12;
  }
  int count = 0;
  for (int i = 0; i < kSize; ++i) {
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
  double bits = 0;
  uint8_t depth[kSize] = { 0 };
  if (count <= 4) {
    // For very low symbol count we build the Huffman tree.
    CreateHuffmanTree(&histogram.data_[0], kSize, 15, depth);
    for (int i = 0; i < kSize; ++i) {
      bits += histogram.data_[i] * depth[i];
    }
    return count == 3 ? bits + 28 : bits + 37;
  }

  // In this loop we compute the entropy of the histogram and simultaneously
  // build a simplified histogram of the code length codes where we use the
  // zero repeat code 17, but we don't use the non-zero repeat code 16.
  int max_depth = 1;
  int depth_histo[kCodeLengthCodes] = { 0 };
  const double log2total = FastLog2(histogram.total_count_);
  for (int i = 0; i < kSize;) {
    if (histogram.data_[i] > 0) {
      // Compute -log2(P(symbol)) = -log2(count(symbol)/total_count) =
      //                          =  log2(total_count) - log2(count(symbol))
      double log2p = log2total - FastLog2(histogram.data_[i]);
      // Approximate the bit depth by round(-log2(P(symbol)))
      int depth = static_cast<int>(log2p + 0.5);
      bits += histogram.data_[i] * log2p;
      if (depth > 15) {
        depth = 15;
      }
      if (depth > max_depth) {
        max_depth = depth;
      }
      ++depth_histo[depth];
      ++i;
    } else {
      // Compute the run length of zeros and add the appropriate number of 0 and
      // 17 code length codes to the code length code histogram.
      int reps = 1;
      for (int k = i + 1; k < kSize && histogram.data_[k] == 0; ++k) {
        ++reps;
      }
      i += reps;
      if (i == kSize) {
        // Don't add any cost for the last zero run, since these are encoded
        // only implicitly.
        break;
      }
      if (reps < 3) {
        depth_histo[0] += reps;
      } else {
        reps -= 2;
        while (reps > 0) {
          ++depth_histo[17];
          // Add the 3 extra bits for the 17 code length code.
          bits += 3;
          reps >>= 3;
        }
      }
    }
  }
  // Add the estimated encoding cost of the code length code histogram.
  bits += 18 + 2 * max_depth;
  // Add the entropy of the code length code histogram.
  bits += BitsEntropy(depth_histo, kCodeLengthCodes);
  return bits;
}

}  // namespace brotli

#endif  // BROTLI_ENC_BIT_COST_H_
