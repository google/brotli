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
#include <brotli/types.h>
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
   * No long streaks of ones or zeros.
   * There is no effort to ensure that it is a prime, the oddity is enough
     for this use.
   * The number has been tuned heuristically against compression benchmarks. */
static const uint32_t kHashMul32 = 0x1e35a7bd;
static const uint64_t kHashMul64 = BROTLI_MAKE_UINT64_T(0x1e35a7bd, 0x1e35a7bd);

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
  len = item & 0x1F;
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
  for (i = 0; i < (shallow ? 1u : 2u); ++i, ++key) {
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

#define HASHER() H10
#define BUCKET_BITS 17
#define MAX_TREE_SEARCH_DEPTH 64
#define MAX_TREE_COMP_LENGTH 128
#include "./hash_to_binary_tree_inc.h"  /* NOLINT(build/include) */
#undef MAX_TREE_SEARCH_DEPTH
#undef MAX_TREE_COMP_LENGTH
#undef BUCKET_BITS
#undef HASHER
/* MAX_NUM_MATCHES == 64 + MAX_TREE_SEARCH_DEPTH */
#define MAX_NUM_MATCHES_H10 128

/* For BUCKET_SWEEP == 1, enabling the dictionary lookup makes compression
   a little faster (0.5% - 1%) and it compresses 0.15% better on small text
   and HTML inputs. */

#define HASHER() H2
#define BUCKET_BITS 16
#define BUCKET_SWEEP 1
#define HASH_LEN 5
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
#undef HASH_LEN
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

#define HASHER() H54
#define BUCKET_BITS 20
#define BUCKET_SWEEP 4
#define HASH_LEN 7
#define USE_DICTIONARY 0
#include "./hash_longest_match_quickly_inc.h"  /* NOLINT(build/include) */
#undef USE_DICTIONARY
#undef HASH_LEN
#undef BUCKET_SWEEP
#undef BUCKET_BITS
#undef HASHER

#undef FN
#undef CAT
#undef EXPAND_CAT

#define FOR_GENERIC_HASHERS(H) H(2) H(3) H(4) H(5) H(6) H(7) H(8) H(9) \
                               H(40) H(41) H(42) H(54)
#define FOR_ALL_HASHERS(H) FOR_GENERIC_HASHERS(H) H(10)

typedef struct Hashers {
#define MEMBER_(N) H ## N* h ## N;
  FOR_ALL_HASHERS(MEMBER_)
#undef MEMBER_
} Hashers;

static BROTLI_INLINE void InitHashers(Hashers* self) {
#define INIT_(N) self->h ## N = 0;
  FOR_ALL_HASHERS(INIT_)
#undef INIT_
}

static BROTLI_INLINE void DestroyHashers(MemoryManager* m, Hashers* self) {
#define CLEANUP_(N) if (self->h ## N) CleanupH ## N(m, self->h ## N);   \
                    BROTLI_FREE(m, self->h ## N);
  FOR_ALL_HASHERS(CLEANUP_)
#undef CLEANUP_
}

static BROTLI_INLINE void HashersReset(Hashers* self, int type) {
  switch (type) {
#define RESET_(N) case N: ResetH ## N(self->h ## N); break;
    FOR_ALL_HASHERS(RESET_)
#undef RESET_
    default: break;
  }
}

static BROTLI_INLINE void HashersSetup(
    MemoryManager* m, Hashers* self, int type) {
  switch (type) {
#define SETUP_(N) case N: self->h ## N = BROTLI_ALLOC(m, H ## N, 1); break;
    FOR_ALL_HASHERS(SETUP_)
#undef SETUP_
    default: break;
  }
  if (BROTLI_IS_OOM(m)) return;
  switch (type) {
#define INITIALIZE_(N) case N: InitializeH ## N(self->h ## N); break;
    FOR_ALL_HASHERS(INITIALIZE_);
#undef INITIALIZE_
    default: break;
  }
  HashersReset(self, type);
}

#define WARMUP_HASH_(N)                                                        \
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
FOR_ALL_HASHERS(WARMUP_HASH_)
#undef WARMUP_HASH_

/* Custom LZ77 window. */
static BROTLI_INLINE void HashersPrependCustomDictionary(
    MemoryManager* m, Hashers* self, const BrotliEncoderParams* params,
    const size_t size, const uint8_t* dict) {
  int hasher_type = ChooseHasher(params);
  switch (hasher_type) {
#define PREPEND_(N) \
    case N: WarmupHashH ## N(m, params, size, dict, self->h ## N); break;
    FOR_ALL_HASHERS(PREPEND_)
#undef PREPEND_
    default: break;
  }
  if (BROTLI_IS_OOM(m)) return;
}

static BROTLI_INLINE void InitOrStitchToPreviousBlock(
    MemoryManager* m, Hashers* self, const uint8_t* data, size_t mask,
    const BrotliEncoderParams* params, size_t position,
    size_t bytes, BROTLI_BOOL is_last) {
  int hasher_type = ChooseHasher(params);
  switch (hasher_type) {
#define INIT_(N)                                                              \
    case N:                                                                   \
      InitH ## N(m, self->h ## N, data, params, position, bytes, is_last);    \
      if (BROTLI_IS_OOM(m)) return;                                           \
      StitchToPreviousBlockH ## N(self->h ## N, bytes, position, data, mask); \
    break;
    FOR_ALL_HASHERS(INIT_)
#undef INIT_
    default: break;
  }
}


#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif

#endif  /* BROTLI_ENC_HASH_H_ */
