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

#include "./dictionary_hash.h"
#include "./fast_log.h"
#include "./find_match_length.h"
#include "./port.h"
#include "./prefix.h"
#include "./static_dict.h"
#include "./transform.h"

namespace brotli {

static const int kDistanceCacheIndex[] = {
  0, 1, 2, 3, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1,
};
static const int kDistanceCacheOffset[] = {
  0, 0, 0, 0, -1, 1, -2, 2, -3, 3, -1, 1, -2, 2, -3, 3
};

static const int kCutoffTransformsCount = 10;
static const int kCutoffTransforms[] = {0, 12, 27, 23, 42, 63, 56, 48, 59, 64};

// kHashMul32 multiplier has these properties:
// * The multiplier must be odd. Otherwise we may lose the highest bit.
// * No long streaks of 1s or 0s.
// * There is no effort to ensure that it is a prime, the oddity is enough
//   for this use.
// * The number has been tuned heuristically against compression benchmarks.
static const uint32_t kHashMul32 = 0x1e35a7bd;

template<int kShiftBits>
inline uint32_t Hash(const uint8_t *data) {
  uint32_t h = BROTLI_UNALIGNED_LOAD32(data) * kHashMul32;
  // The higher bits contain more mixture from the multiplication,
  // so we take our results from there.
  return h >> (32 - kShiftBits);
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
inline double BackwardReferenceScore(int copy_length,
                                     int backward_reference_offset) {
  return 5.4 * copy_length - 1.20 * Log2Floor(backward_reference_offset);
}

inline double BackwardReferenceScoreUsingLastDistance(int copy_length,
                                                      int distance_short_code) {
  static const double kDistanceShortCodeBitCost[16] = {
    -0.6, 0.95, 1.17, 1.27,
    0.93, 0.93, 0.96, 0.96, 0.99, 0.99,
    1.05, 1.05, 1.15, 1.15, 1.25, 1.25
  };
  return 5.4 * copy_length - kDistanceShortCodeBitCost[distance_short_code];
}

struct BackwardMatch {
  BackwardMatch() : distance(0), length_and_code(0) {}

  BackwardMatch(int dist, int len)
      : distance(dist), length_and_code((len << 5)) {}

  BackwardMatch(int dist, int len, int len_code)
      : distance(dist),
        length_and_code((len << 5) | (len == len_code ? 0 : len_code)) {}

  int length() const {
    return length_and_code >> 5;
  }
  int length_code() const {
    int code = length_and_code & 31;
    return code ? code : length();
  }

  int distance;
  int length_and_code;
};

// A (forgetful) hash table to the data seen by the compressor, to
// help create backward references to previous data.
//
// This is a hash map of fixed size (kBucketSize). Starting from the
// given index, kBucketSweep buckets are used to store values of a key.
template <int kBucketBits, int kBucketSweep, bool kUseDictionary>
class HashLongestMatchQuickly {
 public:
  HashLongestMatchQuickly() {
    Reset();
  }
  void Reset() {
    // It is not strictly necessary to fill this buffer here, but
    // not filling will make the results of the compression stochastic
    // (but correct). This is because random data would cause the
    // system to find accidentally good backward references here and there.
    memset(&buckets_[0], 0, sizeof(buckets_));
    num_dict_lookups_ = 0;
    num_dict_matches_ = 0;
  }
  // Look at 4 bytes at data.
  // Compute a hash from these, and store the value somewhere within
  // [ix .. ix+3].
  inline void Store(const uint8_t *data, const int ix) {
    const uint32_t key = HashBytes(data);
    // Wiggle the value with the bucket sweep range.
    const uint32_t off = (static_cast<uint32_t>(ix) >> 3) % kBucketSweep;
    buckets_[key + off] = ix;
  }

  // Store hashes for a range of data.
  void StoreHashes(const uint8_t *data, size_t len, int startix, int mask) {
    for (int p = 0; p < len; ++p) {
      Store(&data[p & mask], startix + p);
    }
  }

  // Find a longest backward match of &ring_buffer[cur_ix & ring_buffer_mask]
  // up to the length of max_length.
  //
  // Does not look for matches longer than max_length.
  // Does not look for matches further away than max_backward.
  // Writes the best found match length into best_len_out.
  // Writes the index (&data[index]) of the start of the best match into
  // best_distance_out.
  inline bool FindLongestMatch(const uint8_t * __restrict ring_buffer,
                               const size_t ring_buffer_mask,
                               const int* __restrict distance_cache,
                               const uint32_t cur_ix,
                               const uint32_t max_length,
                               const uint32_t max_backward,
                               int * __restrict best_len_out,
                               int * __restrict best_len_code_out,
                               int * __restrict best_distance_out,
                               double* __restrict best_score_out) {
    const int best_len_in = *best_len_out;
    const int cur_ix_masked = cur_ix & ring_buffer_mask;
    int compare_char = ring_buffer[cur_ix_masked + best_len_in];
    double best_score = *best_score_out;
    int best_len = best_len_in;
    int backward = distance_cache[0];
    size_t prev_ix = cur_ix - backward;
    bool match_found = false;
    if (prev_ix < cur_ix) {
      prev_ix &= ring_buffer_mask;
      if (compare_char == ring_buffer[prev_ix + best_len]) {
        int len = FindMatchLengthWithLimit(&ring_buffer[prev_ix],
                                           &ring_buffer[cur_ix_masked],
                                           max_length);
        if (len >= 4) {
          best_score = BackwardReferenceScoreUsingLastDistance(len, 0);
          best_len = len;
          *best_len_out = len;
          *best_len_code_out = len;
          *best_distance_out = backward;
          *best_score_out = best_score;
          compare_char = ring_buffer[cur_ix_masked + best_len];
          if (kBucketSweep == 1) {
            return true;
          } else {
            match_found = true;
          }
        }
      }
    }
    const uint32_t key = HashBytes(&ring_buffer[cur_ix_masked]);
    if (kBucketSweep == 1) {
      // Only one to look for, don't bother to prepare for a loop.
      prev_ix = buckets_[key];
      backward = cur_ix - prev_ix;
      prev_ix &= ring_buffer_mask;
      if (compare_char != ring_buffer[prev_ix + best_len_in]) {
        return false;
      }
      if (PREDICT_FALSE(backward == 0 || backward > max_backward)) {
        return false;
      }
      const int len = FindMatchLengthWithLimit(&ring_buffer[prev_ix],
                                               &ring_buffer[cur_ix_masked],
                                               max_length);
      if (len >= 4) {
        *best_len_out = len;
        *best_len_code_out = len;
        *best_distance_out = backward;
        *best_score_out = BackwardReferenceScore(len, backward);
        return true;
      }
    } else {
      uint32_t *bucket = buckets_ + key;
      prev_ix = *bucket++;
      for (int i = 0; i < kBucketSweep; ++i, prev_ix = *bucket++) {
        const int backward = cur_ix - prev_ix;
        prev_ix &= ring_buffer_mask;
        if (compare_char != ring_buffer[prev_ix + best_len]) {
          continue;
        }
        if (PREDICT_FALSE(backward == 0 || backward > max_backward)) {
          continue;
        }
        const int len =
            FindMatchLengthWithLimit(&ring_buffer[prev_ix],
                                     &ring_buffer[cur_ix_masked],
                                     max_length);
        if (len >= 4) {
          const double score = BackwardReferenceScore(len, backward);
          if (best_score < score) {
            best_score = score;
            best_len = len;
            *best_len_out = best_len;
            *best_len_code_out = best_len;
            *best_distance_out = backward;
            *best_score_out = score;
            compare_char = ring_buffer[cur_ix_masked + best_len];
            match_found = true;
          }
        }
      }
    }
    if (kUseDictionary && !match_found &&
        num_dict_matches_ >= (num_dict_lookups_ >> 7)) {
      ++num_dict_lookups_;
      const uint32_t key = Hash<14>(&ring_buffer[cur_ix_masked]) << 1;
      const uint16_t v = kStaticDictionaryHash[key];
      if (v > 0) {
        const int len = v & 31;
        const int dist = v >> 5;
        const int offset = kBrotliDictionaryOffsetsByLength[len] + len * dist;
        if (len <= max_length) {
          const int matchlen =
              FindMatchLengthWithLimit(&ring_buffer[cur_ix_masked],
                                       &kBrotliDictionary[offset], len);
          if (matchlen > len - kCutoffTransformsCount && matchlen > 0) {
            const int transform_id = kCutoffTransforms[len - matchlen];
            const int word_id =
                transform_id * (1 << kBrotliDictionarySizeBitsByLength[len]) +
                dist;
            const size_t backward = max_backward + word_id + 1;
            const double score = BackwardReferenceScore(matchlen, backward);
            if (best_score < score) {
              ++num_dict_matches_;
              best_score = score;
              best_len = matchlen;
              *best_len_out = best_len;
              *best_len_code_out = len;
              *best_distance_out = backward;
              *best_score_out = best_score;
              return true;
            }
          }
        }
      }
    }
    return match_found;
  }

  enum { kHashLength = 5 };
  enum { kHashTypeLength = 8 };
  // HashBytes is the function that chooses the bucket to place
  // the address in. The HashLongestMatch and HashLongestMatchQuickly
  // classes have separate, different implementations of hashing.
  static uint32_t HashBytes(const uint8_t *data) {
    // Computing a hash based on 5 bytes works much better for
    // qualities 1 and 3, where the next hash value is likely to replace
    static const uint32_t kHashMul32 = 0x1e35a7bd;
    uint64_t h = (BROTLI_UNALIGNED_LOAD64(data) << 24) * kHashMul32;
    // The higher bits contain more mixture from the multiplication,
    // so we take our results from there.
    return h >> (64 - kBucketBits);
  }

 private:
  static const uint32_t kBucketSize = 1 << kBucketBits;
  uint32_t buckets_[kBucketSize + kBucketSweep];
  size_t num_dict_lookups_;
  size_t num_dict_matches_;
};

// The maximum length for which the zopflification uses distinct distances.
static const int kMaxZopfliLen = 325;

// A (forgetful) hash table to the data seen by the compressor, to
// help create backward references to previous data.
//
// This is a hash map of fixed size (kBucketSize) to a ring buffer of
// fixed size (kBlockSize). The ring buffer contains the last kBlockSize
// index positions of the given hash key in the compressed data.
template <int kBucketBits,
          int kBlockBits,
          int kNumLastDistancesToCheck>
class HashLongestMatch {
 public:
  HashLongestMatch() {
    Reset();
  }

  void Reset() {
    memset(&num_[0], 0, sizeof(num_));
    num_dict_lookups_ = 0;
    num_dict_matches_ = 0;
  }

  // Look at 3 bytes at data.
  // Compute a hash from these, and store the value of ix at that position.
  inline void Store(const uint8_t *data, const int ix) {
    const uint32_t key = HashBytes(data);
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
                        const size_t ring_buffer_mask,
                        const int* __restrict distance_cache,
                        const uint32_t cur_ix,
                        uint32_t max_length,
                        const uint32_t max_backward,
                        int * __restrict best_len_out,
                        int * __restrict best_len_code_out,
                        int * __restrict best_distance_out,
                        double * __restrict best_score_out) {
    *best_len_code_out = 0;
    const size_t cur_ix_masked = cur_ix & ring_buffer_mask;
    bool match_found = false;
    // Don't accept a short copy from far away.
    double best_score = *best_score_out;
    int best_len = *best_len_out;
    *best_len_out = 0;
    // Try last distance first.
    for (int i = 0; i < kNumLastDistancesToCheck; ++i) {
      const int idx = kDistanceCacheIndex[i];
      const int backward = distance_cache[idx] + kDistanceCacheOffset[i];
      size_t prev_ix = cur_ix - backward;
      if (prev_ix >= cur_ix) {
        continue;
      }
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
      if (len >= 3 || (len == 2 && i < 2)) {
        // Comparing for >= 2 does not change the semantics, but just saves for
        // a few unnecessary binary logarithms in backward reference score,
        // since we are not interested in such short matches.
        double score = BackwardReferenceScoreUsingLastDistance(len, i);
        if (best_score < score) {
          best_score = score;
          best_len = len;
          *best_len_out = best_len;
          *best_len_code_out = best_len;
          *best_distance_out = backward;
          *best_score_out = best_score;
          match_found = true;
        }
      }
    }
    const uint32_t key = HashBytes(&data[cur_ix_masked]);
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
        if (len >= 4) {
          // Comparing for >= 3 does not change the semantics, but just saves
          // for a few unnecessary binary logarithms in backward reference
          // score, since we are not interested in such short matches.
          double score = BackwardReferenceScore(len, backward);
          if (best_score < score) {
            best_score = score;
            best_len = len;
            *best_len_out = best_len;
            *best_len_code_out = best_len;
            *best_distance_out = backward;
            *best_score_out = best_score;
            match_found = true;
          }
        }
      }
    }
    if (!match_found && num_dict_matches_ >= (num_dict_lookups_ >> 7)) {
      uint32_t key = Hash<14>(&data[cur_ix_masked]) << 1;
      for (int k = 0; k < 2; ++k, ++key) {
        ++num_dict_lookups_;
        const uint16_t v = kStaticDictionaryHash[key];
        if (v > 0) {
          const int len = v & 31;
          const int dist = v >> 5;
          const int offset = kBrotliDictionaryOffsetsByLength[len] + len * dist;
          if (len <= max_length) {
            const int matchlen =
                FindMatchLengthWithLimit(&data[cur_ix_masked],
                                         &kBrotliDictionary[offset], len);
            if (matchlen > len - kCutoffTransformsCount && matchlen > 0) {
              const int transform_id = kCutoffTransforms[len - matchlen];
              const int word_id =
                  transform_id * (1 << kBrotliDictionarySizeBitsByLength[len]) +
                  dist;
              const size_t backward = max_backward + word_id + 1;
              double score = BackwardReferenceScore(matchlen, backward);
              if (best_score < score) {
                ++num_dict_matches_;
                best_score = score;
                best_len = matchlen;
                *best_len_out = best_len;
                *best_len_code_out = len;
                *best_distance_out = backward;
                *best_score_out = best_score;
                match_found = true;
              }
            }
          }
        }
      }
    }
    return match_found;
  }

  // Similar to FindLongestMatch(), but finds all matches.
  //
  // Sets *num_matches to the number of matches found, and stores the found
  // matches in matches[0] to matches[*num_matches - 1].
  //
  // If the longest match is longer than kMaxZopfliLen, returns only this
  // longest match.
  //
  // Requires that at least kMaxZopfliLen space is available in matches.
  void FindAllMatches(const uint8_t* data,
                      const size_t ring_buffer_mask,
                      const uint32_t cur_ix,
                      uint32_t max_length,
                      const uint32_t max_backward,
                      int* num_matches,
                      BackwardMatch* matches) const {
    BackwardMatch* const orig_matches = matches;
    const size_t cur_ix_masked = cur_ix & ring_buffer_mask;
    int best_len = 1;
    int stop = static_cast<int>(cur_ix) - 64;
    if (stop < 0) { stop = 0; }
    for (int i = cur_ix - 1; i > stop && best_len <= 2; --i) {
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
      const size_t len =
          FindMatchLengthWithLimit(&data[prev_ix], &data[cur_ix_masked],
                                   max_length);
      if (len > best_len) {
        best_len = len;
        if (len > kMaxZopfliLen) {
          matches = orig_matches;
        }
        *matches++ = BackwardMatch(backward, len);
      }
    }
    const uint32_t key = HashBytes(&data[cur_ix_masked]);
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
        if (len > best_len) {
          best_len = len;
          if (len > kMaxZopfliLen) {
            matches = orig_matches;
          }
          *matches++ = BackwardMatch(backward, len);
        }
      }
    }
    std::vector<int> dict_matches(kMaxDictionaryMatchLen + 1, kInvalidMatch);
    int minlen = std::max<int>(4, best_len + 1);
    if (FindAllStaticDictionaryMatches(&data[cur_ix_masked], minlen, max_length,
                                       &dict_matches[0])) {
      int maxlen = std::min<int>(kMaxDictionaryMatchLen, max_length);
      for (int l = minlen; l <= maxlen; ++l) {
        int dict_id = dict_matches[l];
        if (dict_id < kInvalidMatch) {
          *matches++ = BackwardMatch(max_backward + (dict_id >> 5) + 1, l,
                                     dict_id & 31);
        }
      }
    }
    *num_matches += matches - orig_matches;
  }

  enum { kHashLength = 4 };
  enum { kHashTypeLength = 4 };

  // HashBytes is the function that chooses the bucket to place
  // the address in. The HashLongestMatch and HashLongestMatchQuickly
  // classes have separate, different implementations of hashing.
  static uint32_t HashBytes(const uint8_t *data) {
    // kHashMul32 multiplier has these properties:
    // * The multiplier must be odd. Otherwise we may lose the highest bit.
    // * No long streaks of 1s or 0s.
    // * Is not unfortunate (see the unittest) for the English language.
    // * There is no effort to ensure that it is a prime, the oddity is enough
    //   for this use.
    // * The number has been tuned heuristically against compression benchmarks.
    static const uint32_t kHashMul32 = 0x1e35a7bd;
    uint32_t h = BROTLI_UNALIGNED_LOAD32(data) * kHashMul32;
    // The higher bits contain more mixture from the multiplication,
    // so we take our results from there.
    return h >> (32 - kBucketBits);
  }

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

  size_t num_dict_lookups_;
  size_t num_dict_matches_;
};

struct Hashers {
  // For kBucketSweep == 1, enabling the dictionary lookup makes compression
  // a little faster (0.5% - 1%) and it compresses 0.15% better on small text
  // and html inputs.
  typedef HashLongestMatchQuickly<16, 1, true> H1;
  typedef HashLongestMatchQuickly<16, 2, false> H2;
  typedef HashLongestMatchQuickly<16, 4, false> H3;
  typedef HashLongestMatchQuickly<17, 4, true> H4;
  typedef HashLongestMatch<14, 4, 4> H5;
  typedef HashLongestMatch<14, 5, 4> H6;
  typedef HashLongestMatch<15, 6, 10> H7;
  typedef HashLongestMatch<15, 7, 10> H8;
  typedef HashLongestMatch<15, 8, 16> H9;

  void Init(int type) {
    switch (type) {
      case 1: hash_h1.reset(new H1); break;
      case 2: hash_h2.reset(new H2); break;
      case 3: hash_h3.reset(new H3); break;
      case 4: hash_h4.reset(new H4); break;
      case 5: hash_h5.reset(new H5); break;
      case 6: hash_h6.reset(new H6); break;
      case 7: hash_h7.reset(new H7); break;
      case 8: hash_h8.reset(new H8); break;
      case 9: hash_h9.reset(new H9); break;
      default: break;
    }
  }

  template<typename Hasher>
  void WarmupHash(const size_t size, const uint8_t* dict, Hasher* hasher) {
    for (size_t i = 0; i + Hasher::kHashTypeLength - 1 < size; i++) {
      hasher->Store(dict, i);
    }
  }

  // Custom LZ77 window.
  void PrependCustomDictionary(
      int type, const size_t size, const uint8_t* dict) {
    switch (type) {
      case 1: WarmupHash(size, dict, hash_h1.get()); break;
      case 2: WarmupHash(size, dict, hash_h2.get()); break;
      case 3: WarmupHash(size, dict, hash_h3.get()); break;
      case 4: WarmupHash(size, dict, hash_h4.get()); break;
      case 5: WarmupHash(size, dict, hash_h5.get()); break;
      case 6: WarmupHash(size, dict, hash_h6.get()); break;
      case 7: WarmupHash(size, dict, hash_h7.get()); break;
      case 8: WarmupHash(size, dict, hash_h8.get()); break;
      case 9: WarmupHash(size, dict, hash_h9.get()); break;
      default: break;
    }
  }

  std::unique_ptr<H1> hash_h1;
  std::unique_ptr<H2> hash_h2;
  std::unique_ptr<H3> hash_h3;
  std::unique_ptr<H4> hash_h4;
  std::unique_ptr<H5> hash_h5;
  std::unique_ptr<H6> hash_h6;
  std::unique_ptr<H7> hash_h7;
  std::unique_ptr<H8> hash_h8;
  std::unique_ptr<H9> hash_h9;
};

}  // namespace brotli

#endif  // BROTLI_ENC_HASH_H_
