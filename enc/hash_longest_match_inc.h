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
  int is_dirty_;

  size_t num_dict_lookups_;
  size_t num_dict_matches_;
} HashLongestMatch;

static void FN(Reset)(HashLongestMatch* self) {
  self->is_dirty_ = 1;
  self->num_dict_lookups_ = 0;
  self->num_dict_matches_ = 0;
}

static void FN(InitEmpty)(HashLongestMatch* self) {
  if (self->is_dirty_) {
    memset(self->num_, 0, sizeof(self->num_));
    self->is_dirty_ = 0;
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
    self->is_dirty_ = 0;
  }
}

static void FN(Init)(
    MemoryManager* m, HashLongestMatch* self, const uint8_t* data, int lgwin,
    size_t position, size_t bytes, int is_last) {
  /* Choose which init method is faster.
     Init() is about 100 times faster than InitForData(). */
  const size_t kMaxBytesForPartialHashInit = HASH_MAP_SIZE >> 7;
  BROTLI_UNUSED(m);
  BROTLI_UNUSED(lgwin);
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
   Writes the best found match length into best_len_out.
   Writes the index (&data[index]) offset from the start of the best match
   into best_distance_out.
   Write the score of the best match into best_score_out.
   Returns 1 when match is found, otherwise 0. */
static BROTLI_INLINE int FN(FindLongestMatch)(HashLongestMatch* self,
    const uint8_t* BROTLI_RESTRICT data, const size_t ring_buffer_mask,
    const int* BROTLI_RESTRICT distance_cache, const size_t cur_ix,
    const size_t max_length, const size_t max_backward,
    size_t* BROTLI_RESTRICT best_len_out,
    size_t* BROTLI_RESTRICT best_len_code_out,
    size_t* BROTLI_RESTRICT best_distance_out,
    double* BROTLI_RESTRICT best_score_out) {
  const size_t cur_ix_masked = cur_ix & ring_buffer_mask;
  int is_match_found = 0;
  /* Don't accept a short copy from far away. */
  double best_score = *best_score_out;
  size_t best_len = *best_len_out;
  size_t i;
  *best_len_code_out = 0;
  *best_len_out = 0;
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
        double score = BackwardReferenceScoreUsingLastDistance(len, i);
        if (best_score < score) {
          best_score = score;
          best_len = len;
          *best_len_out = best_len;
          *best_len_code_out = best_len;
          *best_distance_out = backward;
          *best_score_out = best_score;
          is_match_found = 1;
        }
      }
    }
  }
  {
    const uint32_t key = FN(HashBytes)(&data[cur_ix_masked]);
    const uint32_t * BROTLI_RESTRICT const bucket =
        &self->buckets_[key << BLOCK_BITS];
    const size_t down =
        (self->num_[key] > BLOCK_SIZE) ? (self->num_[key] - BLOCK_SIZE) : 0u;
    for (i = self->num_[key]; i > down;) {
      size_t prev_ix = bucket[--i & BLOCK_MASK];
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
      {
        const size_t len = FindMatchLengthWithLimit(&data[prev_ix],
                                                    &data[cur_ix_masked],
                                                    max_length);
        if (len >= 4) {
          /* Comparing for >= 3 does not change the semantics, but just saves
             for a few unnecessary binary logarithms in backward reference
             score, since we are not interested in such short matches. */
          double score = BackwardReferenceScore(len, backward);
          if (best_score < score) {
            best_score = score;
            best_len = len;
            *best_len_out = best_len;
            *best_len_code_out = best_len;
            *best_distance_out = backward;
            *best_score_out = best_score;
            is_match_found = 1;
          }
        }
      }
    }
    self->buckets_[(key << BLOCK_BITS) + (self->num_[key] & BLOCK_MASK)] =
        (uint32_t)cur_ix;
    ++self->num_[key];
  }
  if (!is_match_found &&
      self->num_dict_matches_ >= (self->num_dict_lookups_ >> 7)) {
    size_t dict_key = Hash14(&data[cur_ix_masked]) << 1;
    int k;
    for (k = 0; k < 2; ++k, ++dict_key) {
      const uint16_t v = kStaticDictionaryHash[dict_key];
      ++self->num_dict_lookups_;
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
            const size_t transform_step =
                (size_t)1 << kBrotliDictionarySizeBitsByLength[len];
            const size_t word_id = dist + transform_id * transform_step;
            const size_t backward = max_backward + word_id + 1;
            double score = BackwardReferenceScore(matchlen, backward);
            if (best_score < score) {
              ++self->num_dict_matches_;
              best_score = score;
              best_len = matchlen;
              *best_len_out = best_len;
              *best_len_code_out = len;
              *best_distance_out = backward;
              *best_score_out = best_score;
              is_match_found = 1;
            }
          }
        }
      }
    }
  }
  return is_match_found;
}

#undef HASH_MAP_SIZE
#undef BLOCK_MASK
#undef BLOCK_SIZE
#undef BUCKET_SIZE

#undef HashLongestMatch
