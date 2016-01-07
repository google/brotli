/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

// Functions for clustering similar histograms together.

#ifndef BROTLI_ENC_CLUSTER_H_
#define BROTLI_ENC_CLUSTER_H_

#include <math.h>
#include <algorithm>
#include <map>
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
                           std::vector<HistogramPair>* pairs) {
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
    double threshold = pairs->empty() ? 1e99 :
        std::max(0.0, (*pairs)[0].cost_diff);
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
    if (!pairs->empty() && (pairs->front() < p)) {
      // Replace the top of the queue if needed.
      pairs->push_back(pairs->front());
      pairs->front() = p;
    } else {
      pairs->push_back(p);
    }
  }
}

template<typename HistogramType>
void HistogramCombine(HistogramType* out,
                      uint32_t* cluster_size,
                      uint32_t* symbols,
                      size_t symbols_size,
                      size_t max_clusters) {
  double cost_diff_threshold = 0.0;
  size_t min_cluster_size = 1;

  // Uniquify the list of symbols.
  std::vector<uint32_t> clusters(symbols, symbols + symbols_size);
  std::sort(clusters.begin(), clusters.end());
  std::vector<uint32_t>::iterator last =
      std::unique(clusters.begin(), clusters.end());
  clusters.resize(static_cast<size_t>(last - clusters.begin()));

  // We maintain a heap of histogram pairs, ordered by the bit cost reduction.
  std::vector<HistogramPair> pairs;
  for (size_t idx1 = 0; idx1 < clusters.size(); ++idx1) {
    for (size_t idx2 = idx1 + 1; idx2 < clusters.size(); ++idx2) {
      CompareAndPushToQueue(out, cluster_size, clusters[idx1], clusters[idx2],
                            &pairs);
    }
  }

  while (clusters.size() > min_cluster_size) {
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
    for (std::vector<uint32_t>::iterator cluster = clusters.begin();
         cluster != clusters.end(); ++cluster) {
      if (*cluster >= best_idx2) {
        clusters.erase(cluster);
        break;
      }
    }

    // Remove pairs intersecting the just combined best pair.
    size_t copy_to_idx = 0;
    for (size_t i = 0; i < pairs.size(); ++i) {
      HistogramPair& p = pairs[i];
      if (p.idx1 == best_idx1 || p.idx2 == best_idx1 ||
          p.idx1 == best_idx2 || p.idx2 == best_idx2) {
        // Remove invalid pair from the queue.
        continue;
      }
      if (pairs.front() < p) {
        // Replace the top of the queue if needed.
        HistogramPair front = pairs.front();
        pairs.front() = p;
        pairs[copy_to_idx] = front;
      } else {
        pairs[copy_to_idx] = p;
      }
      ++copy_to_idx;
    }
    pairs.resize(copy_to_idx);

    // Push new pairs formed with the combined histogram to the heap.
    for (size_t i = 0; i < clusters.size(); ++i) {
      CompareAndPushToQueue(out, cluster_size, best_idx1, clusters[i], &pairs);
    }
  }
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
// Note: we assume that out[]->bit_cost_ is already up-to-date.
template<typename HistogramType>
void HistogramRemap(const HistogramType* in, size_t in_size,
                    HistogramType* out, uint32_t* symbols) {
  // Uniquify the list of symbols.
  std::vector<uint32_t> all_symbols(symbols, symbols + in_size);
  std::sort(all_symbols.begin(), all_symbols.end());
  std::vector<uint32_t>::iterator last =
      std::unique(all_symbols.begin(), all_symbols.end());
  all_symbols.resize(static_cast<size_t>(last - all_symbols.begin()));

  for (size_t i = 0; i < in_size; ++i) {
    uint32_t best_out = i == 0 ? symbols[0] : symbols[i - 1];
    double best_bits = HistogramBitCostDistance(in[i], out[best_out]);
    for (std::vector<uint32_t>::const_iterator k = all_symbols.begin();
         k != all_symbols.end(); ++k) {
      const double cur_bits = HistogramBitCostDistance(in[i], out[*k]);
      if (cur_bits < best_bits) {
        best_bits = cur_bits;
        best_out = *k;
      }
    }
    symbols[i] = best_out;
  }


  // Recompute each out based on raw and symbols.
  for (std::vector<uint32_t>::const_iterator k = all_symbols.begin();
       k != all_symbols.end(); ++k) {
    out[*k].Clear();
  }
  for (size_t i = 0; i < in_size; ++i) {
    out[symbols[i]].AddHistogram(in[i]);
  }
}

// Reorder histograms in *out so that the new symbols in *symbols come in
// increasing order.
template<typename HistogramType>
void HistogramReindex(std::vector<HistogramType>* out,
                      std::vector<uint32_t>* symbols) {
  std::vector<HistogramType> tmp(*out);
  std::map<uint32_t, uint32_t> new_index;
  uint32_t next_index = 0;
  for (size_t i = 0; i < symbols->size(); ++i) {
    if (new_index.find((*symbols)[i]) == new_index.end()) {
      new_index[(*symbols)[i]] = next_index;
      (*out)[next_index] = tmp[(*symbols)[i]];
      ++next_index;
    }
  }
  out->resize(next_index);
  for (size_t i = 0; i < symbols->size(); ++i) {
    (*symbols)[i] = new_index[(*symbols)[i]];
  }
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
  out->resize(in_size);
  histogram_symbols->resize(in_size);
  for (size_t i = 0; i < in_size; ++i) {
    (*out)[i] = in[i];
    (*out)[i].bit_cost_ = PopulationCost(in[i]);
    (*histogram_symbols)[i] = static_cast<uint32_t>(i);
  }


  const size_t max_input_histograms = 64;
  for (size_t i = 0; i < in_size; i += max_input_histograms) {
    size_t num_to_combine = std::min(in_size - i, max_input_histograms);
    HistogramCombine(&(*out)[0], &cluster_size[0],
                     &(*histogram_symbols)[i], num_to_combine,
                     max_histograms);
  }

  // Collapse similar histograms.
  HistogramCombine(&(*out)[0], &cluster_size[0],
                   &(*histogram_symbols)[0], in_size,
                   max_histograms);

  // Find the optimal map from original histograms to the final ones.
  HistogramRemap(&in[0], in_size, &(*out)[0], &(*histogram_symbols)[0]);

  // Convert the context map to a canonical form.
  HistogramReindex(out, histogram_symbols);

}


}  // namespace brotli

#endif  // BROTLI_ENC_CLUSTER_H_
