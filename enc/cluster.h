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
// Functions for clustering similar histograms together.

#ifndef BROTLI_ENC_CLUSTER_H_
#define BROTLI_ENC_CLUSTER_H_

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <algorithm>
#include <complex>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "./bit_cost.h"
#include "./entropy_encode.h"
#include "./fast_log.h"
#include "./histogram.h"

namespace brotli {

struct HistogramPair {
  int idx1;
  int idx2;
  bool valid;
  double cost_combo;
  double cost_diff;
};

struct HistogramPairComparator {
  bool operator()(const HistogramPair& p1, const HistogramPair& p2) const {
    if (p1.cost_diff != p2.cost_diff) {
      return p1.cost_diff > p2.cost_diff;
    }
    return abs(p1.idx1 - p1.idx2) > abs(p2.idx1 - p2.idx2);
  }
};

// Returns entropy reduction of the context map when we combine two clusters.
inline double ClusterCostDiff(int size_a, int size_b) {
  int size_c = size_a + size_b;
  return size_a * FastLog2(size_a) + size_b * FastLog2(size_b) -
      size_c * FastLog2(size_c);
}

// Computes the bit cost reduction by combining out[idx1] and out[idx2] and if
// it is below a threshold, stores the pair (idx1, idx2) in the *pairs heap.
template<typename HistogramType>
void CompareAndPushToHeap(const HistogramType* out,
                          const int* cluster_size,
                          int idx1, int idx2,
                          std::vector<HistogramPair>* pairs) {
  if (idx1 == idx2) {
    return;
  }
  if (idx2 < idx1) {
    int t = idx2;
    idx2 = idx1;
    idx1 = t;
  }
  bool store_pair = false;
  HistogramPair p;
  p.idx1 = idx1;
  p.idx2 = idx2;
  p.valid = true;
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
    pairs->push_back(p);
    std::push_heap(pairs->begin(), pairs->end(), HistogramPairComparator());
  }
}

template<typename HistogramType>
void HistogramCombine(HistogramType* out,
                      int* cluster_size,
                      int* symbols,
                      int symbols_size,
                      int max_clusters) {
  double cost_diff_threshold = 0.0;
  int min_cluster_size = 1;
  std::set<int> all_symbols;
  std::vector<int> clusters;
  for (int i = 0; i < symbols_size; ++i) {
    if (all_symbols.find(symbols[i]) == all_symbols.end()) {
      all_symbols.insert(symbols[i]);
      clusters.push_back(symbols[i]);
    }
  }

  // We maintain a heap of histogram pairs, ordered by the bit cost reduction.
  std::vector<HistogramPair> pairs;
  for (int idx1 = 0; idx1 < clusters.size(); ++idx1) {
    for (int idx2 = idx1 + 1; idx2 < clusters.size(); ++idx2) {
      CompareAndPushToHeap(out, cluster_size, clusters[idx1], clusters[idx2],
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
    int best_idx1 = pairs[0].idx1;
    int best_idx2 = pairs[0].idx2;
    out[best_idx1].AddHistogram(out[best_idx2]);
    out[best_idx1].bit_cost_ = pairs[0].cost_combo;
    cluster_size[best_idx1] += cluster_size[best_idx2];
    for (int i = 0; i < symbols_size; ++i) {
      if (symbols[i] == best_idx2) {
        symbols[i] = best_idx1;
      }
    }
    for (int i = 0; i + 1 < clusters.size(); ++i) {
      if (clusters[i] >= best_idx2) {
        clusters[i] = clusters[i + 1];
      }
    }
    clusters.pop_back();
    // Invalidate pairs intersecting the just combined best pair.
    for (int i = 0; i < pairs.size(); ++i) {
      HistogramPair& p = pairs[i];
      if (p.idx1 == best_idx1 || p.idx2 == best_idx1 ||
          p.idx1 == best_idx2 || p.idx2 == best_idx2) {
        p.valid = false;
      }
    }
    // Pop invalid pairs from the top of the heap.
    while (!pairs.empty() && !pairs[0].valid) {
      std::pop_heap(pairs.begin(), pairs.end(), HistogramPairComparator());
      pairs.pop_back();
    }
    // Push new pairs formed with the combined histogram to the heap.
    for (int i = 0; i < clusters.size(); ++i) {
      CompareAndPushToHeap(out, cluster_size, best_idx1, clusters[i], &pairs);
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
void HistogramRemap(const HistogramType* in, int in_size,
                    HistogramType* out, int* symbols) {
  std::set<int> all_symbols;
  for (int i = 0; i < in_size; ++i) {
    all_symbols.insert(symbols[i]);
  }
  for (int i = 0; i < in_size; ++i) {
    int best_out = i == 0 ? symbols[0] : symbols[i - 1];
    double best_bits = HistogramBitCostDistance(in[i], out[best_out]);
    for (std::set<int>::const_iterator k = all_symbols.begin();
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
  for (std::set<int>::const_iterator k = all_symbols.begin();
       k != all_symbols.end(); ++k) {
    out[*k].Clear();
  }
  for (int i = 0; i < in_size; ++i) {
    out[symbols[i]].AddHistogram(in[i]);
  }
}

// Reorder histograms in *out so that the new symbols in *symbols come in
// increasing order.
template<typename HistogramType>
void HistogramReindex(std::vector<HistogramType>* out,
                      std::vector<int>* symbols) {
  std::vector<HistogramType> tmp(*out);
  std::map<int, int> new_index;
  int next_index = 0;
  for (int i = 0; i < symbols->size(); ++i) {
    if (new_index.find((*symbols)[i]) == new_index.end()) {
      new_index[(*symbols)[i]] = next_index;
      (*out)[next_index] = tmp[(*symbols)[i]];
      ++next_index;
    }
  }
  out->resize(next_index);
  for (int i = 0; i < symbols->size(); ++i) {
    (*symbols)[i] = new_index[(*symbols)[i]];
  }
}

template<typename HistogramType>
void ClusterHistogramsTrivial(const std::vector<HistogramType>& in,
                              int num_contexts, int num_blocks,
                              int max_histograms,
                              std::vector<HistogramType>* out,
                              std::vector<int>* histogram_symbols) {
  out->resize(num_blocks);
  for (int i = 0; i < num_blocks; ++i) {
    (*out)[i].Clear();
    for (int j = 0; j < num_contexts; ++j) {
      (*out)[i].AddHistogram(in[i * num_contexts + j]);
      histogram_symbols->push_back(i);
    }
  }
}

// Clusters similar histograms in 'in' together, the selected histograms are
// placed in 'out', and for each index in 'in', *histogram_symbols will
// indicate which of the 'out' histograms is the best approximation.
template<typename HistogramType>
void ClusterHistograms(const std::vector<HistogramType>& in,
                       int num_contexts, int num_blocks,
                       int max_histograms,
                       std::vector<HistogramType>* out,
                       std::vector<int>* histogram_symbols) {
  const int in_size = num_contexts * num_blocks;
  std::vector<int> cluster_size(in_size, 1);
  out->resize(in_size);
  histogram_symbols->resize(in_size);
  for (int i = 0; i < in_size; ++i) {
    (*out)[i] = in[i];
    (*out)[i].bit_cost_ = PopulationCost(in[i]);
    (*histogram_symbols)[i] = i;
  }

  // Collapse similar histograms within a block type.
  if (num_contexts > 1) {
    for (int i = 0; i < num_blocks; ++i) {
      HistogramCombine(&(*out)[0], &cluster_size[0],
                       &(*histogram_symbols)[i * num_contexts], num_contexts,
                       max_histograms);
    }
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
