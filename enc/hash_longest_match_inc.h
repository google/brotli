/* NOLINT(build/header_guard) */
/* Copyright 2010 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* template parameters: FN, BUCKET_BITS, BLOCK_BITS,
                        NUM_LAST_DISTANCES_TO_CHECK */

/* A (forgetful) hash table to the data seen by the compressor, to
   help create backward references to previous data.

   This is a hash map of fixed size (BUCKET_SIZE) to a ring buffer of
   fixed size (BLOCK_SIZE). The ring buffer contains the last BLOCK_SIZE
   index positions of the given hash key in the compressed data. */

#define HashLongestMatch HASHER()

/* Number of hash buckets. */
#define BUCKET_SIZE (1 << BUCKET_BITS)

/* Only BLOCK_SIZE newest backward references are kept,
   and the older are forgotten. */
#define BLOCK_SIZE (1u << BLOCK_BITS)

/* Mask for accessing entries in a block (in a ringbuffer manner). */
#define BLOCK_MASK ((1 << BLOCK_BITS) - 1)

#define HASH_MAP_SIZE (2 << BUCKET_BITS)

static BROTLI_INLINE size_t FN(HashTypeLength)(void) { return 4; }
static BROTLI_INLINE size_t FN(StoreLookahead)(void) { return 4; }

/* HashBytes is the function that chooses the bucket to place
   the address in. The HashLongestMatch and HashLongestMatchQuickly
   classes have separate, different implementations of hashing. */
static uint32_t FN(HashBytes)(const uint8_t *data) {
  uint32_t h = BROTLI_UNALIGNED_LOAD32(data) * kHashMul32;
  /* The higher bits contain more mixture from the multiplication,
     so we take our results from there. */
  return h >> (32 - BUCKET_BITS);
}

typedef struct HashLongestMatch {
  /* Number of entries in a particular bucket. */
  uint16_t num_[BUCKET_SIZE];

  /* Buckets containing BLOCK_SIZE of backward references. */
  uint32_t buckets_[BLOCK_SIZE << BUCKET_BITS];

  /* True if num_ array needs to be initialized. */
  BROTLI_BOOL is_dirty_;

  DictionarySearchStatictics dict_search_stats_;
} HashLongestMatch;

static void FN(Reset)(HashLongestMatch* self) {
  self->is_dirty_ = BROTLI_TRUE;
  DictionarySearchStaticticsReset(&self->dict_search_stats_);
}

static void FN(InitEmpty)(HashLongestMatch* self) {
  if (self->is_dirty_) {
    memset(self->num_, 0, sizeof(self->num_));
    self->is_dirty_ = BROTLI_FALSE;
  }
}

static void FN(InitForData)(HashLongestMatch* self, const uint8_t* data,
    size_t num) {
  size_t i;
  for (i = 0; i < num; ++i) {
    const uint32_t key = FN(HashBytes)(&data[i]);
    self->num_[key] = 0;
  }
  if (num != 0) {
    self->is_dirty_ = BROTLI_FALSE;
  }
}

static void FN(Init)(
    MemoryManager* m, HashLongestMatch* self, const uint8_t* data,
    const BrotliEncoderParams* params, size_t position, size_t bytes,
    BROTLI_BOOL is_last) {
  /* Choose which init method is faster.
     Init() is about 100 times faster than InitForData(). */
  const size_t kMaxBytesForPartialHashInit = HASH_MAP_SIZE >> 7;
  BROTLI_UNUSED(m);
  BROTLI_UNUSED(params);
  if (position == 0 && is_last && bytes <= kMaxBytesForPartialHashInit) {
    FN(InitForData)(self, data, bytes);
  } else {
    FN(InitEmpty)(self);
  }
}

/* Look at 4 bytes at &data[ix & mask].
   Compute a hash from these, and store the value of ix at that position. */
static BROTLI_INLINE void FN(Store)(HashLongestMatch* self, const uint8_t *data,
    const size_t mask, const size_t ix) {
  const uint32_t key = FN(HashBytes)(&data[ix & mask]);
  const size_t minor_ix = self->num_[key] & BLOCK_MASK;
  self->buckets_[minor_ix + (key << BLOCK_BITS)] = (uint32_t)ix;
  ++self->num_[key];
}

static BROTLI_INLINE void FN(StoreRange)(HashLongestMatch* self,
    const uint8_t *data, const size_t mask, const size_t ix_start,
    const size_t ix_end) {
  size_t i;
  for (i = ix_start; i < ix_end; ++i) {
    FN(Store)(self, data, mask, i);
  }
}

static BROTLI_INLINE void FN(StitchToPreviousBlock)(HashLongestMatch* self,
    size_t num_bytes, size_t position, const uint8_t* ringbuffer,
    size_t ringbuffer_mask) {
  if (num_bytes >= FN(HashTypeLength)() - 1 && position >= 3) {
    /* Prepare the hashes for three last bytes of the last write.
       These could not be calculated before, since they require knowledge
       of both the previous and the current block. */
    FN(Store)(self, ringbuffer, ringbuffer_mask, position - 3);
    FN(Store)(self, ringbuffer, ringbuffer_mask, position - 2);
    FN(Store)(self, ringbuffer, ringbuffer_mask, position - 1);
  }
}

/* Find a longest backward match of &data[cur_ix] up to the length of
   max_length and stores the position cur_ix in the hash table.

   Does not look for matches longer than max_length.
   Does not look for matches further away than max_backward.
   Writes the best match into |out|.
   Returns true when match is found, otherwise false. */
static BROTLI_INLINE BROTLI_BOOL FN(FindLongestMatch)(HashLongestMatch* self,
    const uint8_t* BROTLI_RESTRICT data, const size_t ring_buffer_mask,
    const int* BROTLI_RESTRICT distance_cache, const size_t cur_ix,
    const size_t max_length, const size_t max_backward,
    HasherSearchResult* BROTLI_RESTRICT out) {
  const size_t cur_ix_masked = cur_ix & ring_buffer_mask;
  BROTLI_BOOL is_match_found = BROTLI_FALSE;
  /* Don't accept a short copy from far away. */
  score_t best_score = out->score;
  size_t best_len = out->len;
  size_t i;
  out->len = 0;
  out->len_x_code = 0;
  /* Try last distance first. */
  for (i = 0; i < NUM_LAST_DISTANCES_TO_CHECK; ++i) {
    const size_t idx = kDistanceCacheIndex[i];
    const size_t backward =
        (size_t)(distance_cache[idx] + kDistanceCacheOffset[i]);
    size_t prev_ix = (size_t)(cur_ix - backward);
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
    {
      const size_t len = FindMatchLengthWithLimit(&data[prev_ix],
                                                  &data[cur_ix_masked],
                                                  max_length);
      if (len >= 3 || (len == 2 && i < 2)) {
        /* Comparing for >= 2 does not change the semantics, but just saves for
           a few unnecessary binary logarithms in backward reference score,
           since we are not interested in such short matches. */
        score_t score = BackwardReferenceScoreUsingLastDistance(len, i);
        if (best_score < score) {
          best_score = score;
          best_len = len;
          out->len = best_len;
          out->distance = backward;
          out->score = best_score;
          is_match_found = BROTLI_TRUE;
        }
      }
    }
  }
  {
    const uint32_t key = FN(HashBytes)(&data[cur_ix_masked]);
    uint32_t* BROTLI_RESTRICT bucket = &self->buckets_[key << BLOCK_BITS];
    const size_t down =
        (self->num_[key] > BLOCK_SIZE) ? (self->num_[key] - BLOCK_SIZE) : 0u;
    for (i = self->num_[key]; i > down;) {
      size_t prev_ix = bucket[--i & BLOCK_MASK];
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
      {
        const size_t len = FindMatchLengthWithLimit(&data[prev_ix],
                                                    &data[cur_ix_masked],
                                                    max_length);
        if (len >= 4) {
          /* Comparing for >= 3 does not change the semantics, but just saves
             for a few unnecessary binary logarithms in backward reference
             score, since we are not interested in such short matches. */
          score_t score = BackwardReferenceScore(len, backward);
          if (best_score < score) {
            best_score = score;
            best_len = len;
            out->len = best_len;
            out->distance = backward;
            out->score = best_score;
            is_match_found = BROTLI_TRUE;
          }
        }
      }
    }
    bucket[self->num_[key] & BLOCK_MASK] = (uint32_t)cur_ix;
    ++self->num_[key];
  }
  if (!is_match_found) {
    is_match_found = SearchInStaticDictionary(&self->dict_search_stats_,
        &data[cur_ix_masked], max_length, max_backward, out, BROTLI_FALSE);
  }
  return is_match_found;
}

#undef HASH_MAP_SIZE
#undef BLOCK_MASK
#undef BLOCK_SIZE
#undef BUCKET_SIZE

#undef HashLongestMatch
