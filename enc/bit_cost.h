/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

// Functions to estimate the bit cost of Huffman trees.

#ifndef BROTLI_ENC_BIT_COST_H_
#define BROTLI_ENC_BIT_COST_H_



#include "./entropy_encode.h"
#include "./fast_log.h"
#include "./types.h"

namespace brotli {

static inline double ShannonEntropy(const uint32_t *population, size_t size,
                                    size_t *total) {
  size_t sum = 0;
  double retval = 0;
  const uint32_t *population_end = population + size;
  size_t p;
  if (size & 1) {
    goto odd_number_of_elements_left;
  }
  while (population < population_end) {
    p = *population++;
    sum += p;
    retval -= static_cast<double>(p) * FastLog2(p);
 odd_number_of_elements_left:
    p = *population++;
    sum += p;
    retval -= static_cast<double>(p) * FastLog2(p);
  }
  if (sum) retval += static_cast<double>(sum) * FastLog2(sum);
  *total = sum;
  return retval;
}

static inline double BitsEntropy(const uint32_t *population, size_t size) {
  size_t sum;
  double retval = ShannonEntropy(population, size, &sum);
  if (retval < sum) {
    // At least one bit per literal is needed.
    retval = static_cast<double>(sum);
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
    return static_cast<double>(20 + histogram.total_count_);
  }
  double bits = 0;
  uint8_t depth_array[kSize] = { 0 };
  if (count <= 4) {
    // For very low symbol count we build the Huffman tree.
    CreateHuffmanTree(&histogram.data_[0], kSize, 15, depth_array);
    for (int i = 0; i < kSize; ++i) {
      bits += histogram.data_[i] * depth_array[i];
    }
    return count == 3 ? bits + 28 : bits + 37;
  }

  // In this loop we compute the entropy of the histogram and simultaneously
  // build a simplified histogram of the code length codes where we use the
  // zero repeat code 17, but we don't use the non-zero repeat code 16.
  size_t max_depth = 1;
  uint32_t depth_histo[kCodeLengthCodes] = { 0 };
  const double log2total = FastLog2(histogram.total_count_);
  for (size_t i = 0; i < kSize;) {
    if (histogram.data_[i] > 0) {
      // Compute -log2(P(symbol)) = -log2(count(symbol)/total_count) =
      //                          =  log2(total_count) - log2(count(symbol))
      double log2p = log2total - FastLog2(histogram.data_[i]);
      // Approximate the bit depth by round(-log2(P(symbol)))
      size_t depth = static_cast<size_t>(log2p + 0.5);
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
      uint32_t reps = 1;
      for (size_t k = i + 1; k < kSize && histogram.data_[k] == 0; ++k) {
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
  bits += static_cast<double>(18 + 2 * max_depth);
  // Add the entropy of the code length code histogram.
  bits += BitsEntropy(depth_histo, kCodeLengthCodes);
  return bits;
}

}  // namespace brotli

#endif  // BROTLI_ENC_BIT_COST_H_
