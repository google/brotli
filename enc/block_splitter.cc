/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

// Block split point selection utilities.

#include "./block_splitter.h"

#include <assert.h>
#include <math.h>

#include <algorithm>
#include <cstring>
#include <map>

#include "./cluster.h"
#include "./command.h"
#include "./fast_log.h"
#include "./histogram.h"

namespace brotli {

static const size_t kMaxLiteralHistograms = 100;
static const size_t kMaxCommandHistograms = 50;
static const double kLiteralBlockSwitchCost = 28.1;
static const double kCommandBlockSwitchCost = 13.5;
static const double kDistanceBlockSwitchCost = 14.6;
static const size_t kLiteralStrideLength = 70;
static const size_t kCommandStrideLength = 40;
static const size_t kSymbolsPerLiteralHistogram = 544;
static const size_t kSymbolsPerCommandHistogram = 530;
static const size_t kSymbolsPerDistanceHistogram = 544;
static const size_t kMinLengthForBlockSplitting = 128;
static const size_t kIterMulForRefining = 2;
static const size_t kMinItersForRefining = 100;

void CopyLiteralsToByteArray(const Command* cmds,
                             const size_t num_commands,
                             const uint8_t* data,
                             const size_t offset,
                             const size_t mask,
                             std::vector<uint8_t>* literals) {
  // Count how many we have.
  size_t total_length = 0;
  for (size_t i = 0; i < num_commands; ++i) {
    total_length += cmds[i].insert_len_;
  }
  if (total_length == 0) {
    return;
  }

  // Allocate.
  literals->resize(total_length);

  // Loop again, and copy this time.
  size_t pos = 0;
  size_t from_pos = offset & mask;
  for (size_t i = 0; i < num_commands && pos < total_length; ++i) {
    size_t insert_len = cmds[i].insert_len_;
    if (from_pos + insert_len > mask) {
      size_t head_size = mask + 1 - from_pos;
      memcpy(&(*literals)[pos], data + from_pos, head_size);
      from_pos = 0;
      pos += head_size;
      insert_len -= head_size;
    }
    if (insert_len > 0) {
      memcpy(&(*literals)[pos], data + from_pos, insert_len);
      pos += insert_len;
    }
    from_pos = (from_pos + insert_len + cmds[i].copy_len_) & mask;
  }
}

void CopyCommandsToByteArray(const Command* cmds,
                             const size_t num_commands,
                             std::vector<uint16_t>* insert_and_copy_codes,
                             std::vector<uint16_t>* distance_prefixes) {
  for (size_t i = 0; i < num_commands; ++i) {
    const Command& cmd = cmds[i];
    insert_and_copy_codes->push_back(cmd.cmd_prefix_);
    if (cmd.copy_len_ > 0 && cmd.cmd_prefix_ >= 128) {
      distance_prefixes->push_back(cmd.dist_prefix_);
    }
  }
}

inline static unsigned int MyRand(unsigned int* seed) {
  *seed *= 16807U;
  if (*seed == 0) {
    *seed = 1;
  }
  return *seed;
}

template<typename HistogramType, typename DataType>
void InitialEntropyCodes(const DataType* data, size_t length,
                         size_t literals_per_histogram,
                         size_t max_histograms,
                         size_t stride,
                         std::vector<HistogramType>* vec) {
  size_t total_histograms = length / literals_per_histogram + 1;
  if (total_histograms > max_histograms) {
    total_histograms = max_histograms;
  }
  unsigned int seed = 7;
  size_t block_length = length / total_histograms;
  for (size_t i = 0; i < total_histograms; ++i) {
    size_t pos = length * i / total_histograms;
    if (i != 0) {
      pos += MyRand(&seed) % block_length;
    }
    if (pos + stride >= length) {
      pos = length - stride - 1;
    }
    HistogramType histo;
    histo.Add(data + pos, stride);
    vec->push_back(histo);
  }
}

template<typename HistogramType, typename DataType>
void RandomSample(unsigned int* seed,
                  const DataType* data,
                  size_t length,
                  size_t stride,
                  HistogramType* sample) {
  size_t pos = 0;
  if (stride >= length) {
    pos = 0;
    stride = length;
  } else {
    pos = MyRand(seed) % (length - stride + 1);
  }
  sample->Add(data + pos, stride);
}

template<typename HistogramType, typename DataType>
void RefineEntropyCodes(const DataType* data, size_t length,
                        size_t stride,
                        std::vector<HistogramType>* vec) {
  size_t iters =
      kIterMulForRefining * length / stride + kMinItersForRefining;
  unsigned int seed = 7;
  iters = ((iters + vec->size() - 1) / vec->size()) * vec->size();
  for (size_t iter = 0; iter < iters; ++iter) {
    HistogramType sample;
    RandomSample(&seed, data, length, stride, &sample);
    size_t ix = iter % vec->size();
    (*vec)[ix].AddHistogram(sample);
  }
}

inline static double BitCost(size_t count) {
  return count == 0 ? -2.0 : FastLog2(count);
}

template<typename DataType, int kSize>
void FindBlocks(const DataType* data, const size_t length,
                const double block_switch_bitcost,
                const std::vector<Histogram<kSize> > &vec,
                uint8_t *block_id) {
  if (vec.size() <= 1) {
    for (size_t i = 0; i < length; ++i) {
      block_id[i] = 0;
    }
    return;
  }
  size_t vecsize = vec.size();
  assert(vecsize <= 256);
  double* insert_cost = new double[kSize * vecsize];
  memset(insert_cost, 0, sizeof(insert_cost[0]) * kSize * vecsize);
  for (size_t j = 0; j < vecsize; ++j) {
    insert_cost[j] = FastLog2(static_cast<uint32_t>(vec[j].total_count_));
  }
  for (size_t i = kSize; i != 0;) {
    --i;
    for (size_t j = 0; j < vecsize; ++j) {
      insert_cost[i * vecsize + j] = insert_cost[j] - BitCost(vec[j].data_[i]);
    }
  }
  double *cost = new double[vecsize];
  memset(cost, 0, sizeof(cost[0]) * vecsize);
  bool* switch_signal = new bool[length * vecsize];
  memset(switch_signal, 0, sizeof(switch_signal[0]) * length * vecsize);
  // After each iteration of this loop, cost[k] will contain the difference
  // between the minimum cost of arriving at the current byte position using
  // entropy code k, and the minimum cost of arriving at the current byte
  // position. This difference is capped at the block switch cost, and if it
  // reaches block switch cost, it means that when we trace back from the last
  // position, we need to switch here.
  for (size_t byte_ix = 0; byte_ix < length; ++byte_ix) {
    size_t ix = byte_ix * vecsize;
    size_t insert_cost_ix = data[byte_ix] * vecsize;
    double min_cost = 1e99;
    for (size_t k = 0; k < vecsize; ++k) {
      // We are coding the symbol in data[byte_ix] with entropy code k.
      cost[k] += insert_cost[insert_cost_ix + k];
      if (cost[k] < min_cost) {
        min_cost = cost[k];
        block_id[byte_ix] = static_cast<uint8_t>(k);
      }
    }
    double block_switch_cost = block_switch_bitcost;
    // More blocks for the beginning.
    if (byte_ix < 2000) {
      block_switch_cost *= 0.77 + 0.07 * static_cast<double>(byte_ix) / 2000;
    }
    for (size_t k = 0; k < vecsize; ++k) {
      cost[k] -= min_cost;
      if (cost[k] >= block_switch_cost) {
        cost[k] = block_switch_cost;
        switch_signal[ix + k] = true;
      }
    }
  }
  // Now trace back from the last position and switch at the marked places.
  size_t byte_ix = length - 1;
  size_t ix = byte_ix * vecsize;
  uint8_t cur_id = block_id[byte_ix];
  while (byte_ix > 0) {
    --byte_ix;
    ix -= vecsize;
    if (switch_signal[ix + cur_id]) {
      cur_id = block_id[byte_ix];
    }
    block_id[byte_ix] = cur_id;
  }
  delete[] insert_cost;
  delete[] cost;
  delete[] switch_signal;
}

size_t RemapBlockIds(uint8_t* block_ids, const size_t length) {
  std::map<uint8_t, uint8_t> new_id;
  size_t next_id = 0;
  for (size_t i = 0; i < length; ++i) {
    if (new_id.find(block_ids[i]) == new_id.end()) {
      new_id[block_ids[i]] = static_cast<uint8_t>(next_id);
      ++next_id;
    }
  }
  for (size_t i = 0; i < length; ++i) {
    block_ids[i] = new_id[block_ids[i]];
  }
  return next_id;
}

template<typename HistogramType, typename DataType>
void BuildBlockHistograms(const DataType* data, const size_t length,
                          uint8_t* block_ids,
                          std::vector<HistogramType>* histograms) {
  size_t num_types = RemapBlockIds(block_ids, length);
  assert(num_types <= 256);
  histograms->clear();
  histograms->resize(num_types);
  for (size_t i = 0; i < length; ++i) {
    (*histograms)[block_ids[i]].Add(data[i]);
  }
}

template<typename HistogramType, typename DataType>
void ClusterBlocks(const DataType* data, const size_t length,
                   uint8_t* block_ids) {
  std::vector<HistogramType> histograms;
  std::vector<uint32_t> block_index(length);
  uint32_t cur_idx = 0;
  HistogramType cur_histogram;
  for (size_t i = 0; i < length; ++i) {
    bool block_boundary = (i + 1 == length || block_ids[i] != block_ids[i + 1]);
    block_index[i] = cur_idx;
    cur_histogram.Add(data[i]);
    if (block_boundary) {
      histograms.push_back(cur_histogram);
      cur_histogram.Clear();
      ++cur_idx;
    }
  }
  std::vector<HistogramType> clustered_histograms;
  std::vector<uint32_t> histogram_symbols;
  // Block ids need to fit in one byte.
  static const size_t kMaxNumberOfBlockTypes = 256;
  ClusterHistograms(histograms, 1, histograms.size(),
                    kMaxNumberOfBlockTypes,
                    &clustered_histograms,
                    &histogram_symbols);
  for (size_t i = 0; i < length; ++i) {
    block_ids[i] = static_cast<uint8_t>(histogram_symbols[block_index[i]]);
  }
}

void BuildBlockSplit(const std::vector<uint8_t>& block_ids, BlockSplit* split) {
  uint8_t cur_id = block_ids[0];
  uint8_t max_type = cur_id;
  uint32_t cur_length = 1;
  for (size_t i = 1; i < block_ids.size(); ++i) {
    uint8_t next_id = block_ids[i];
    if (next_id != cur_id) {
      split->types.push_back(cur_id);
      split->lengths.push_back(cur_length);
      max_type = std::max(max_type, next_id);
      cur_id = next_id;
      cur_length = 0;
    }
    ++cur_length;
  }
  split->types.push_back(cur_id);
  split->lengths.push_back(cur_length);
  split->num_types = static_cast<size_t>(max_type) + 1;
}

template<typename HistogramType, typename DataType>
void SplitByteVector(const std::vector<DataType>& data,
                     const size_t literals_per_histogram,
                     const size_t max_histograms,
                     const size_t sampling_stride_length,
                     const double block_switch_cost,
                     BlockSplit* split) {
  if (data.empty()) {
    split->num_types = 1;
    return;
  } else if (data.size() < kMinLengthForBlockSplitting) {
    split->num_types = 1;
    split->types.push_back(0);
    split->lengths.push_back(static_cast<uint32_t>(data.size()));
    return;
  }
  std::vector<HistogramType> histograms;
  // Find good entropy codes.
  InitialEntropyCodes(&data[0], data.size(),
                      literals_per_histogram,
                      max_histograms,
                      sampling_stride_length,
                      &histograms);
  RefineEntropyCodes(&data[0], data.size(),
                     sampling_stride_length,
                     &histograms);
  // Find a good path through literals with the good entropy codes.
  std::vector<uint8_t> block_ids(data.size());
  for (size_t i = 0; i < 10; ++i) {
    FindBlocks(&data[0], data.size(),
               block_switch_cost,
               histograms,
               &block_ids[0]);
    BuildBlockHistograms(&data[0], data.size(), &block_ids[0], &histograms);
  }
  ClusterBlocks<HistogramType>(&data[0], data.size(), &block_ids[0]);
  BuildBlockSplit(block_ids, split);
}

void SplitBlock(const Command* cmds,
                const size_t num_commands,
                const uint8_t* data,
                const size_t pos,
                const size_t mask,
                BlockSplit* literal_split,
                BlockSplit* insert_and_copy_split,
                BlockSplit* dist_split) {
  // Create a continuous array of literals.
  std::vector<uint8_t> literals;
  CopyLiteralsToByteArray(cmds, num_commands, data, pos, mask, &literals);

  // Compute prefix codes for commands.
  std::vector<uint16_t> insert_and_copy_codes;
  std::vector<uint16_t> distance_prefixes;
  CopyCommandsToByteArray(cmds, num_commands,
                          &insert_and_copy_codes,
                          &distance_prefixes);

  SplitByteVector<HistogramLiteral>(
      literals,
      kSymbolsPerLiteralHistogram, kMaxLiteralHistograms,
      kLiteralStrideLength, kLiteralBlockSwitchCost,
      literal_split);
  SplitByteVector<HistogramCommand>(
      insert_and_copy_codes,
      kSymbolsPerCommandHistogram, kMaxCommandHistograms,
      kCommandStrideLength, kCommandBlockSwitchCost,
      insert_and_copy_split);
  SplitByteVector<HistogramDistance>(
      distance_prefixes,
      kSymbolsPerDistanceHistogram, kMaxCommandHistograms,
      kCommandStrideLength, kDistanceBlockSwitchCost,
      dist_split);
}

}  // namespace brotli
