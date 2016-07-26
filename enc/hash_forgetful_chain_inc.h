/* NOLINT(build/header_guard) */
/* Copyright 2016 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* template parameters: FN, BUCKET_BITS, NUM_BANKS, BANK_BITS,
                        NUM_LAST_DISTANCES_TO_CHECK */

/* A (forgetful) hash table to the data seen by the compressor, to
   help create backward references to previous data.

   Hashes are stored in chains which are bucketed to groups. Group of chains
   share a storage "bank". When more than "bank size" chain nodes are added,
   oldest nodes are replaced; this way several chains may share a tail. */

#define HashForgetfulChain HASHER()

#define BANK_SIZE (1 << BANK_BITS)

/* Number of hash buckets. */
#define BUCKET_SIZE (1 << BUCKET_BITS)

#define CAPPED_CHAINS 0

static BROTLI_INLINE size_t FN(HashTypeLength)(void) { return 4; }
static BROTLI_INLINE size_t FN(StoreLookahead)(void) { return 4; }

/* HashBytes is the function that chooses the bucket to place the address in.*/
static BROTLI_INLINE size_t FN(HashBytes)(const uint8_t *data) {
  const uint32_t h = BROTLI_UNALIGNED_LOAD32(data) * kHashMul32;
  /* The higher bits contain more mixture from the multiplication,
     so we take our results from there. */
  return h >> (32 - BUCKET_BITS);
}

typedef struct FN(Slot) {
  uint16_t delta;
  uint16_t next;
} FN(Slot);

typedef struct FN(Bank) {
  FN(Slot) slots[BANK_SIZE];
} FN(Bank);

typedef struct HashForgetfulChain {
  uint32_t addr[BUCKET_SIZE];
  uint16_t head[BUCKET_SIZE];
  /* Truncated hash used for quick rejection of "distance cache" candidates. */
  uint8_t tiny_hash[65536];
  FN(Bank) banks[NUM_BANKS];
  uint16_t free_slot_idx[NUM_BANKS];
  BROTLI_BOOL is_dirty_;
  DictionarySearchStatictics dict_search_stats_;
  size_t max_hops;
} HashForgetfulChain;

static void FN(Reset)(HashForgetfulChain* self) {
  self->is_dirty_ = BROTLI_TRUE;
  DictionarySearchStaticticsReset(&self->dict_search_stats_);
}

static void FN(InitEmpty)(HashForgetfulChain* self) {
  if (self->is_dirty_) {
    /* Fill |addr| array with 0xCCCCCCCC value. Because of wrapping, position
       processed by hasher never reaches 3GB + 64M; this makes all new chains
       to be terminated after the first node. */
    memset(self->addr, 0xCC, sizeof(self->addr));
    memset(self->head, 0, sizeof(self->head));
    memset(self->tiny_hash, 0, sizeof(self->tiny_hash));
    memset(self->free_slot_idx, 0, sizeof(self->free_slot_idx));
    self->is_dirty_ = BROTLI_FALSE;
  }
}

static void FN(InitForData)(HashForgetfulChain* self, const uint8_t* data,
    size_t num) {
  size_t i;
  for (i = 0; i < num; ++i) {
    size_t bucket = FN(HashBytes)(&data[i]);
    /* See InitEmpty comment. */
    self->addr[bucket] = 0xCCCCCCCC;
    self->head[bucket] = 0xCCCC;
  }
  memset(self->tiny_hash, 0, sizeof(self->tiny_hash));
  memset(self->free_slot_idx, 0, sizeof(self->free_slot_idx));
  if (num != 0) {
    self->is_dirty_ = BROTLI_FALSE;
  }
}

static void FN(Init)(
    MemoryManager* m, HashForgetfulChain* self, const uint8_t* data,
    const BrotliEncoderParams* params, size_t position, size_t bytes,
    BROTLI_BOOL is_last) {
  /* Choose which init method is faster.
     Init() is about 100 times faster than InitForData(). */
  const size_t kMaxBytesForPartialHashInit = BUCKET_SIZE >> 6;
  BROTLI_UNUSED(m);
  self->max_hops = (params->quality > 6 ? 7u : 8u) << (params->quality - 4);
  if (position == 0 && is_last && bytes <= kMaxBytesForPartialHashInit) {
    FN(InitForData)(self, data, bytes);
  } else {
    FN(InitEmpty)(self);
  }
}

/* Look at 4 bytes at &data[ix & mask]. Compute a hash from these, and prepend
   node to corresponding chain; also update tiny_hash for current position. */
static BROTLI_INLINE void FN(Store)(HashForgetfulChain* BROTLI_RESTRICT self,
    const uint8_t* BROTLI_RESTRICT data, const size_t mask, const size_t ix) {
  const size_t key = FN(HashBytes)(&data[ix & mask]);
  const size_t bank = key & (NUM_BANKS - 1);
  const size_t idx = self->free_slot_idx[bank]++ & (BANK_SIZE - 1);
  size_t delta = ix - self->addr[key];
  self->tiny_hash[(uint16_t)ix] = (uint8_t)key;
  if (delta > 0xFFFF) delta = CAPPED_CHAINS ? 0 : 0xFFFF;
  self->banks[bank].slots[idx].delta = (uint16_t)delta;
  self->banks[bank].slots[idx].next = self->head[key];
  self->addr[key] = (uint32_t)ix;
  self->head[key] = (uint16_t)idx;
}

static BROTLI_INLINE void FN(StoreRange)(HashForgetfulChain* self,
    const uint8_t *data, const size_t mask, const size_t ix_start,
    const size_t ix_end) {
  size_t i;
  for (i = ix_start; i < ix_end; ++i) {
    FN(Store)(self, data, mask, i);
  }
}

static BROTLI_INLINE void FN(StitchToPreviousBlock)(HashForgetfulChain* self,
    size_t num_bytes, size_t position, const uint8_t* ringbuffer,
    size_t ring_buffer_mask) {
  if (num_bytes >= FN(HashTypeLength)() - 1 && position >= 3) {
    /* Prepare the hashes for three last bytes of the last write.
       These could not be calculated before, since they require knowledge
       of both the previous and the current block. */
    FN(Store)(self, ringbuffer, ring_buffer_mask, position - 3);
    FN(Store)(self, ringbuffer, ring_buffer_mask, position - 2);
    FN(Store)(self, ringbuffer, ring_buffer_mask, position - 1);
  }
}

/* Find a longest backward match of &data[cur_ix] up to the length of
   max_length and stores the position cur_ix in the hash table.

   Does not look for matches longer than max_length.
   Does not look for matches further away than max_backward.
   Writes the best match into |out|.
   Returns 1 when match is found, otherwise 0. */
static BROTLI_INLINE BROTLI_BOOL FN(FindLongestMatch)(
    HashForgetfulChain* self, const uint8_t* BROTLI_RESTRICT data,
    const size_t ring_buffer_mask, const int* BROTLI_RESTRICT distance_cache,
    const size_t cur_ix, const size_t max_length, const size_t max_backward,
    HasherSearchResult* BROTLI_RESTRICT out) {
  const size_t cur_ix_masked = cur_ix & ring_buffer_mask;
  BROTLI_BOOL is_match_found = BROTLI_FALSE;
  /* Don't accept a short copy from far away. */
  score_t best_score = out->score;
  size_t best_len = out->len;
  size_t i;
  const size_t key = FN(HashBytes)(&data[cur_ix_masked]);
  const uint8_t tiny_hash = (uint8_t)(key);
  out->len = 0;
  out->len_x_code = 0;
  /* Try last distance first. */
  for (i = 0; i < NUM_LAST_DISTANCES_TO_CHECK; ++i) {
    const size_t idx = kDistanceCacheIndex[i];
    const size_t backward =
        (size_t)(distance_cache[idx] + kDistanceCacheOffset[i]);
    size_t prev_ix = (cur_ix - backward);
    if (i > 0 && self->tiny_hash[(uint16_t)prev_ix] != tiny_hash) continue;
    if (prev_ix >= cur_ix || backward > max_backward) {
      continue;
    }
    prev_ix &= ring_buffer_mask;
    {
      const size_t len = FindMatchLengthWithLimit(&data[prev_ix],
                                                  &data[cur_ix_masked],
                                                  max_length);
      if (len >= 2) {
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
    const size_t bank = key & (NUM_BANKS - 1);
    size_t backward = 0;
    size_t hops = self->max_hops;
    size_t delta = cur_ix - self->addr[key];
    size_t slot = self->head[key];
    while (hops--) {
      size_t prev_ix;
      size_t last = slot;
      backward += delta;
      if (backward > max_backward || (CAPPED_CHAINS && !delta)) break;
      prev_ix = (cur_ix - backward) & ring_buffer_mask;
      slot = self->banks[bank].slots[last].next;
      delta = self->banks[bank].slots[last].delta;
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
    FN(Store)(self, data, ring_buffer_mask, cur_ix);
  }
  if (!is_match_found) {
    is_match_found = SearchInStaticDictionary(&self->dict_search_stats_,
        &data[cur_ix_masked], max_length, max_backward, out, BROTLI_FALSE);
  }
  return is_match_found;
}

#undef BANK_SIZE
#undef BUCKET_SIZE
#undef CAPPED_CHAINS

#undef HashForgetfulChain
