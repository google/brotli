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
#include <vector>

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
    from_pos = (from_pos + insert_len + cmds[i].copy_len()) & mask;
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
                         size_t stride,
                         size_t num_histograms,
                         HistogramType* histograms) {
  for (size_t i = 0; i < num_histograms; ++i) {
    histograms[i].Clear();
  }
  unsigned int seed = 7;
  size_t block_length = length / num_histograms;
  for (size_t i = 0; i < num_histograms; ++i) {
    size_t pos = length * i / num_histograms;
    if (i != 0) {
      pos += MyRand(&seed) % block_length;
    }
    if (pos + stride >= length) {
      pos = length - stride - 1;
    }
    histograms[i].Add(data + pos, stride);
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
                        size_t num_histograms,
                        HistogramType* histograms) {
  size_t iters =
      kIterMulForRefining * length / stride + kMinItersForRefining;
  unsigned int seed = 7;
  iters = ((iters + num_histograms - 1) / num_histograms) * num_histograms;
  for (size_t iter = 0; iter < iters; ++iter) {
    HistogramType sample;
    RandomSample(&seed, data, length, stride, &sample);
    size_t ix = iter % num_histograms;
    histograms[ix].AddHistogram(sample);
  }
}

inline static double BitCost(size_t count) {
  return count == 0 ? -2.0 : FastLog2(count);
}

// Assigns a block id from the range [0, vec.size()) to each data element
// in data[0..length) and fills in block_id[0..length) with the assigned values.
// Returns the number of blocks, i.e. one plus the number of block switches.
template<typename DataType, int kSize>
size_t FindBlocks(const DataType* data, const size_t length,
                  const double block_switch_bitcost,
                  const size_t num_histograms,
                  const Histogram<kSize>* histograms,
                  double* insert_cost,
                  double* cost,
                  uint8_t* switch_signal,
                  uint8_t *block_id) {
  if (num_histograms <= 1) {
    for (size_t i = 0; i < length; ++i) {
      block_id[i] = 0;
    }
    return 1;
  }
  const size_t bitmaplen = (num_histograms + 7) >> 3;
  assert(num_histograms <= 256);
  memset(insert_cost, 0, sizeof(insert_cost[0]) * kSize * num_histograms);
  for (size_t j = 0; j < num_histograms; ++j) {
    insert_cost[j] = FastLog2(static_cast<uint32_t>(
        histograms[j].total_count_));
  }
  for (size_t i = kSize; i != 0;) {
    --i;
    for (size_t j = 0; j < num_histograms; ++j) {
      insert_cost[i * num_histograms + j] =
          insert_cost[j] - BitCost(histograms[j].data_[i]);
    }
  }
  memset(cost, 0, sizeof(cost[0]) * num_histograms);
  memset(switch_signal, 0, sizeof(switch_signal[0]) * length * bitmaplen);
  // After each iteration of this loop, cost[k] will contain the difference
  // between the minimum cost of arriving at the current byte position using
  // entropy code k, and the minimum cost of arriving at the current byte
  // position. This difference is capped at the block switch cost, and if it
  // reaches block switch cost, it means that when we trace back from the last
  // position, we need to switch here.
  for (size_t byte_ix = 0; byte_ix < length; ++byte_ix) {
    size_t ix = byte_ix * bitmaplen;
    size_t insert_cost_ix = data[byte_ix] * num_histograms;
    double min_cost = 1e99;
    for (size_t k = 0; k < num_histograms; ++k) {
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
    for (size_t k = 0; k < num_histograms; ++k) {
      cost[k] -= min_cost;
      if (cost[k] >= block_switch_cost) {
        cost[k] = block_switch_cost;
        const uint8_t mask = static_cast<uint8_t>(1u << (k & 7));
        assert((k >> 3) < bitmaplen);
        switch_signal[ix + (k >> 3)] |= mask;
      }
    }
  }
  // Now trace back from the last position and switch at the marked places.
  size_t byte_ix = length - 1;
  size_t ix = byte_ix * bitmaplen;
  uint8_t cur_id = block_id[byte_ix];
  size_t num_blocks = 1;
  while (byte_ix > 0) {
    --byte_ix;
    ix -= bitmaplen;
    const uint8_t mask = static_cast<uint8_t>(1u << (cur_id & 7));
    assert((static_cast<size_t>(cur_id) >> 3) < bitmaplen);
    if (switch_signal[ix + (cur_id >> 3)] & mask) {
      if (cur_id != block_id[byte_ix]) {
        cur_id = block_id[byte_ix];
        ++num_blocks;
      }
    }
    block_id[byte_ix] = cur_id;
  }
  return num_blocks;
}

static size_t RemapBlockIds(uint8_t* block_ids, const size_t length,
                            uint16_t* new_id, const size_t num_histograms) {
  static const uint16_t kInvalidId = 256;
  for (size_t i = 0; i < num_histograms; ++i) {
    new_id[i] = kInvalidId;
  }
  uint16_t next_id = 0;
  for (size_t i = 0; i < length; ++i) {
    assert(block_ids[i] < num_histograms);
    if (new_id[block_ids[i]] == kInvalidId) {
      new_id[block_ids[i]] = next_id++;
    }
  }
  for (size_t i = 0; i < length; ++i) {
    block_ids[i] = static_cast<uint8_t>(new_id[block_ids[i]]);
    assert(block_ids[i] < num_histograms);
  }
  assert(next_id <= num_histograms);
  return next_id;
}

template<typename HistogramType, typename DataType>
void BuildBlockHistograms(const DataType* data, const size_t length,
                          const uint8_t* block_ids,
                          const size_t num_histograms,
                          HistogramType* histograms) {
  for (size_t i = 0; i < num_histograms; ++i) {
    histograms[i].Clear();
  }
  for (size_t i = 0; i < length; ++i) {
    histograms[block_ids[i]].Add(data[i]);
  }
}

template<typename HistogramType, typename DataType>
void ClusterBlocks(const DataType* data, const size_t length,
                   const size_t num_blocks,
                   uint8_t* block_ids,
                   BlockSplit* split) {
  static const size_t kMaxNumberOfBlockTypes = 256;
  static const size_t kHistogramsPerBatch = 64;
  static const size_t kClustersPerBatch = 16;
  std::vector<uint32_t> histogram_symbols(num_blocks);
  std::vector<uint32_t> block_lengths(num_blocks);

  size_t block_idx = 0;
  for (size_t i = 0; i < length; ++i) {
    assert(block_idx < num_blocks);
    ++block_lengths[block_idx];
    if (i + 1 == length || block_ids[i] != block_ids[i + 1]) {
      ++block_idx;
    }
  }
  assert(block_idx == num_blocks);

  const size_t expected_num_clusters =
      kClustersPerBatch *
      (num_blocks + kHistogramsPerBatch - 1) / kHistogramsPerBatch;
  std::vector<HistogramType> all_histograms;
  std::vector<uint32_t> cluster_size;
  all_histograms.reserve(expected_num_clusters);
  cluster_size.reserve(expected_num_clusters);
  size_t num_clusters = 0;
  std::vector<HistogramType> histograms(
      std::min(num_blocks, kHistogramsPerBatch));
  size_t max_num_pairs = kHistogramsPerBatch * kHistogramsPerBatch / 2;
  std::vector<HistogramPair> pairs(max_num_pairs + 1);
  size_t pos = 0;
  for (size_t i = 0; i < num_blocks; i += kHistogramsPerBatch) {
    const size_t num_to_combine = std::min(num_blocks - i, kHistogramsPerBatch);
    uint32_t sizes[kHistogramsPerBatch];
    uint32_t clusters[kHistogramsPerBatch];
    uint32_t symbols[kHistogramsPerBatch];
    uint32_t remap[kHistogramsPerBatch];
    for (size_t j = 0; j < num_to_combine; ++j) {
      histograms[j].Clear();
      for (size_t k = 0; k < block_lengths[i + j]; ++k) {
        histograms[j].Add(data[pos++]);
      }
      histograms[j].bit_cost_ = PopulationCost(histograms[j]);
      symbols[j] = clusters[j] = static_cast<uint32_t>(j);
      sizes[j] = 1;
    }
    size_t num_new_clusters = HistogramCombine(
        &histograms[0], sizes, symbols, clusters, &pairs[0], num_to_combine,
        num_to_combine, kHistogramsPerBatch, max_num_pairs);
    for (size_t j = 0; j < num_new_clusters; ++j) {
      all_histograms.push_back(histograms[clusters[j]]);
      cluster_size.push_back(sizes[clusters[j]]);
      remap[clusters[j]] = static_cast<uint32_t>(j);
    }
    for (size_t j = 0; j < num_to_combine; ++j) {
      histogram_symbols[i + j] =
          static_cast<uint32_t>(num_clusters) + remap[symbols[j]];
    }
    num_clusters += num_new_clusters;
    assert(num_clusters == cluster_size.size());
    assert(num_clusters == all_histograms.size());
  }

  max_num_pairs =
      std::min(64 * num_clusters, (num_clusters / 2) * num_clusters);
  pairs.resize(max_num_pairs + 1);

  std::vector<uint32_t> clusters(num_clusters);
  for (size_t i = 0; i < num_clusters; ++i) {
    clusters[i] = static_cast<uint32_t>(i);
  }
  size_t num_final_clusters =
      HistogramCombine(&all_histograms[0], &cluster_size[0],
                       &histogram_symbols[0],
                       &clusters[0], &pairs[0], num_clusters,
                       num_blocks, kMaxNumberOfBlockTypes, max_num_pairs);

  static const uint32_t kInvalidIndex = std::numeric_limits<uint32_t>::max();
  std::vector<uint32_t> new_index(num_clusters, kInvalidIndex);
  uint32_t next_index = 0;
  pos = 0;
  for (size_t i = 0; i < num_blocks; ++i) {
    HistogramType histo;
    for (size_t j = 0; j < block_lengths[i]; ++j) {
      histo.Add(data[pos++]);
    }
    uint32_t best_out =
        i == 0 ? histogram_symbols[0] : histogram_symbols[i - 1];
    double best_bits = HistogramBitCostDistance(
        histo, all_histograms[best_out]);
    for (size_t j = 0; j < num_final_clusters; ++j) {
      const double cur_bits = HistogramBitCostDistance(
          histo, all_histograms[clusters[j]]);
      if (cur_bits < best_bits) {
        best_bits = cur_bits;
        best_out = clusters[j];
      }
    }
    histogram_symbols[i] = best_out;
    if (new_index[best_out] == kInvalidIndex) {
      new_index[best_out] = next_index++;
    }
  }
  uint8_t max_type = 0;
  uint32_t cur_length = 0;
  block_idx = 0;
  split->types.resize(num_blocks);
  split->lengths.resize(num_blocks);
  for (size_t i = 0; i < num_blocks; ++i) {
    cur_length += block_lengths[i];
    if (i + 1 == num_blocks ||
        histogram_symbols[i] != histogram_symbols[i + 1]) {
      const uint8_t id = static_cast<uint8_t>(new_index[histogram_symbols[i]]);
      split->types[block_idx] = id;
      split->lengths[block_idx] = cur_length;
      max_type = std::max(max_type, id);
      cur_length = 0;
      ++block_idx;
    }
  }
  split->types.resize(block_idx);
  split->lengths.resize(block_idx);
  split->num_types = static_cast<size_t>(max_type) + 1;
}

template<int kSize, typename DataType>
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
  size_t num_histograms = data.size() / literals_per_histogram + 1;
  if (num_histograms > max_histograms) {
    num_histograms = max_histograms;
  }
  Histogram<kSize>* histograms = new Histogram<kSize>[num_histograms];
  // Find good entropy codes.
  InitialEntropyCodes(&data[0], data.size(),
                      sampling_stride_length,
                      num_histograms, histograms);
  RefineEntropyCodes(&data[0], data.size(),
                     sampling_stride_length,
                     num_histograms, histograms);
  // Find a good path through literals with the good entropy codes.
  std::vector<uint8_t> block_ids(data.size());
  size_t num_blocks;
  const size_t bitmaplen = (num_histograms + 7) >> 3;
  double* insert_cost = new double[kSize * num_histograms];
  double *cost = new double[num_histograms];
  uint8_t* switch_signal = new uint8_t[data.size() * bitmaplen];
  uint16_t* new_id = new uint16_t[num_histograms];
  for (size_t i = 0; i < 10; ++i) {
    num_blocks = FindBlocks(&data[0], data.size(),
                            block_switch_cost,
                            num_histograms, histograms,
                            insert_cost, cost, switch_signal,
                            &block_ids[0]);
    num_histograms = RemapBlockIds(&block_ids[0], data.size(),
                                   new_id, num_histograms);
    BuildBlockHistograms(&data[0], data.size(), &block_ids[0],
                         num_histograms, histograms);
  }
  delete[] insert_cost;
  delete[] cost;
  delete[] switch_signal;
  delete[] new_id;
  delete[] histograms;
  ClusterBlocks<Histogram<kSize> >(&data[0], data.size(), num_blocks,
                                   &block_ids[0], split);
}

void SplitBlock(const Command* cmds,
                const size_t num_commands,
                const uint8_t* data,
                const size_t pos,
                const size_t mask,
                BlockSplit* literal_split,
                BlockSplit* insert_and_copy_split,
                BlockSplit* dist_split) {
  {
    // Create a continuous array of literals.
    std::vector<uint8_t> literals;
    CopyLiteralsToByteArray(cmds, num_commands, data, pos, mask, &literals);
    // Create the block split on the array of literals.
    // Literal histograms have alphabet size 256.
    SplitByteVector<256>(
        literals,
        kSymbolsPerLiteralHistogram, kMaxLiteralHistograms,
        kLiteralStrideLength, kLiteralBlockSwitchCost,
        literal_split);
  }

  {
    // Compute prefix codes for commands.
    std::vector<uint16_t> insert_and_copy_codes(num_commands);
    for (size_t i = 0; i < num_commands; ++i) {
      insert_and_copy_codes[i] = cmds[i].cmd_prefix_;
    }
    // Create the block split on the array of command prefixes.
    SplitByteVector<kNumCommandPrefixes>(
        insert_and_copy_codes,
        kSymbolsPerCommandHistogram, kMaxCommandHistograms,
        kCommandStrideLength, kCommandBlockSwitchCost,
        insert_and_copy_split);
  }

  {
    // Create a continuous array of distance prefixes.
    std::vector<uint16_t> distance_prefixes(num_commands);
    size_t pos = 0;
    for (size_t i = 0; i < num_commands; ++i) {
      const Command& cmd = cmds[i];
      if (cmd.copy_len() && cmd.cmd_prefix_ >= 128) {
        distance_prefixes[pos++] = cmd.dist_prefix_;
      }
    }
    distance_prefixes.resize(pos);
    // Create the block split on the array of distance prefixes.
    SplitByteVector<kNumDistancePrefixes>(
        distance_prefixes,
        kSymbolsPerDistanceHistogram, kMaxCommandHistograms,
        kCommandStrideLength, kDistanceBlockSwitchCost,
        dist_split);
  }
}

}  // namespace brotli
