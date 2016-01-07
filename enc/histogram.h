/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

// Models the histograms of literals, commands and distance codes.

#ifndef BROTLI_ENC_HISTOGRAM_H_
#define BROTLI_ENC_HISTOGRAM_H_

#include <cstring>
#include <limits>
#include <vector>
#include "./context.h"
#include "./command.h"
#include "./fast_log.h"
#include "./prefix.h"
#include "./types.h"

namespace brotli {

struct BlockSplit;

// A simple container for histograms of data in blocks.
template<int kDataSize>
struct Histogram {
  Histogram() {
    Clear();
  }
  void Clear() {
    memset(data_, 0, sizeof(data_));
    total_count_ = 0;
    bit_cost_ = std::numeric_limits<double>::infinity();
  }
  void Add(size_t val) {
    ++data_[val];
    ++total_count_;
  }
  void Remove(size_t val) {
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
    for (size_t i = 0; i < kDataSize; ++i) {
      data_[i] += v.data_[i];
    }
  }

  uint32_t data_[kDataSize];
  size_t total_count_;
  double bit_cost_;
};

// Literal histogram.
typedef Histogram<256> HistogramLiteral;
// Prefix histograms.
typedef Histogram<kNumCommandPrefixes> HistogramCommand;
typedef Histogram<kNumDistancePrefixes> HistogramDistance;
typedef Histogram<kNumBlockLenPrefixes> HistogramBlockLength;
// Context map histogram, 256 Huffman tree indexes + 16 run length codes.
typedef Histogram<272> HistogramContextMap;
// Block type histogram, 256 block types + 2 special symbols.
typedef Histogram<258> HistogramBlockType;

static const size_t kLiteralContextBits = 6;
static const size_t kDistanceContextBits = 2;

void BuildHistograms(
    const Command* cmds,
    const size_t num_commands,
    const BlockSplit& literal_split,
    const BlockSplit& insert_and_copy_split,
    const BlockSplit& dist_split,
    const uint8_t* ringbuffer,
    size_t pos,
    size_t mask,
    uint8_t prev_byte,
    uint8_t prev_byte2,
    const std::vector<ContextType>& context_modes,
    std::vector<HistogramLiteral>* literal_histograms,
    std::vector<HistogramCommand>* insert_and_copy_histograms,
    std::vector<HistogramDistance>* copy_dist_histograms);

}  // namespace brotli

#endif  // BROTLI_ENC_HISTOGRAM_H_
