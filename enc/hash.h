/* Copyright 2010 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* A (forgetful) hash table to the data seen by the compressor, to
   help create backward references to previous data. */

#ifndef BROTLI_ENC_HASH_H_
#define BROTLI_ENC_HASH_H_

#include <string.h>  /* memcmp, memset */

#include "../common/constants.h"
#include "../common/dictionary.h"
#include "../common/types.h"
#include "./dictionary_hash.h"
#include "./fast_log.h"
#include "./find_match_length.h"
#include "./memory.h"
#include "./port.h"
#include "./quality.h"
#include "./static_dict.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#define MAX_TREE_SEARCH_DEPTH 64
#define MAX_TREE_COMP_LENGTH 128
#define score_t size_t

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

typedef struct HasherSearchResult {
  size_t len;
  size_t len_x_code; /* == len ^ len_code */
  size_t distance;
  score_t score;
} HasherSearchResult;

typedef struct DictionarySearchStatictics {
  size_t num_lookups;
  size_t num_matches;
} DictionarySearchStatictics;

/* kHashMul32 multiplier has these properties:
   * The multiplier must be odd. Otherwise we may lose the highest bit.
   * No long streaks of 1s or 0s.
   * There is no effort to ensure that it is a prime, the oddity is enough
     for this use.
   * The number has been tuned heuristically against compression benchmarks. */
static const uint32_t kHashMul32 = 0x1e35a7bd;

static BROTLI_INLINE uint32_t Hash14(const uint8_t* data) {
  uint32_t h = BROTLI_UNALIGNED_LOAD32(data) * kHashMul32;
  /* The higher bits contain more mixture from the multiplication,
     so we take our results from there. */
  return h >> (32 - 14);
}

#define BROTLI_LITERAL_BYTE_SCORE 540
#define BROTLI_DISTANCE_BIT_PENALTY 120
/* Score must be positive after applying maximal penalty. */
#define BROTLI_SCORE_BASE (BROTLI_DISTANCE_BIT_PENALTY * 8 * sizeof(size_t))

/* Usually, we always choose the longest backward reference. This function
   allows for the exception of that rule.

   If we choose a backward reference that is further away, it will
   usually be coded with more bits. We approximate this by assuming
   log2(distance). If the distance can be expressed in terms of the
   last four distances, we use some heuristic constants to estimate
   the bits cost. For the first up to four literals we use the bit
   cost of the literals from the literal cost model, after that we
   use the average bit cost of the cost model.

   This function is used to sometimes discard a longer backward reference
   when it is not much longer and the bit cost for encoding it is more
   than the saved literals.

   backward_reference_offset MUST be positive. */
static BROTLI_INLINE score_t BackwardReferenceScore(
    size_t copy_length, size_t backward_reference_offset) {
  return BROTLI_SCORE_BASE + BROTLI_LITERAL_BYTE_SCORE * (score_t)copy_length -
      BROTLI_DISTANCE_BIT_PENALTY * Log2FloorNonZero(backward_reference_offset);
}

static const score_t kDistanceShortCodeCost[BROTLI_NUM_DISTANCE_SHORT_CODES] = {
  /* Repeat last */
  BROTLI_SCORE_BASE +  60,
  /* 2nd, 3rd, 4th last */
  BROTLI_SCORE_BASE -  95,
  BROTLI_SCORE_BASE - 117,
  BROTLI_SCORE_BASE - 127,
  /* Last with offset */
  BROTLI_SCORE_BASE -  93,
  BROTLI_SCORE_BASE -  93,
  BROTLI_SCORE_BASE -  96,
  BROTLI_SCORE_BASE -  96,
  BROTLI_SCORE_BASE -  99,
  BROTLI_SCORE_BASE -  99,
  /* 2nd last with offset */
  BROTLI_SCORE_BASE - 105,
  BROTLI_SCORE_BASE - 105,
  BROTLI_SCORE_BASE - 115,
  BROTLI_SCORE_BASE - 115,
  BROTLI_SCORE_BASE - 125,
  BROTLI_SCORE_BASE - 125
};

static BROTLI_INLINE score_t BackwardReferenceScoreUsingLastDistance(
    size_t copy_length, size_t distance_short_code) {
  return BROTLI_LITERAL_BYTE_SCORE * (score_t)copy_length +
      kDistanceShortCodeCost[distance_short_code];
}

static BROTLI_INLINE void DictionarySearchStaticticsReset(
    DictionarySearchStatictics* self) {
  self->num_lookups = 0;
  self->num_matches = 0;
}

static BROTLI_INLINE BROTLI_BOOL TestStaticDictionaryItem(
    size_t item, const uint8_t* data, size_t max_length, size_t max_backward,
    HasherSearchResult* out) {
  size_t len;
  size_t dist;
  size_t offset;
  size_t matchlen;
  size_t backward;
  score_t score;
  len = item & 31;
  dist = item >> 5;
  offset = kBrotliDictionaryOffsetsByLength[len] + len * dist;
  if (len > max_length) {
    return BROTLI_FALSE;
  }

  matchlen = FindMatchLengthWithLimit(data, &kBrotliDictionary[offset], len);
  if (matchlen + kCutoffTransformsCount <= len || matchlen == 0) {
    return BROTLI_FALSE;
  }
  {
    size_t transform_id = kCutoffTransforms[len - matchlen];
    backward = max_backward + dist + 1 +
        (transform_id << kBrotliDictionarySizeBitsByLength[len]);
  }
  score = BackwardReferenceScore(matchlen, backward);
  if (score < out->score) {
    return BROTLI_FALSE;
  }
  out->len = matchlen;
  out->len_x_code = len ^ matchlen;
  out->distance = backward;
  out->score = score;
  return BROTLI_TRUE;
}

static BROTLI_INLINE BROTLI_BOOL SearchInStaticDictionary(
    DictionarySearchStatictics* self, const uint8_t* data, size_t max_length,
    size_t max_backward, HasherSearchResult* out, BROTLI_BOOL shallow) {
  size_t key;
  size_t i;
  BROTLI_BOOL is_match_found = BROTLI_FALSE;
  if (self->num_matches < (self->num_lookups >> 7)) {
    return BROTLI_FALSE;
  }
  key = Hash14(data) << 1;
  for (i = 0; i < (shallow ? 1 : 2); ++i, ++key) {
    size_t item = kStaticDictionaryHash[key];
    self->num_lookups++;
    if (item != 0 &&
        TestStaticDictionaryItem(item, data, max_length, max_backward, out)) {
      self->num_matches++;
      is_match_found = BROTLI_TRUE;
    }
  }
  return is_match_found;
}

typedef struct BackwardMatch {
  uint32_t distance;
  uint32_t length_and_code;
} BackwardMatch;

static BROTLI_INLINE void InitBackwardMatch(BackwardMatch* self,
    size_t dist, size_t len) {
  self->distance = (uint32_t)dist;
  self->length_and_code = (uint32_t)(len << 5);
}

static BROTLI_INLINE void InitDictionaryBackwardMatch(BackwardMatch* self,
    size_t dist, size_t len, size_t len_code) {
  self->distance = (uint32_t)dist;
  self->length_and_code =
      (uint32_t)((len << 5) | (len == len_code ? 0 : len_code));
}

static BROTLI_INLINE size_t BackwardMatchLength(const BackwardMatch* self) {
  return self->length_and_code >> 5;
}

static BROTLI_INLINE size_t BackwardMatchLengthCode(const BackwardMatch* self) {
  size_t code = self->length_and_code & 31;
  return code ? code : BackwardMatchLength(self);
}

#define EXPAND_CAT(a, b) CAT(a, b)
#define CAT(a, b) a ## b
#define FN(X) EXPAND_CAT(X, HASHER())

#define MAX_NUM_MATCHES_H10 (64 + MAX_TREE_SEARCH_DEPTH)

#define HASHER() H10
#define HashToBinaryTree HASHER()

#define BUCKET_BITS 17
#define BUCKET_SIZE (1 << BUCKET_BITS)

static size_t FN(HashTypeLength)(void) { return 4; }
static size_t FN(StoreLookahead)(void) { return MAX_TREE_COMP_LENGTH; }

static uint32_t FN(HashBytes)(const uint8_t *data) {
  uint32_t h = BROTLI_UNALIGNED_LOAD32(data) * kHashMul32;
  /* The higher bits contain more mixture from the multiplication,
     so we take our results from there. */
  return h >> (32 - BUCKET_BITS);
}

/* A (forgetful) hash table where each hash bucket contains a binary tree of
   sequences whose first 4 bytes share the same hash code.
   Each sequence is MAX_TREE_COMP_LENGTH long and is identified by its starting
   position in the input data. The binary tree is sorted by the lexicographic
   order of the sequences, and it is also a max-heap with respect to the
   starting positions. */
typedef struct HashToBinaryTree {
  /* The window size minus 1 */
  size_t window_mask_;

  /* Hash table that maps the 4-byte hashes of the sequence to the last
     position where this hash was found, which is the root of the binary
     tree of sequences that share this hash bucket. */
  uint32_t buckets_[BUCKET_SIZE];

  /* The union of the binary trees of each hash bucket. The root of the tree
     corresponding to a hash is a sequence starting at buckets_[hash] and
     the left and right children of a sequence starting at pos are
     forest_[2 * pos] and forest_[2 * pos + 1]. */
  uint32_t* forest_;

  /* A position used to mark a non-existent sequence, i.e. a tree is empty if
     its root is at invalid_pos_ and a node is a leaf if both its children
     are at invalid_pos_. */
  uint32_t invalid_pos_;

  size_t forest_size_;
  BROTLI_BOOL is_dirty_;
} HashToBinaryTree;

static void FN(Reset)(HashToBinaryTree* self) {
  self->is_dirty_ = BROTLI_TRUE;
}

static void FN(Initialize)(HashToBinaryTree* self) {
  self->forest_ = NULL;
  self->forest_size_ = 0;
  FN(Reset)(self);
}

static void FN(Cleanup)(MemoryManager* m, HashToBinaryTree* self) {
  BROTLI_FREE(m, self->forest_);
}

static void FN(Init)(
    MemoryManager* m, HashToBinaryTree* self, const uint8_t* data,
    const BrotliEncoderParams* params, size_t position, size_t bytes,
    BROTLI_BOOL is_last) {
  if (self->is_dirty_) {
    uint32_t invalid_pos;
    size_t num_nodes;
    uint32_t i;
    BROTLI_UNUSED(data);
    self->window_mask_ = (1u << params->lgwin) - 1u;
    invalid_pos = (uint32_t)(0 - self->window_mask_);
    self->invalid_pos_ = invalid_pos;
    for (i = 0; i < BUCKET_SIZE; i++) {
      self->buckets_[i] = invalid_pos;
    }
    num_nodes = (position == 0 && is_last) ? bytes : self->window_mask_ + 1;
    if (num_nodes > self->forest_size_) {
      BROTLI_FREE(m, self->forest_);
      self->forest_ = BROTLI_ALLOC(m, uint32_t, 2 * num_nodes);
      if (BROTLI_IS_OOM(m)) return;
      self->forest_size_ = num_nodes;
    }
    self->is_dirty_ = BROTLI_FALSE;
  }
}

static BROTLI_INLINE size_t FN(LeftChildIndex)(HashToBinaryTree* self,
    const size_t pos) {
  return 2 * (pos & self->window_mask_);
}

static BROTLI_INLINE size_t FN(RightChildIndex)(HashToBinaryTree* self,
    const size_t pos) {
  return 2 * (pos & self->window_mask_) + 1;
}

/* Stores the hash of the next 4 bytes and in a single tree-traversal, the
   hash bucket's binary tree is searched for matches and is re-rooted at the
   current position.

   If less than MAX_TREE_COMP_LENGTH data is available, the hash bucket of the
   current position is searched for matches, but the state of the hash table
   is not changed, since we can not know the final sorting order of the
   current (incomplete) sequence.

   This function must be called with increasing cur_ix positions. */
static BROTLI_INLINE BackwardMatch* FN(StoreAndFindMatches)(
    HashToBinaryTree* self, const uint8_t* const BROTLI_RESTRICT data,
    const size_t cur_ix, const size_t ring_buffer_mask, const size_t max_length,
    const size_t max_backward, size_t* const BROTLI_RESTRICT best_len,
    BackwardMatch* BROTLI_RESTRICT matches) {
  const size_t cur_ix_masked = cur_ix & ring_buffer_mask;
  const size_t max_comp_len =
      BROTLI_MIN(size_t, max_length, MAX_TREE_COMP_LENGTH);
  const BROTLI_BOOL should_reroot_tree =
      TO_BROTLI_BOOL(max_length >= MAX_TREE_COMP_LENGTH);
  const uint32_t key = FN(HashBytes)(&data[cur_ix_masked]);
  size_t prev_ix = self->buckets_[key];
  /* The forest index of the rightmost node of the left subtree of the new
     root, updated as we traverse and reroot the tree of the hash bucket. */
  size_t node_left = FN(LeftChildIndex)(self, cur_ix);
  /* The forest index of the leftmost node of the right subtree of the new
     root, updated as we traverse and reroot the tree of the hash bucket. */
  size_t node_right = FN(RightChildIndex)(self, cur_ix);
  /* The match length of the rightmost node of the left subtree of the new
     root, updated as we traverse and reroot the tree of the hash bucket. */
  size_t best_len_left = 0;
  /* The match length of the leftmost node of the right subtree of the new
     root, updated as we traverse and reroot the tree of the hash bucket. */
  size_t best_len_right = 0;
  size_t depth_remaining;
  if (should_reroot_tree) {
    self->buckets_[key] = (uint32_t)cur_ix;
  }
  for (depth_remaining = MAX_TREE_SEARCH_DEPTH; ; --depth_remaining) {
    const size_t backward = cur_ix - prev_ix;
    const size_t prev_ix_masked = prev_ix & ring_buffer_mask;
    if (backward == 0 || backward > max_backward || depth_remaining == 0) {
      if (should_reroot_tree) {
        self->forest_[node_left] = self->invalid_pos_;
        self->forest_[node_right] = self->invalid_pos_;
      }
      break;
    }
    {
      const size_t cur_len = BROTLI_MIN(size_t, best_len_left, best_len_right);
      size_t len;
      assert(cur_len <= MAX_TREE_COMP_LENGTH);
      len = cur_len +
          FindMatchLengthWithLimit(&data[cur_ix_masked + cur_len],
                                   &data[prev_ix_masked + cur_len],
                                   max_length - cur_len);
      assert(0 == memcmp(&data[cur_ix_masked], &data[prev_ix_masked], len));
      if (matches && len > *best_len) {
        *best_len = len;
        InitBackwardMatch(matches++, backward, len);
      }
      if (len >= max_comp_len) {
        if (should_reroot_tree) {
          self->forest_[node_left] =
              self->forest_[FN(LeftChildIndex)(self, prev_ix)];
          self->forest_[node_right] =
              self->forest_[FN(RightChildIndex)(self, prev_ix)];
        }
        break;
      }
      if (data[cur_ix_masked + len] > data[prev_ix_masked + len]) {
        best_len_left = len;
        if (should_reroot_tree) {
          self->forest_[node_left] = (uint32_t)prev_ix;
        }
        node_left = FN(RightChildIndex)(self, prev_ix);
        prev_ix = self->forest_[node_left];
      } else {
        best_len_right = len;
        if (should_reroot_tree) {
          self->forest_[node_right] = (uint32_t)prev_ix;
        }
        node_right = FN(LeftChildIndex)(self, prev_ix);
        prev_ix = self->forest_[node_right];
      }
    }
  }
  return matches;
}

/* Finds all backward matches of &data[cur_ix & ring_buffer_mask] up to the
   length of max_length and stores the position cur_ix in the hash table.

   Sets *num_matches to the number of matches found, and stores the found
   matches in matches[0] to matches[*num_matches - 1]. The matches will be
   sorted by strictly increasing length and (non-strictly) increasing
   distance. */
static BROTLI_INLINE size_t FN(FindAllMatches)(HashToBinaryTree* self,
    const uint8_t* data, const size_t ring_buffer_mask, const size_t cur_ix,
    const size_t max_length, const size_t max_backward,
    const BrotliEncoderParams* params, BackwardMatch* matches) {
  BackwardMatch* const orig_matches = matches;
  const size_t cur_ix_masked = cur_ix & ring_buffer_mask;
  size_t best_len = 1;
  const size_t short_match_max_backward =
      params->quality != HQ_ZOPFLIFICATION_QUALITY ? 16 : 64;
  size_t stop = cur_ix - short_match_max_backward;
  uint32_t dict_matches[BROTLI_MAX_STATIC_DICTIONARY_MATCH_LEN + 1];
  size_t i;
  if (cur_ix < short_match_max_backward) { stop = 0; }
  for (i = cur_ix - 1; i > stop && best_len <= 2; --i) {
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
    {
      const size_t len =
          FindMatchLengthWithLimit(&data[prev_ix], &data[cur_ix_masked],
                                   max_length);
      if (len > best_len) {
        best_len = len;
        InitBackwardMatch(matches++, backward, len);
      }
    }
  }
  if (best_len < max_length) {
    matches = FN(StoreAndFindMatches)(self, data, cur_ix, ring_buffer_mask,
        max_length, max_backward, &best_len, matches);
  }
  for (i = 0; i <= BROTLI_MAX_STATIC_DICTIONARY_MATCH_LEN; ++i) {
    dict_matches[i] = kInvalidMatch;
  }
  {
    size_t minlen = BROTLI_MAX(size_t, 4, best_len + 1);
    if (BrotliFindAllStaticDictionaryMatches(&data[cur_ix_masked], minlen,
                                             max_length, &dict_matches[0])) {
      size_t maxlen = BROTLI_MIN(
          size_t, BROTLI_MAX_STATIC_DICTIONARY_MATCH_LEN, max_length);
      size_t l;
      for (l = minlen; l <= maxlen; ++l) {
        uint32_t dict_id = dict_matches[l];
        if (dict_id < kInvalidMatch) {
          InitDictionaryBackwardMatch(matches++,
              max_backward + (dict_id >> 5) + 1, l, dict_id & 31);
        }
      }
    }
  }
  return (size_t)(matches - orig_matches);
}

/* Stores the hash of the next 4 bytes and re-roots the binary tree at the
   current sequence, without returning any matches.
   REQUIRES: ix + MAX_TREE_COMP_LENGTH <= end-of-current-block */
static BROTLI_INLINE void FN(Store)(HashToBinaryTree* self, const uint8_t *data,
    const size_t mask, const size_t ix) {
  /* Maximum distance is window size - 16, see section 9.1. of the spec. */
  const size_t max_backward = self->window_mask_ - 15;
  FN(StoreAndFindMatches)(self, data, ix, mask, MAX_TREE_COMP_LENGTH,
      max_backward, NULL, NULL);
}

static BROTLI_INLINE void FN(StoreRange)(HashToBinaryTree* self,
    const uint8_t *data, const size_t mask, const size_t ix_start,
    const size_t ix_end) {
  size_t i = ix_start + 63 <= ix_end ? ix_end - 63 : ix_start;
  for (; i < ix_end; ++i) {
    FN(Store)(self, data, mask, i);
  }
}

static BROTLI_INLINE void FN(StitchToPreviousBlock)(HashToBinaryTree* self,
    size_t num_bytes, size_t position, const uint8_t* ringbuffer,
    size_t ringbuffer_mask) {
  if (num_bytes >= FN(HashTypeLength)() - 1 &&
      position >= MAX_TREE_COMP_LENGTH) {
    /* Store the last `MAX_TREE_COMP_LENGTH - 1` positions in the hasher.
       These could not be calculated before, since they require knowledge
       of both the previous and the current block. */
    const size_t i_start = position - MAX_TREE_COMP_LENGTH + 1;
    const size_t i_end = BROTLI_MIN(size_t, position, i_start + num_bytes);
    size_t i;
    for (i = i_start; i < i_end; ++i) {
      /* Maximum distance is window size - 16, see section 9.1. of the spec.
         Furthermore, we have to make sure that we don't look further back
         from the start of the next block than the window size, otherwise we
         could access already overwritten areas of the ringbuffer. */
      const size_t max_backward =
          self->window_mask_ - BROTLI_MAX(size_t, 15, position - i);
      /* We know that i + MAX_TREE_COMP_LENGTH <= position + num_bytes, i.e. the
         end of the current block and that we have at least
         MAX_TREE_COMP_LENGTH tail in the ringbuffer. */
      FN(StoreAndFindMatches)(self, ringbuffer, i, ringbuffer_mask,
          MAX_TREE_COMP_LENGTH, max_backward, NULL, NULL);
    }
  }
}

#undef BUCKET_SIZE
#undef BUCKET_BITS

#undef HASHER

/* For BUCKET_SWEEP == 1, enabling the dictionary lookup makes compression
   a little faster (0.5% - 1%) and it compresses 0.15% better on small text
   and html inputs. */

#define HASHER() H2
#define BUCKET_BITS 16
#define BUCKET_SWEEP 1
#define USE_DICTIONARY 1
#include "./hash_longest_match_quickly_inc.h"  /* NOLINT(build/include) */
#undef BUCKET_SWEEP
#undef USE_DICTIONARY
#undef HASHER

#define HASHER() H3
#define BUCKET_SWEEP 2
#define USE_DICTIONARY 0
#include "./hash_longest_match_quickly_inc.h"  /* NOLINT(build/include) */
#undef USE_DICTIONARY
#undef BUCKET_SWEEP
#undef BUCKET_BITS
#undef HASHER

#define HASHER() H4
#define BUCKET_BITS 17
#define BUCKET_SWEEP 4
#define USE_DICTIONARY 1
#include "./hash_longest_match_quickly_inc.h"  /* NOLINT(build/include) */
#undef USE_DICTIONARY
#undef BUCKET_SWEEP
#undef BUCKET_BITS
#undef HASHER

#define HASHER() H5
#define BUCKET_BITS 14
#define BLOCK_BITS 4
#define NUM_LAST_DISTANCES_TO_CHECK 4
#include "./hash_longest_match_inc.h"  /* NOLINT(build/include) */
#undef BLOCK_BITS
#undef HASHER

#define HASHER() H6
#define BLOCK_BITS 5
#include "./hash_longest_match_inc.h"  /* NOLINT(build/include) */
#undef NUM_LAST_DISTANCES_TO_CHECK
#undef BLOCK_BITS
#undef BUCKET_BITS
#undef HASHER

#define HASHER() H7
#define BUCKET_BITS 15
#define BLOCK_BITS 6
#define NUM_LAST_DISTANCES_TO_CHECK 10
#include "./hash_longest_match_inc.h"  /* NOLINT(build/include) */
#undef BLOCK_BITS
#undef HASHER

#define HASHER() H8
#define BLOCK_BITS 7
#include "./hash_longest_match_inc.h"  /* NOLINT(build/include) */
#undef NUM_LAST_DISTANCES_TO_CHECK
#undef BLOCK_BITS
#undef HASHER

#define HASHER() H9
#define BLOCK_BITS 8
#define NUM_LAST_DISTANCES_TO_CHECK 16
#include "./hash_longest_match_inc.h"  /* NOLINT(build/include) */
#undef NUM_LAST_DISTANCES_TO_CHECK
#undef BLOCK_BITS
#undef BUCKET_BITS
#undef HASHER

#define BUCKET_BITS 15

#define NUM_LAST_DISTANCES_TO_CHECK 4
#define NUM_BANKS 1
#define BANK_BITS 16
#define HASHER() H40
#include "./hash_forgetful_chain_inc.h"  /* NOLINT(build/include) */
#undef HASHER
#undef NUM_LAST_DISTANCES_TO_CHECK

#define NUM_LAST_DISTANCES_TO_CHECK 10
#define HASHER() H41
#include "./hash_forgetful_chain_inc.h"  /* NOLINT(build/include) */
#undef HASHER
#undef NUM_LAST_DISTANCES_TO_CHECK
#undef NUM_BANKS
#undef BANK_BITS

#define NUM_LAST_DISTANCES_TO_CHECK 16
#define NUM_BANKS 512
#define BANK_BITS 9
#define HASHER() H42
#include "./hash_forgetful_chain_inc.h"  /* NOLINT(build/include) */
#undef HASHER
#undef NUM_LAST_DISTANCES_TO_CHECK
#undef NUM_BANKS
#undef BANK_BITS

#undef BUCKET_BITS

#undef FN
#undef CAT
#undef EXPAND_CAT

#define FOR_GENERIC_HASHERS(H) H(2) H(3) H(4) H(5) H(6) H(7) H(8) H(9) \
                               H(40) H(41) H(42)
#define FOR_ALL_HASHERS(H) FOR_GENERIC_HASHERS(H) H(10)

typedef struct Hashers {
#define _MEMBER(N) H ## N* h ## N;
  FOR_ALL_HASHERS(_MEMBER)
#undef _MEMBER
} Hashers;

static BROTLI_INLINE void InitHashers(Hashers* self) {
#define _INIT(N) self->h ## N = 0;
  FOR_ALL_HASHERS(_INIT)
#undef _INIT
}

static BROTLI_INLINE void DestroyHashers(MemoryManager* m, Hashers* self) {
  if (self->h10) CleanupH10(m, self->h10);
#define _CLEANUP(N) BROTLI_FREE(m, self->h ## N)
  FOR_ALL_HASHERS(_CLEANUP)
#undef _CLEANUP
}

static BROTLI_INLINE void HashersReset(Hashers* self, int type) {
  switch (type) {
#define _RESET(N) case N: ResetH ## N(self->h ## N); break;
    FOR_ALL_HASHERS(_RESET)
#undef _RESET
    default: break;
  }
}

static BROTLI_INLINE void HashersSetup(
    MemoryManager* m, Hashers* self, int type) {
  switch (type) {
#define _SETUP(N) case N: self->h ## N = BROTLI_ALLOC(m, H ## N, 1); break;
    FOR_ALL_HASHERS(_SETUP)
#undef _SETUP
    default: break;
  }
  if (BROTLI_IS_OOM(m)) return;
  if (type == 10) InitializeH10(self->h10);
  HashersReset(self, type);
}

#define _WARMUP_HASH(N)                                                        \
static BROTLI_INLINE void WarmupHashH ## N(MemoryManager* m,                   \
    const BrotliEncoderParams* params, const size_t size, const uint8_t* dict, \
    H ## N* hasher) {                                                          \
  size_t overlap = (StoreLookaheadH ## N()) - 1;                               \
  size_t i;                                                                    \
  InitH ## N(m, hasher, dict, params, 0, size, BROTLI_FALSE);                  \
  if (BROTLI_IS_OOM(m)) return;                                                \
  for (i = 0; i + overlap < size; i++) {                                       \
    StoreH ## N(hasher, dict, ~(size_t)0, i);                                  \
  }                                                                            \
}
FOR_ALL_HASHERS(_WARMUP_HASH)
#undef _WARMUP_HASH

/* Custom LZ77 window. */
static BROTLI_INLINE void HashersPrependCustomDictionary(
    MemoryManager* m, Hashers* self, const BrotliEncoderParams* params,
    const size_t size, const uint8_t* dict) {
  int hasher_type = ChooseHasher(params);
  switch (hasher_type) {
#define _PREPEND(N) \
    case N: WarmupHashH ## N(m, params, size, dict, self->h ## N); break;
    FOR_ALL_HASHERS(_PREPEND)
#undef _PREPEND
    default: break;
  }
  if (BROTLI_IS_OOM(m)) return;
}


#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif

#endif  /* BROTLI_ENC_HASH_H_ */
