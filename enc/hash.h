/* Copyright 2010 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

// A (forgetful) hash table to the data seen by the compressor, to
// help create backward references to previous data.

#ifndef BROTLI_ENC_HASH_H_
#define BROTLI_ENC_HASH_H_

#include <sys/types.h>
#include <algorithm>
#include <cstring>
#include <vector>

#include "./dictionary_hash.h"
#include "./fast_log.h"
#include "./find_match_length.h"
#include "./port.h"
#include "./prefix.h"
#include "./static_dict.h"
#include "./transform.h"
#include "./types.h"

namespace brotli {

static const uint32_t kDistanceCacheIndex[] = {
  0, 1, 2, 3, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1,
};
static const int kDistanceCacheOffset[] = {
  0, 0, 0, 0, -1, 1, -2, 2, -3, 3, -1, 1, -2, 2, -3, 3
};

static const uint32_t kCutoffTransformsCount = 10;
static const uint8_t kCutoffTransforms[] = {
  0, 12, 27, 23, 42, 63, 56, 48, 59, 64
};

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
//
// backward_reference_offset MUST be positive.
inline double BackwardReferenceScore(size_t copy_length,
                                     size_t backward_reference_offset) {
  return 5.4 * static_cast<double>(copy_length) -
      1.20 * Log2FloorNonZero(backward_reference_offset);
}

inline double BackwardReferenceScoreUsingLastDistance(size_t copy_length,
    size_t distance_short_code) {
  static const double kDistanceShortCodeBitCost[16] = {
    -0.6, 0.95, 1.17, 1.27,
    0.93, 0.93, 0.96, 0.96, 0.99, 0.99,
    1.05, 1.05, 1.15, 1.15, 1.25, 1.25
  };
  return 5.4 * static_cast<double>(copy_length) -
      kDistanceShortCodeBitCost[distance_short_code];
}

struct BackwardMatch {
  BackwardMatch() : distance(0), length_and_code(0) {}

  BackwardMatch(size_t dist, size_t len)
      : distance(static_cast<uint32_t>(dist))
      , length_and_code(static_cast<uint32_t>(len << 5)) {}

  BackwardMatch(size_t dist, size_t len, size_t len_code)
      : distance(static_cast<uint32_t>(dist))
      , length_and_code(static_cast<uint32_t>(
            (len << 5) | (len == len_code ? 0 : len_code))) {}

  size_t length() const {
    return length_and_code >> 5;
  }
  size_t length_code() const {
    size_t code = length_and_code & 31;
    return code ? code : length();
  }

  uint32_t distance;
  uint32_t length_and_code;
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
    need_init_ = true;
    num_dict_lookups_ = 0;
    num_dict_matches_ = 0;
  }
  void Init() {
    if (need_init_) {
      // It is not strictly necessary to fill this buffer here, but
      // not filling will make the results of the compression stochastic
      // (but correct). This is because random data would cause the
      // system to find accidentally good backward references here and there.
      memset(&buckets_[0], 0, sizeof(buckets_));
      need_init_ = false;
    }
  }
  void InitForData(const uint8_t* data, size_t num) {
    for (size_t i = 0; i < num; ++i) {
      const uint32_t key = HashBytes(&data[i]);
      memset(&buckets_[key], 0, kBucketSweep * sizeof(buckets_[0]));
      need_init_ = false;
    }
  }
  // Look at 4 bytes at data.
  // Compute a hash from these, and store the value somewhere within
  // [ix .. ix+3].
  inline void Store(const uint8_t *data, const uint32_t ix) {
    const uint32_t key = HashBytes(data);
    // Wiggle the value with the bucket sweep range.
    const uint32_t off = (ix >> 3) % kBucketSweep;
    buckets_[key + off] = ix;
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
                               const size_t cur_ix,
                               const size_t max_length,
                               const size_t max_backward,
                               size_t * __restrict best_len_out,
                               size_t * __restrict best_len_code_out,
                               size_t * __restrict best_distance_out,
                               double* __restrict best_score_out) {
    const size_t best_len_in = *best_len_out;
    const size_t cur_ix_masked = cur_ix & ring_buffer_mask;
    int compare_char = ring_buffer[cur_ix_masked + best_len_in];
    double best_score = *best_score_out;
    size_t best_len = best_len_in;
    size_t cached_backward = static_cast<size_t>(distance_cache[0]);
    size_t prev_ix = cur_ix - cached_backward;
    bool match_found = false;
    if (prev_ix < cur_ix) {
      prev_ix &= static_cast<uint32_t>(ring_buffer_mask);
      if (compare_char == ring_buffer[prev_ix + best_len]) {
        size_t len = FindMatchLengthWithLimit(&ring_buffer[prev_ix],
                                              &ring_buffer[cur_ix_masked],
                                              max_length);
        if (len >= 4) {
          best_score = BackwardReferenceScoreUsingLastDistance(len, 0);
          best_len = len;
          *best_len_out = len;
          *best_len_code_out = len;
          *best_distance_out = cached_backward;
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
      size_t backward = cur_ix - prev_ix;
      prev_ix &= static_cast<uint32_t>(ring_buffer_mask);
      if (compare_char != ring_buffer[prev_ix + best_len_in]) {
        return false;
      }
      if (PREDICT_FALSE(backward == 0 || backward > max_backward)) {
        return false;
      }
      const size_t len = FindMatchLengthWithLimit(&ring_buffer[prev_ix],
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
        const size_t backward = cur_ix - prev_ix;
        prev_ix &= static_cast<uint32_t>(ring_buffer_mask);
        if (compare_char != ring_buffer[prev_ix + best_len]) {
          continue;
        }
        if (PREDICT_FALSE(backward == 0 || backward > max_backward)) {
          continue;
        }
        const size_t len = FindMatchLengthWithLimit(&ring_buffer[prev_ix],
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
      const uint32_t dict_key = Hash<14>(&ring_buffer[cur_ix_masked]) << 1;
      const uint16_t v = kStaticDictionaryHash[dict_key];
      if (v > 0) {
        const uint32_t len = v & 31;
        const uint32_t dist = v >> 5;
        const size_t offset =
            kBrotliDictionaryOffsetsByLength[len] + len * dist;
        if (len <= max_length) {
          const size_t matchlen =
              FindMatchLengthWithLimit(&ring_buffer[cur_ix_masked],
                                       &kBrotliDictionary[offset], len);
          if (matchlen + kCutoffTransformsCount > len && matchlen > 0) {
            const size_t transform_id = kCutoffTransforms[len - matchlen];
            const size_t word_id =
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
    uint64_t h = (BROTLI_UNALIGNED_LOAD64(data) << 24) * kHashMul32;
    // The higher bits contain more mixture from the multiplication,
    // so we take our results from there.
    return static_cast<uint32_t>(h >> (64 - kBucketBits));
  }

  enum { kHashMapSize = 4 << kBucketBits };

 private:
  static const uint32_t kBucketSize = 1 << kBucketBits;
  uint32_t buckets_[kBucketSize + kBucketSweep];
  // True if buckets_ array needs to be initialized.
  bool need_init_;
  size_t num_dict_lookups_;
  size_t num_dict_matches_;
};

// The maximum length for which the zopflification uses distinct distances.
static const uint16_t kMaxZopfliLen = 325;

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
    need_init_ = true;
    num_dict_lookups_ = 0;
    num_dict_matches_ = 0;
  }

  void Init() {
    if (need_init_) {
      memset(&num_[0], 0, sizeof(num_));
      need_init_ = false;
    }
  }

  void InitForData(const uint8_t* data, size_t num) {
    for (size_t i = 0; i < num; ++i) {
      const uint32_t key = HashBytes(&data[i]);
      num_[key] = 0;
      need_init_ = false;
    }
  }

  // Look at 3 bytes at data.
  // Compute a hash from these, and store the value of ix at that position.
  inline void Store(const uint8_t *data, const uint32_t ix) {
    const uint32_t key = HashBytes(data);
    const int minor_ix = num_[key] & kBlockMask;
    buckets_[key][minor_ix] = ix;
    ++num_[key];
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
                        const size_t cur_ix,
                        const size_t max_length,
                        const size_t max_backward,
                        size_t * __restrict best_len_out,
                        size_t * __restrict best_len_code_out,
                        size_t * __restrict best_distance_out,
                        double * __restrict best_score_out) {
    *best_len_code_out = 0;
    const size_t cur_ix_masked = cur_ix & ring_buffer_mask;
    bool match_found = false;
    // Don't accept a short copy from far away.
    double best_score = *best_score_out;
    size_t best_len = *best_len_out;
    *best_len_out = 0;
    // Try last distance first.
    for (size_t i = 0; i < kNumLastDistancesToCheck; ++i) {
      const size_t idx = kDistanceCacheIndex[i];
      const size_t backward =
          static_cast<size_t>(distance_cache[idx] + kDistanceCacheOffset[i]);
      size_t prev_ix = static_cast<size_t>(cur_ix - backward);
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
      const size_t len = FindMatchLengthWithLimit(&data[prev_ix],
                                                  &data[cur_ix_masked],
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
    const uint32_t * __restrict const bucket = &buckets_[key][0];
    const size_t down = (num_[key] > kBlockSize) ? (num_[key] - kBlockSize) : 0;
    for (size_t i = num_[key]; i > down;) {
      --i;
      size_t prev_ix = bucket[i & kBlockMask];
      const size_t backward = cur_ix - prev_ix;
      if (PREDICT_FALSE(backward == 0 || backward > max_backward)) {
        break;
      }
      prev_ix &= ring_buffer_mask;
      if (cur_ix_masked + best_len > ring_buffer_mask ||
          prev_ix + best_len > ring_buffer_mask ||
          data[cur_ix_masked + best_len] != data[prev_ix + best_len]) {
        continue;
      }
      const size_t len = FindMatchLengthWithLimit(&data[prev_ix],
                                                  &data[cur_ix_masked],
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
    if (!match_found && num_dict_matches_ >= (num_dict_lookups_ >> 7)) {
      size_t dict_key = Hash<14>(&data[cur_ix_masked]) << 1;
      for (int k = 0; k < 2; ++k, ++dict_key) {
        ++num_dict_lookups_;
        const uint16_t v = kStaticDictionaryHash[dict_key];
        if (v > 0) {
          const size_t len = v & 31;
          const size_t dist = v >> 5;
          const size_t offset =
              kBrotliDictionaryOffsetsByLength[len] + len * dist;
          if (len <= max_length) {
            const size_t matchlen =
                FindMatchLengthWithLimit(&data[cur_ix_masked],
                                         &kBrotliDictionary[offset], len);
            if (matchlen + kCutoffTransformsCount > len && matchlen > 0) {
              const size_t transform_id = kCutoffTransforms[len - matchlen];
              const size_t word_id =
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
  size_t FindAllMatches(const uint8_t* data,
                        const size_t ring_buffer_mask,
                        const size_t cur_ix,
                        const size_t max_length,
                        const size_t max_backward,
                        BackwardMatch* matches) const {
    BackwardMatch* const orig_matches = matches;
    const size_t cur_ix_masked = cur_ix & ring_buffer_mask;
    size_t best_len = 1;
    size_t stop = cur_ix - 64;
    if (cur_ix < 64) { stop = 0; }
    for (size_t i = cur_ix - 1; i > stop && best_len <= 2; --i) {
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
    const uint32_t * __restrict const bucket = &buckets_[key][0];
    const size_t down = (num_[key] > kBlockSize) ? (num_[key] - kBlockSize) : 0;
    for (size_t i = num_[key]; i > down;) {
      --i;
      size_t prev_ix = bucket[i & kBlockMask];
      const size_t backward = cur_ix - prev_ix;
      if (PREDICT_FALSE(backward == 0 || backward > max_backward)) {
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
    std::vector<uint32_t> dict_matches(kMaxDictionaryMatchLen + 1,
                                       kInvalidMatch);
    size_t minlen = std::max<size_t>(4, best_len + 1);
    if (FindAllStaticDictionaryMatches(&data[cur_ix_masked], minlen, max_length,
                                       &dict_matches[0])) {
      size_t maxlen = std::min<size_t>(kMaxDictionaryMatchLen, max_length);
      for (size_t l = minlen; l <= maxlen; ++l) {
        uint32_t dict_id = dict_matches[l];
        if (dict_id < kInvalidMatch) {
          *matches++ = BackwardMatch(max_backward + (dict_id >> 5) + 1, l,
                                     dict_id & 31);
        }
      }
    }
    return static_cast<size_t>(matches - orig_matches);
  }

  enum { kHashLength = 4 };
  enum { kHashTypeLength = 4 };

  // HashBytes is the function that chooses the bucket to place
  // the address in. The HashLongestMatch and HashLongestMatchQuickly
  // classes have separate, different implementations of hashing.
  static uint32_t HashBytes(const uint8_t *data) {
    uint32_t h = BROTLI_UNALIGNED_LOAD32(data) * kHashMul32;
    // The higher bits contain more mixture from the multiplication,
    // so we take our results from there.
    return h >> (32 - kBucketBits);
  }

  enum { kHashMapSize = 2 << kBucketBits };

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
  uint32_t buckets_[kBucketSize][kBlockSize];

  // True if num_ array needs to be initialized.
  bool need_init_;

  size_t num_dict_lookups_;
  size_t num_dict_matches_;
};

struct Hashers {
  // For kBucketSweep == 1, enabling the dictionary lookup makes compression
  // a little faster (0.5% - 1%) and it compresses 0.15% better on small text
  // and html inputs.
  typedef HashLongestMatchQuickly<16, 1, true> H2;
  typedef HashLongestMatchQuickly<16, 2, false> H3;
  typedef HashLongestMatchQuickly<17, 4, true> H4;
  typedef HashLongestMatch<14, 4, 4> H5;
  typedef HashLongestMatch<14, 5, 4> H6;
  typedef HashLongestMatch<15, 6, 10> H7;
  typedef HashLongestMatch<15, 7, 10> H8;
  typedef HashLongestMatch<15, 8, 16> H9;

  Hashers() : hash_h2(0), hash_h3(0), hash_h4(0), hash_h5(0),
              hash_h6(0), hash_h7(0), hash_h8(0), hash_h9(0) {}

  ~Hashers() {
    delete hash_h2;
    delete hash_h3;
    delete hash_h4;
    delete hash_h5;
    delete hash_h6;
    delete hash_h7;
    delete hash_h8;
    delete hash_h9;
  }

  void Init(int type) {
    switch (type) {
      case 2: hash_h2 = new H2; break;
      case 3: hash_h3 = new H3; break;
      case 4: hash_h4 = new H4; break;
      case 5: hash_h5 = new H5; break;
      case 6: hash_h6 = new H6; break;
      case 7: hash_h7 = new H7; break;
      case 8: hash_h8 = new H8; break;
      case 9: hash_h9 = new H9; break;
      default: break;
    }
  }

  template<typename Hasher>
  void WarmupHash(const size_t size, const uint8_t* dict, Hasher* hasher) {
    hasher->Init();
    for (size_t i = 0; i + Hasher::kHashTypeLength - 1 < size; i++) {
      hasher->Store(&dict[i], static_cast<uint32_t>(i));
    }
  }

  // Custom LZ77 window.
  void PrependCustomDictionary(
      int type, const size_t size, const uint8_t* dict) {
    switch (type) {
      case 2: WarmupHash(size, dict, hash_h2); break;
      case 3: WarmupHash(size, dict, hash_h3); break;
      case 4: WarmupHash(size, dict, hash_h4); break;
      case 5: WarmupHash(size, dict, hash_h5); break;
      case 6: WarmupHash(size, dict, hash_h6); break;
      case 7: WarmupHash(size, dict, hash_h7); break;
      case 8: WarmupHash(size, dict, hash_h8); break;
      case 9: WarmupHash(size, dict, hash_h9); break;
      default: break;
    }
  }


  H2* hash_h2;
  H3* hash_h3;
  H4* hash_h4;
  H5* hash_h5;
  H6* hash_h6;
  H7* hash_h7;
  H8* hash_h8;
  H9* hash_h9;
};

}  // namespace brotli

#endif  // BROTLI_ENC_HASH_H_
