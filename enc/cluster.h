/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

// Functions for clustering similar histograms together.

#ifndef BROTLI_ENC_CLUSTER_H_
#define BROTLI_ENC_CLUSTER_H_

#include <math.h>
#include <algorithm>
#include <utility>
#include <vector>

#include "./bit_cost.h"
#include "./entropy_encode.h"
#include "./fast_log.h"
#include "./histogram.h"
#include "./port.h"
#include "./types.h"

namespace brotli {

struct HistogramPair {
  uint32_t idx1;
  uint32_t idx2;
  double cost_combo;
  double cost_diff;
};

inline bool operator<(const HistogramPair& p1, const HistogramPair& p2) {
  if (p1.cost_diff != p2.cost_diff) {
    return p1.cost_diff > p2.cost_diff;
  }
  return (p1.idx2 - p1.idx1) > (p2.idx2 - p2.idx1);
}

// Returns entropy reduction of the context map when we combine two clusters.
inline double ClusterCostDiff(size_t size_a, size_t size_b) {
  size_t size_c = size_a + size_b;
  return static_cast<double>(size_a) * FastLog2(size_a) +
      static_cast<double>(size_b) * FastLog2(size_b) -
      static_cast<double>(size_c) * FastLog2(size_c);
}

// Computes the bit cost reduction by combining out[idx1] and out[idx2] and if
// it is below a threshold, stores the pair (idx1, idx2) in the *pairs queue.
template<typename HistogramType>
void CompareAndPushToQueue(const HistogramType* out,
                           const uint32_t* cluster_size,
                           uint32_t idx1, uint32_t idx2,
                           size_t max_num_pairs,
                           HistogramPair* pairs,
                           size_t* num_pairs) {
  if (idx1 == idx2) {
    return;
  }
  if (idx2 < idx1) {
    uint32_t t = idx2;
    idx2 = idx1;
    idx1 = t;
  }
  bool store_pair = false;
  HistogramPair p;
  p.idx1 = idx1;
  p.idx2 = idx2;
  p.cost_diff = 0.5 * ClusterCostDiff(cluster_size[idx1], cluster_size[idx2]);
  p.cost_diff -= out[idx1].bit_cost_;
  p.cost_diff -= out[idx2].bit_cost_;

  if (out[idx1].total_count_ == 0) {
    p.cost_combo = out[idx2].bit_cost_;
    store_pair = true;
  } else if (out[idx2].total_count_ == 0) {
    p.cost_combo = out[idx1].bit_cost_;
    store_pair = true;
  } else {
    double threshold = *num_pairs == 0 ? 1e99 :
        std::max(0.0, pairs[0].cost_diff);
    HistogramType combo = out[idx1];
    combo.AddHistogram(out[idx2]);
    double cost_combo = PopulationCost(combo);
    if (cost_combo < threshold - p.cost_diff) {
      p.cost_combo = cost_combo;
      store_pair = true;
    }
  }
  if (store_pair) {
    p.cost_diff += p.cost_combo;
    if (*num_pairs > 0 && pairs[0] < p) {
      // Replace the top of the queue if needed.
      if (*num_pairs < max_num_pairs) {
        pairs[*num_pairs] = pairs[0];
        ++(*num_pairs);
      }
      pairs[0] = p;
    } else if (*num_pairs < max_num_pairs) {
      pairs[*num_pairs] = p;
      ++(*num_pairs);
    }
  }
}

template<typename HistogramType>
size_t HistogramCombine(HistogramType* out,
                        uint32_t* cluster_size,
                        uint32_t* symbols,
                        uint32_t* clusters,
                        HistogramPair* pairs,
                        size_t num_clusters,
                        size_t symbols_size,
                        size_t max_clusters,
                        size_t max_num_pairs) {
  double cost_diff_threshold = 0.0;
  size_t min_cluster_size = 1;

  // We maintain a vector of histogram pairs, with the property that the pair
  // with the maximum bit cost reduction is the first.
  size_t num_pairs = 0;
  for (size_t idx1 = 0; idx1 < num_clusters; ++idx1) {
    for (size_t idx2 = idx1 + 1; idx2 < num_clusters; ++idx2) {
      CompareAndPushToQueue(out, cluster_size, clusters[idx1], clusters[idx2],
                            max_num_pairs, &pairs[0], &num_pairs);
    }
  }

  while (num_clusters > min_cluster_size) {
    if (pairs[0].cost_diff >= cost_diff_threshold) {
      cost_diff_threshold = 1e99;
      min_cluster_size = max_clusters;
      continue;
    }
    // Take the best pair from the top of heap.
    uint32_t best_idx1 = pairs[0].idx1;
    uint32_t best_idx2 = pairs[0].idx2;
    out[best_idx1].AddHistogram(out[best_idx2]);
    out[best_idx1].bit_cost_ = pairs[0].cost_combo;
    cluster_size[best_idx1] += cluster_size[best_idx2];
    for (size_t i = 0; i < symbols_size; ++i) {
      if (symbols[i] == best_idx2) {
        symbols[i] = best_idx1;
      }
    }
    for (size_t i = 0; i < num_clusters; ++i) {
      if (clusters[i] == best_idx2) {
        memmove(&clusters[i], &clusters[i + 1],
                (num_clusters - i - 1) * sizeof(clusters[0]));
        break;
      }
    }
    --num_clusters;
    // Remove pairs intersecting the just combined best pair.
    size_t copy_to_idx = 0;
    for (size_t i = 0; i < num_pairs; ++i) {
      HistogramPair& p = pairs[i];
      if (p.idx1 == best_idx1 || p.idx2 == best_idx1 ||
          p.idx1 == best_idx2 || p.idx2 == best_idx2) {
        // Remove invalid pair from the queue.
        continue;
      }
      if (pairs[0] < p) {
        // Replace the top of the queue if needed.
        HistogramPair front = pairs[0];
        pairs[0] = p;
        pairs[copy_to_idx] = front;
      } else {
        pairs[copy_to_idx] = p;
      }
      ++copy_to_idx;
    }
    num_pairs = copy_to_idx;

    // Push new pairs formed with the combined histogram to the heap.
    for (size_t i = 0; i < num_clusters; ++i) {
      CompareAndPushToQueue(out, cluster_size, best_idx1, clusters[i],
                            max_num_pairs, &pairs[0], &num_pairs);
    }
  }
  return num_clusters;
}

// -----------------------------------------------------------------------------
// Histogram refinement

// What is the bit cost of moving histogram from cur_symbol to candidate.
template<typename HistogramType>
double HistogramBitCostDistance(const HistogramType& histogram,
                                const HistogramType& candidate) {
  if (histogram.total_count_ == 0) {
    return 0.0;
  }
  HistogramType tmp = histogram;
  tmp.AddHistogram(candidate);
  return PopulationCost(tmp) - candidate.bit_cost_;
}

// Find the best 'out' histogram for each of the 'in' histograms.
// When called, clusters[0..num_clusters) contains the unique values from
// symbols[0..in_size), but this property is not preserved in this function.
// Note: we assume that out[]->bit_cost_ is already up-to-date.
template<typename HistogramType>
void HistogramRemap(const HistogramType* in, size_t in_size,
                    const uint32_t* clusters, size_t num_clusters,
                    HistogramType* out, uint32_t* symbols) {
  for (size_t i = 0; i < in_size; ++i) {
    uint32_t best_out = i == 0 ? symbols[0] : symbols[i - 1];
    double best_bits = HistogramBitCostDistance(in[i], out[best_out]);
    for (size_t j = 0; j < num_clusters; ++j) {
      const double cur_bits = HistogramBitCostDistance(in[i], out[clusters[j]]);
      if (cur_bits < best_bits) {
        best_bits = cur_bits;
        best_out = clusters[j];
      }
    }
    symbols[i] = best_out;
  }

  // Recompute each out based on raw and symbols.
  for (size_t j = 0; j < num_clusters; ++j) {
    out[clusters[j]].Clear();
  }
  for (size_t i = 0; i < in_size; ++i) {
    out[symbols[i]].AddHistogram(in[i]);
  }
}

// Reorders elements of the out[0..length) array and changes values in
// symbols[0..length) array in the following way:
//   * when called, symbols[] contains indexes into out[], and has N unique
//     values (possibly N < length)
//   * on return, symbols'[i] = f(symbols[i]) and
//                out'[symbols'[i]] = out[symbols[i]], for each 0 <= i < length,
//     where f is a bijection between the range of symbols[] and [0..N), and
//     the first occurrences of values in symbols'[i] come in consecutive
//     increasing order.
// Returns N, the number of unique values in symbols[].
template<typename HistogramType>
size_t HistogramReindex(HistogramType* out, uint32_t* symbols, size_t length) {
  static const uint32_t kInvalidIndex = std::numeric_limits<uint32_t>::max();
  std::vector<uint32_t> new_index(length, kInvalidIndex);
  uint32_t next_index = 0;
  for (size_t i = 0; i < length; ++i) {
    if (new_index[symbols[i]] == kInvalidIndex) {
      new_index[symbols[i]] = next_index;
      ++next_index;
    }
  }
  std::vector<HistogramType> tmp(next_index);
  next_index = 0;
  for (size_t i = 0; i < length; ++i) {
    if (new_index[symbols[i]] == next_index) {
      tmp[next_index] = out[symbols[i]];
      ++next_index;
    }
    symbols[i] = new_index[symbols[i]];
  }
  for (size_t i = 0; i < next_index; ++i) {
    out[i] = tmp[i];
  }
  return next_index;
}

// Clusters similar histograms in 'in' together, the selected histograms are
// placed in 'out', and for each index in 'in', *histogram_symbols will
// indicate which of the 'out' histograms is the best approximation.
template<typename HistogramType>
void ClusterHistograms(const std::vector<HistogramType>& in,
                       size_t num_contexts, size_t num_blocks,
                       size_t max_histograms,
                       std::vector<HistogramType>* out,
                       std::vector<uint32_t>* histogram_symbols) {
  const size_t in_size = num_contexts * num_blocks;
  assert(in_size == in.size());
  std::vector<uint32_t> cluster_size(in_size, 1);
  std::vector<uint32_t> clusters(in_size);
  size_t num_clusters = 0;
  out->resize(in_size);
  histogram_symbols->resize(in_size);
  for (size_t i = 0; i < in_size; ++i) {
    (*out)[i] = in[i];
    (*out)[i].bit_cost_ = PopulationCost(in[i]);
    (*histogram_symbols)[i] = static_cast<uint32_t>(i);
  }

  const size_t max_input_histograms = 64;
  // For the first pass of clustering, we allow all pairs.
  size_t max_num_pairs = max_input_histograms * max_input_histograms / 2;
  std::vector<HistogramPair> pairs(max_num_pairs + 1);

  for (size_t i = 0; i < in_size; i += max_input_histograms) {
    size_t num_to_combine = std::min(in_size - i, max_input_histograms);
    for (size_t j = 0; j < num_to_combine; ++j) {
      clusters[num_clusters + j] = static_cast<uint32_t>(i + j);
    }
    size_t num_new_clusters =
        HistogramCombine(&(*out)[0], &cluster_size[0],
                         &(*histogram_symbols)[i],
                         &clusters[num_clusters], &pairs[0],
                         num_to_combine, num_to_combine,
                         max_histograms, max_num_pairs);
    num_clusters += num_new_clusters;
  }

  // For the second pass, we limit the total number of histogram pairs.
  // After this limit is reached, we only keep searching for the best pair.
  max_num_pairs =
      std::min(64 * num_clusters, (num_clusters / 2) * num_clusters);
  pairs.resize(max_num_pairs + 1);

  // Collapse similar histograms.
  num_clusters = HistogramCombine(&(*out)[0], &cluster_size[0],
                                  &(*histogram_symbols)[0], &clusters[0],
                                  &pairs[0], num_clusters, in_size,
                                  max_histograms, max_num_pairs);

  // Find the optimal map from original histograms to the final ones.
  HistogramRemap(&in[0], in_size, &clusters[0], num_clusters,
                 &(*out)[0], &(*histogram_symbols)[0]);

  // Convert the context map to a canonical form.
  size_t num_histograms =
      HistogramReindex(&(*out)[0], &(*histogram_symbols)[0], in_size);
  out->resize(num_histograms);
}

}  // namespace brotli

#endif  // BROTLI_ENC_CLUSTER_H_
