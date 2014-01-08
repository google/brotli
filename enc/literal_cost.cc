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
// Literal cost model to allow backward reference replacement to be efficient.

#include "./literal_cost.h"

#include <math.h>
#include <stdint.h>
#include <algorithm>

namespace brotli {

void EstimateBitCostsForLiterals(size_t pos, size_t len, size_t mask,
                                 const uint8_t *data, float *cost) {
  int histogram[256] = { 0 };
  int window_half = 2000;
  int in_window = std::min(static_cast<size_t>(window_half), len);

  // Bootstrap histogram.
  for (int i = 0; i < in_window; ++i) {
    ++histogram[data[(pos + i) & mask]];
  }

  // Compute bit costs with sliding window.
  for (int i = 0; i < len; ++i) {
    if (i - window_half >= 0) {
      // Remove a byte in the past.
      --histogram[data[(pos + i - window_half) & mask]];
      --in_window;
    }
    if (i + window_half < len) {
      // Add a byte in the future.
      ++histogram[data[(pos + i + window_half) & mask]];
      ++in_window;
    }
    int masked_pos = (pos + i) & mask;
    int histo = histogram[data[masked_pos]];
    if (histo == 0) {
      histo = 1;
    }
    cost[masked_pos] = log2(static_cast<double>(in_window) / histo);
    cost[masked_pos] += 0.029;
    if (cost[masked_pos] < 1.0) {
      cost[masked_pos] *= 0.5;
      cost[masked_pos] += 0.5;
    }
  }
}

}  // namespace brotli
