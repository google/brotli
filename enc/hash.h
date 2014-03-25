// Copyright 2010 Google Inc. All Rights Reserved.
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
// A (forgetful) hash table to the data seen by the compressor, to
// help create backward references to previous data.

#ifndef BROTLI_ENC_HASH_H_
#define BROTLI_ENC_HASH_H_

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>

#include "./transform.h"
#include "./fast_log.h"
#include "./find_match_length.h"
#include "./port.h"
#include "./static_dict.h"

namespace brotli {

// kHashMul32 multiplier has these properties:
// * The multiplier must be odd. Otherwise we may lose the highest bit.
// * No long streaks of 1s or 0s.
// * There is no effort to ensure that it is a prime, the oddity is enough
//   for this use.
// * The number has been tuned heuristically against compression benchmarks.
static const uint32_t kHashMul32 = 0x1e35a7bd;

template<int kShiftBits, int kMinLength>
inline uint32_t Hash(const uint8_t *data) {
  if (kMinLength <= 3) {
    // If kMinLength is 2 or 3, we hash the first 3 bytes of data.
    uint32_t h = (BROTLI_UNALIGNED_LOAD32(data) & 0xffffff) * kHashMul32;
    // The higher bits contain more mixture from the multiplication,
    // so we take our results from there.
    return h >> (32 - kShiftBits);
  } else {
    // If kMinLength is at least 4, we hash the first 4 bytes of data.
    uint32_t h = BROTLI_UNALIGNED_LOAD32(data) * kHashMul32;
    // The higher bits contain more mixture from the multiplication,
    // so we take our results from there.
    return h >> (32 - kShiftBits);
  }
}

// Usually, we always choose the longest backward reference. This function
// allows for the exception of that rule.
//
// If we choose a backward reference that is further away, it will
// usually be coded with more bits. We approximate this by assuming
// log2(distance). If the distance can be expressed in terms of the
// last four distances, we use some heuristic constants to estimate
// the bits cost. For the first up to four literals we use the bit
// cost of the literals from the literal cost model, after that we
// use the average bit cost of the cost model.
//
// This function is used to sometimes discard a longer backward reference
// when it is not much longer and the bit cost for encoding it is more
// than the saved literals.
inline double BackwardReferenceScore(double average_cost,
                                     double start_cost4,
                                     double start_cost3,
                                     double start_cost2,
                                     int copy_length,
                                     int backward_reference_offset) {
  double retval = 0;
  switch (copy_length) {
    case 2: retval = start_cost2; break;
    case 3: retval = start_cost3; break;
    default: retval = start_cost4 + (copy_length - 4) * average_cost; break;
  }
  retval -= 1.20 * Log2Floor(backward_reference_offset);
  return retval;
}

inline double BackwardReferenceScoreUsingLastDistance(double average_cost,
                                                      double start_cost4,
                                                      double start_cost3,
                                                      double start_cost2,
                                                      int copy_length,
                                                      int distance_short_code) {
  double retval = 0;
  switch (copy_length) {
    case 2: retval = start_cost2; break;
    case 3: retval = start_cost3; break;
    default: retval = start_cost4 + (copy_length - 4) * average_cost; break;
  }
  static const double kDistanceShortCodeBitCost[16] = {
    -0.6, 0.95, 1.17, 1.27,
    0.93, 0.93, 0.96, 0.96, 0.99, 0.99,
    1.05, 1.05, 1.15, 1.15, 1.25, 1.25
  };
  retval -= kDistanceShortCodeBitCost[distance_short_code];
  return retval;
}

// A (forgetful) hash table to the data seen by the compressor, to
// help create backward references to previous data.
//
// This is a hash map of fixed size (kBucketSize) to a ring buffer of
// fixed size (kBlockSize). The ring buffer contains the last kBlockSize
// index positions of the given hash key in the compressed data.
template <int kBucketBits, int kBlockBits, int kMinLength>
class HashLongestMatch {
 public:
  HashLongestMatch()
      : last_distance1_(4),
        last_distance2_(11),
        last_distance3_(15),
        last_distance4_(16),
        insert_length_(0),
        average_cost_(5.4),
        static_dict_(NULL) {
    Reset();
  }
  void Reset() {
    std::fill(&num_[0], &num_[sizeof(num_) / sizeof(num_[0])], 0);
  }
  void SetStaticDictionary(const StaticDictionary *dict) {
    static_dict_ = dict;
  }
  bool HasStaticDictionary() const {
    return static_dict_ != NULL;
  }

  // Look at 3 bytes at data.
  // Compute a hash from these, and store the value of ix at that position.
  inline void Store(const uint8_t *data, const int ix) {
    const uint32_t key = Hash<kBucketBits, kMinLength>(data);
    const int minor_ix = num_[key] & kBlockMask;
    buckets_[key][minor_ix] = ix;
    ++num_[key];
  }

  // Store hashes for a range of data.
  void StoreHashes(const uint8_t *data, size_t len, int startix, int mask) {
    for (int p = 0; p < len; ++p) {
      Store(&data[p & mask], startix + p);
    }
  }

  // Find a longest backward match of &data[cur_ix] up to the length of
  // max_length.
  //
  // Does not look for matches longer than max_length.
  // Does not look for matches further away than max_backward.
  // Writes the best found match length into best_len_out.
  // Writes the index (&data[index]) offset from the start of the best match
  // into best_distance_out.
  // Write the score of the best match into best_score_out.
  bool FindLongestMatch(const uint8_t * __restrict data,
                        const float * __restrict literal_cost,
                        const size_t ring_buffer_mask,
                        const uint32_t cur_ix,
                        uint32_t max_length,
                        const uint32_t max_backward,
                        size_t * __restrict best_len_out,
                        size_t * __restrict best_len_code_out,
                        size_t * __restrict best_distance_out,
                        double * __restrict best_score_out,
                        bool * __restrict in_dictionary) {
    *in_dictionary = true;
    *best_len_code_out = 0;
    const size_t cur_ix_masked = cur_ix & ring_buffer_mask;
    const double start_cost4 = literal_cost == NULL ? 20 :
        literal_cost[cur_ix_masked] +
        literal_cost[(cur_ix + 1) & ring_buffer_mask] +
        literal_cost[(cur_ix + 2) & ring_buffer_mask] +
        literal_cost[(cur_ix + 3) & ring_buffer_mask];
    const double start_cost3 = literal_cost == NULL ? 15 :
        literal_cost[cur_ix_masked] +
        literal_cost[(cur_ix + 1) & ring_buffer_mask] +
        literal_cost[(cur_ix + 2) & ring_buffer_mask] + 0.3;
    double start_cost2 = literal_cost == NULL ? 10 :
        literal_cost[cur_ix_masked] +
        literal_cost[(cur_ix + 1) & ring_buffer_mask] + 1.2;
    bool match_found = false;
    // Don't accept a short copy from far away.
    double best_score = 8.115;
    if (insert_length_ < 4) {
      double cost_diff[4] = { 0.10, 0.04, 0.02, 0.01 };
      best_score += cost_diff[insert_length_];
    }
    size_t best_len = *best_len_out;
    *best_len_out = 0;
    size_t best_ix = 1;
    // Try last distance first.
    for (int i = 0; i < 16; ++i) {
      size_t prev_ix = cur_ix;
      switch(i) {
        case 0: prev_ix -= last_distance1_; break;
        case 1: prev_ix -= last_distance2_; break;
        case 2: prev_ix -= last_distance3_; break;
        case 3: prev_ix -= last_distance4_; break;

        case 4: prev_ix -= last_distance1_ - 1; break;
        case 5: prev_ix -= last_distance1_ + 1; break;
        case 6: prev_ix -= last_distance1_ - 2; break;
        case 7: prev_ix -= last_distance1_ + 2; break;
        case 8: prev_ix -= last_distance1_ - 3; break;
        case 9: prev_ix -= last_distance1_ + 3; break;

        case 10: prev_ix -= last_distance2_ - 1; break;
        case 11: prev_ix -= last_distance2_ + 1; break;
        case 12: prev_ix -= last_distance2_ - 2; break;
        case 13: prev_ix -= last_distance2_ + 2; break;
        case 14: prev_ix -= last_distance2_ - 3; break;
        case 15: prev_ix -= last_distance2_ + 3; break;
      }
      if (prev_ix >= cur_ix) {
        continue;
      }
      const size_t backward = cur_ix - prev_ix;
      if (PREDICT_FALSE(backward > max_backward)) {
        continue;
      }
      prev_ix &= ring_buffer_mask;
      if (cur_ix_masked + best_len > ring_buffer_mask ||
          prev_ix + best_len > ring_buffer_mask ||
          data[cur_ix_masked + best_len] != data[prev_ix + best_len]) {
        continue;
      }
      const size_t len =
          FindMatchLengthWithLimit(&data[prev_ix], &data[cur_ix_masked],
                                   max_length);
      if (len >= std::max(kMinLength, 3) ||
          (kMinLength == 2 && len == 2 && i < 2)) {
        // Comparing for >= 2 does not change the semantics, but just saves for
        // a few unnecessary binary logarithms in backward reference score,
        // since we are not interested in such short matches.
        const double score = BackwardReferenceScoreUsingLastDistance(
            average_cost_,
            start_cost4,
            start_cost3,
            start_cost2,
            len, i);
        if (best_score < score) {
          best_score = score;
          best_len = len;
          best_ix = backward;
          *best_len_out = best_len;
          *best_len_code_out = best_len;
          *best_distance_out = best_ix;
          *best_score_out = best_score;
          match_found = true;
          *in_dictionary = backward > max_backward;
        }
      }
    }
    if (kMinLength == 2) {
      int stop = int(cur_ix) - 64;
      if (stop < 0) { stop = 0; }
      start_cost2 -= 1.0;
      for (int i = cur_ix - 1; i > stop; --i) {
        size_t prev_ix = i;
        const size_t backward = cur_ix - prev_ix;
        if (PREDICT_FALSE(backward > max_backward)) {
          break;
        }
        prev_ix &= ring_buffer_mask;
        if (data[cur_ix_masked] != data[prev_ix] ||
            data[cur_ix_masked + 1] != data[prev_ix + 1]) {
          continue;
        }
        int len = 2;
        const double score = start_cost2 - 2.3 * Log2Floor(backward);

        if (best_score < score) {
          best_score = score;
          best_len = len;
          best_ix = backward;
          *best_len_out = best_len;
          *best_len_code_out = best_len;
          *best_distance_out = best_ix;
          match_found = true;
        }
      }
    }
    const uint32_t key = Hash<kBucketBits, kMinLength>(&data[cur_ix_masked]);
    const int * __restrict const bucket = &buckets_[key][0];
    const int down = (num_[key] > kBlockSize) ? (num_[key] - kBlockSize) : 0;
    for (int i = num_[key] - 1; i >= down; --i) {
      int prev_ix = bucket[i & kBlockMask];
      if (prev_ix >= 0) {
        const size_t backward = cur_ix - prev_ix;
        if (PREDICT_FALSE(backward > max_backward)) {
          break;
        }
        prev_ix &= ring_buffer_mask;
        if (cur_ix_masked + best_len > ring_buffer_mask ||
            prev_ix + best_len > ring_buffer_mask ||
            data[cur_ix_masked + best_len] != data[prev_ix + best_len]) {
          continue;
        }
        const size_t len =
            FindMatchLengthWithLimit(&data[prev_ix], &data[cur_ix_masked],
                                     max_length);
        if (len >= std::max(kMinLength, 3)) {
          // Comparing for >= 3 does not change the semantics, but just saves
          // for a few unnecessary binary logarithms in backward reference
          // score, since we are not interested in such short matches.
          const double score = BackwardReferenceScore(average_cost_,
                                                      start_cost4,
                                                      start_cost3,
                                                      start_cost2,
                                                      len, backward);
          if (best_score < score) {
            best_score = score;
            best_len = len;
            best_ix = backward;
            *best_len_out = best_len;
            *best_len_code_out = best_len;
            *best_distance_out = best_ix;
            *best_score_out = best_score;
            match_found = true;
            *in_dictionary = false;
          }
        }
      }
    }
    if (static_dict_ != NULL) {
      // We decide based on first 4 bytes how many bytes to test for.
      int prefix = BROTLI_UNALIGNED_LOAD32(&data[cur_ix_masked]);
      int maxlen = static_dict_->GetLength(prefix);
      for (int len = std::min<size_t>(maxlen, max_length);
           len > best_len && len >= 4; --len) {
        std::string snippet((const char *)&data[cur_ix_masked], len);
        int copy_len_code;
        int word_id;
        if (static_dict_->Get(snippet, &copy_len_code, &word_id)) {
          const size_t backward = max_backward + word_id + 1;
          const double score = BackwardReferenceScore(average_cost_,
                                                      start_cost4,
                                                      start_cost3,
                                                      start_cost2,
                                                      len, backward);
          if (best_score < score) {
            best_score = score;
            best_len = len;
            best_ix = backward;
            *best_len_out = best_len;
            *best_len_code_out = copy_len_code;
            *best_distance_out = best_ix;
            *best_score_out = best_score;
            match_found = true;
            *in_dictionary = true;
          }
        }
      }
    }
    return match_found;
  }

  void set_last_distance(int v) {
    if (last_distance1_ != v) {
      last_distance4_ = last_distance3_;
      last_distance3_ = last_distance2_;
      last_distance2_ = last_distance1_;
      last_distance1_ = v;
    }
  }

  int last_distance() const { return last_distance1_; }

  void set_insert_length(int v) { insert_length_ = v; }

  void set_average_cost(double v) { average_cost_ = v; }

 private:
  // Number of hash buckets.
  static const uint32_t kBucketSize = 1 << kBucketBits;

  // Only kBlockSize newest backward references are kept,
  // and the older are forgotten.
  static const uint32_t kBlockSize = 1 << kBlockBits;

  // Mask for accessing entries in a block (in a ringbuffer manner).
  static const uint32_t kBlockMask = (1 << kBlockBits) - 1;

  // Number of entries in a particular bucket.
  uint16_t num_[kBucketSize];

  // Buckets containing kBlockSize of backward references.
  int buckets_[kBucketSize][kBlockSize];

  int last_distance1_;
  int last_distance2_;
  int last_distance3_;
  int last_distance4_;

  // Cost adjustment for how many literals we are planning to insert
  // anyway.
  int insert_length_;

  double average_cost_;

  const StaticDictionary *static_dict_;
};

struct Hashers {
  enum Type {
    HASH_15_8_4 = 0,
    HASH_15_8_2 = 1,
  };

  void Init(Type type) {
    switch (type) {
      case HASH_15_8_4:
        hash_15_8_4.reset(new HashLongestMatch<15, 8, 4>());
        break;
      case HASH_15_8_2:
        hash_15_8_2.reset(new HashLongestMatch<15, 8, 2>());
        break;
      default:
        break;
    }
  }

  void SetStaticDictionary(const StaticDictionary *dict) {
    if (hash_15_8_4.get() != NULL) hash_15_8_4->SetStaticDictionary(dict);
    if (hash_15_8_2.get() != NULL) hash_15_8_2->SetStaticDictionary(dict);
  }

  std::unique_ptr<HashLongestMatch<15, 8, 4> > hash_15_8_4;
  std::unique_ptr<HashLongestMatch<15, 8, 2> > hash_15_8_2;
};

}  // namespace brotli

#endif  // BROTLI_ENC_HASH_H_
