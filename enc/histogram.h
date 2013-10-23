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
// Models the histograms of literals, commands and distance codes.

#ifndef BROTLI_ENC_HISTOGRAM_H_
#define BROTLI_ENC_HISTOGRAM_H_

#include <stdint.h>
#include <string.h>
#include <vector>
#include <utility>
#include "./command.h"
#include "./fast_log.h"
#include "./prefix.h"

namespace brotli {

class BlockSplit;

// A simple container for histograms of data in blocks.
template<int kDataSize>
struct Histogram {
  Histogram() {
    Clear();
  }
  void Clear() {
    memset(data_, 0, sizeof(data_));
    total_count_ = 0;
  }
  void Add(int val) {
    ++data_[val];
    ++total_count_;
  }
  void Remove(int val) {
    --data_[val];
    --total_count_;
  }
  template<typename DataType>
  void Add(const DataType *p, size_t n) {
    total_count_ += n;
    n += 1;
    while(--n) ++data_[*p++];
  }
  void AddHistogram(const Histogram& v) {
    total_count_ += v.total_count_;
    for (int i = 0; i < kDataSize; ++i) {
      data_[i] += v.data_[i];
    }
  }
  double EntropyBitCost() const {
    double retval = total_count_ * FastLog2(total_count_);
    for (int i = 0; i < kDataSize; ++i) {
      retval -= data_[i] * FastLog2(data_[i]);
    }
    return retval;
  }

  int data_[kDataSize];
  int total_count_;
  double bit_cost_;
};

// Literal histogram.
typedef Histogram<256> HistogramLiteral;
// Prefix histograms.
typedef Histogram<kNumCommandPrefixes> HistogramCommand;
typedef Histogram<kNumDistancePrefixes> HistogramDistance;
typedef Histogram<kNumBlockLenPrefixes> HistogramBlockLength;

void BuildHistograms(
    const std::vector<Command>& cmds,
    const BlockSplit& literal_split,
    const BlockSplit& insert_and_copy_split,
    const BlockSplit& dist_split,
    const uint8_t* input_buffer,
    size_t pos,
    int context_mode,
    int distance_context_mode,
    std::vector<HistogramLiteral>* literal_histograms,
    std::vector<HistogramCommand>* insert_and_copy_histograms,
    std::vector<HistogramDistance>* copy_dist_histograms);

}  // namespace brotli

#endif  // BROTLI_ENC_HISTOGRAM_H_
