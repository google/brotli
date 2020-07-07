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
static BROTLI_INLINE size_t FN(HashBytes)(const uint8_t* BROTLI_RESTRICT data) {
  const uint32_t h = BROTLI_UNALIGNED_LOAD32LE(data) * kHashMul32;
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
  uint16_t free_slot_idx[NUM_BANKS];  /* Up to 1KiB. Move to dynamic? */
  size_t max_hops;

  /* Shortcuts. */
  void* extra;
  HasherCommon* common;

  /* --- Dynamic size members --- */

  /* uint32_t addr[BUCKET_SIZE]; */

  /* uint16_t head[BUCKET_SIZE]; */

  /* Truncated hash used for quick rejection of "distance cache" candidates. */
  /* uint8_t tiny_hash[65536];*/

  /* FN(Bank) banks[NUM_BANKS]; */
} HashForgetfulChain;

static uint32_t* FN(Addr)(void* extra) {
  return (uint32_t*)extra;
}

static uint16_t* FN(Head)(void* extra) {
  return (uint16_t*)(&FN(Addr)(extra)[BUCKET_SIZE]);
}

static uint8_t* FN(TinyHash)(void* extra) {
  return (uint8_t*)(&FN(Head)(extra)[BUCKET_SIZE]);
}

static FN(Bank)* FN(Banks)(void* extra) {
  return (FN(Bank)*)(&FN(TinyHash)(extra)[65536]);
}

static void FN(Initialize)(
    HasherCommon* common, HashForgetfulChain* BROTLI_RESTRICT self,
    const BrotliEncoderParams* params) {
  self->common = common;
  self->extra = common->extra;

  self->max_hops = (params->quality > 6 ? 7u : 8u) << (params->quality - 4);
}

static void FN(Prepare)(
    HashForgetfulChain* BROTLI_RESTRICT self, BROTLI_BOOL one_shot,
    size_t input_size, const uint8_t* BROTLI_RESTRICT data) {
  uint32_t* BROTLI_RESTRICT addr = FN(Addr)(self->extra);
  uint16_t* BROTLI_RESTRICT head = FN(Head)(self->extra);
  uint8_t* BROTLI_RESTRICT tiny_hash = FN(TinyHash)(self->extra);
  /* Partial preparation is 100 times slower (per socket). */
  size_t partial_prepare_threshold = BUCKET_SIZE >> 6;
  if (one_shot && input_size <= partial_prepare_threshold) {
    size_t i;
    for (i = 0; i < input_size; ++i) {
      size_t bucket = FN(HashBytes)(&data[i]);
      /* See InitEmpty comment. */
      addr[bucket] = 0xCCCCCCCC;
      head[bucket] = 0xCCCC;
    }
  } else {
    /* Fill |addr| array with 0xCCCCCCCC value. Because of wrapping, position
       processed by hasher never reaches 3GB + 64M; this makes all new chains
       to be terminated after the first node. */
    memset(addr, 0xCC, sizeof(uint32_t) * BUCKET_SIZE);
    memset(head, 0, sizeof(uint16_t) * BUCKET_SIZE);
  }
  memset(tiny_hash, 0, sizeof(uint8_t) * 65536);
  memset(self->free_slot_idx, 0, sizeof(self->free_slot_idx));
}

static BROTLI_INLINE size_t FN(HashMemAllocInBytes)(
    const BrotliEncoderParams* params, BROTLI_BOOL one_shot,
    size_t input_size) {
  BROTLI_UNUSED(params);
  BROTLI_UNUSED(one_shot);
  BROTLI_UNUSED(input_size);
  return sizeof(uint32_t) * BUCKET_SIZE + sizeof(uint16_t) * BUCKET_SIZE +
         sizeof(uint8_t) * 65536 + sizeof(FN(Bank)) * NUM_BANKS;
}

/* Look at 4 bytes at &data[ix & mask]. Compute a hash from these, and prepend
   node to corresponding chain; also update tiny_hash for current position. */
static BROTLI_INLINE void FN(Store)(HashForgetfulChain* BROTLI_RESTRICT self,
    const uint8_t* BROTLI_RESTRICT data, const size_t mask, const size_t ix) {
  uint32_t* BROTLI_RESTRICT addr = FN(Addr)(self->extra);
  uint16_t* BROTLI_RESTRICT head = FN(Head)(self->extra);
  uint8_t* BROTLI_RESTRICT tiny_hash = FN(TinyHash)(self->extra);
  FN(Bank)* BROTLI_RESTRICT banks = FN(Banks)(self->extra);
  const size_t key = FN(HashBytes)(&data[ix & mask]);
  const size_t bank = key & (NUM_BANKS - 1);
  const size_t idx = self->free_slot_idx[bank]++ & (BANK_SIZE - 1);
  size_t delta = ix - addr[key];
  tiny_hash[(uint16_t)ix] = (uint8_t)key;
  if (delta > 0xFFFF) delta = CAPPED_CHAINS ? 0 : 0xFFFF;
  banks[bank].slots[idx].delta = (uint16_t)delta;
  banks[bank].slots[idx].next = head[key];
  addr[key] = (uint32_t)ix;
  head[key] = (uint16_t)idx;
}

static BROTLI_INLINE void FN(StoreRange)(
    HashForgetfulChain* BROTLI_RESTRICT self,
    const uint8_t* BROTLI_RESTRICT data, const size_t mask,
    const size_t ix_start, const size_t ix_end) {
  size_t i;
  for (i = ix_start; i < ix_end; ++i) {
    FN(Store)(self, data, mask, i);
  }
}

static BROTLI_INLINE void FN(StitchToPreviousBlock)(
    HashForgetfulChain* BROTLI_RESTRICT self,
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

static BROTLI_INLINE void FN(PrepareDistanceCache)(
    HashForgetfulChain* BROTLI_RESTRICT self,
    int* BROTLI_RESTRICT distance_cache) {
  BROTLI_UNUSED(self);
  PrepareDistanceCache(distance_cache, NUM_LAST_DISTANCES_TO_CHECK);
}


// static BROTLI_INLINE BROTLI_BOOL GetStaticDictReference(const size_t cur_ix, const int distance,
//                                                         const int copy_len, const size_t max_backward,
//                                                         const BrotliEncoderDictionary* dictionary,
//                                                         const size_t max_distance, const uint8_t* data) {
//   if (distance > BROTLI_MAX_ALLOWED_DISTANCE) {
//     BROTLI_LOG(("Invalid backward reference. pos: %d distance: %d "
//         "len: %d \n", cur_ix, distance, copy_len));
//     return BROTLI_FALSE;
//   }
//   if (copy_len >= BROTLI_MIN_DICTIONARY_WORD_LENGTH &&
//       copy_len <= BROTLI_MAX_DICTIONARY_WORD_LENGTH) {
//     int address = distance - (int)max_backward - 1;
//     const BrotliDictionary* words = dictionary->words;
//     // const BrotliTransforms* transforms = s->transforms;
//     size_t offset = dictionary->words->offsets_by_length[copy_len];
//     uint32_t shift = dictionary->words->size_bits_by_length[copy_len];
//     int mask = (int)BitMask(shift);
//     int word_idx = address & mask;
//     int transform_idx = address >> shift;
//     offset += word_idx * copy_len;
//     if (transform_idx < (int)dictionary->cutoffTransformsCount) {
//       const uint8_t* word = &words->data[offset];
//       printf("copy_len=%d, transform_idx=%d, word_idx=%d\n", copy_len, transform_idx, word_idx);
//       printf("word=");
//       for (int i = 0; i < 5; ++i) {
//         printf("%u", word[i]);
//       }
//       size_t backward = max_backward + 1 + (size_t)word_idx +
//           ((int)transform_idx << dictionary->words->size_bits_by_length[copy_len]);
//       if (backward > max_distance) {
//         printf("backward > max_distance\n");
//         // return BROTLI_FALSE;
//       }
//       size_t matchlen = FindMatchLengthWithLimit(data, &dictionary->words->data[offset], copy_len);
//       printf("backward=%zu, matchlen=%zu, len_code_delta=%d\n", backward, matchlen, (int)copy_len - (int)matchlen);
//
//       // if (transform_idx == transforms->cutOffTransforms[0]) {
//       //
//       // }
//     } else {
//       BROTLI_LOG(("Invalid backward reference. pos: %d distance: %d "
//           "len: %d\n", cur_ix, distance, copy_len));
//       return BROTLI_FALSE;
//     }
//   } else {
//     BROTLI_LOG(("Invalid backward reference. pos: %d distance: %d "
//         "len: %d\n",  cur_ix, distance, copy_len));
//     return BROTLI_FALSE;
//   }
//   return BROTLI_TRUE;
// }



// if (s->distance_code > s->max_distance) {
//   if (s->distance_code > BROTLI_MAX_ALLOWED_DISTANCE) {
//     BROTLI_LOG(("Invalid backward reference. pos: %d distance: %d "
//         "len: %d bytes left: %d\n",
//         pos, s->distance_code, i, s->meta_block_remaining_len));
//     return BROTLI_FAILURE(BROTLI_DECODER_ERROR_FORMAT_DISTANCE);
//   }
//   if (i >= BROTLI_MIN_DICTIONARY_WORD_LENGTH &&
//       i <= BROTLI_MAX_DICTIONARY_WORD_LENGTH) {
//     int address = s->distance_code - s->max_distance - 1;
//     const BrotliDictionary* words = s->dictionary;
//     const BrotliTransforms* transforms = s->transforms;
//     int offset = (int)s->dictionary->offsets_by_length[i];
//     uint32_t shift = s->dictionary->size_bits_by_length[i];
//
//     int mask = (int)BitMask(shift);
//     int word_idx = address & mask;
//     int transform_idx = address >> shift;
//     /* Compensate double distance-ring-buffer roll. */
//     s->dist_rb_idx += s->distance_context;
//     offset += word_idx * i;
//     if (BROTLI_PREDICT_FALSE(!words->data)) {
//       return BROTLI_FAILURE(BROTLI_DECODER_ERROR_DICTIONARY_NOT_SET);
//     }
//     if (transform_idx < (int)transforms->num_transforms) {
//       const uint8_t* word = &words->data[offset];
//       int len = i;
//       if (transform_idx == transforms->cutOffTransforms[0]) {
//         memcpy(&s->ringbuffer[pos], word, (size_t)len);
//         BROTLI_LOG(("[ProcessCommandsInternal] dictionary word: [%.*s]\n",
//                     len, word));
//       } else {
//         len = BrotliTransformDictionaryWord(&s->ringbuffer[pos], word, len,
//             transforms, transform_idx);
//         BROTLI_LOG(("[ProcessCommandsInternal] dictionary word: [%.*s],"
//                     " transform_idx = %d, transformed: [%.*s]\n",
//                     i, word, transform_idx, len, &s->ringbuffer[pos]));
//       }
//       pos += len;
//       s->meta_block_remaining_len -= len;
//       if (pos >= s->ringbuffer_size) {
//         s->state = BROTLI_STATE_COMMAND_POST_WRITE_1;
//         goto saveStateAndReturn;
//       }
//     } else {
//       BROTLI_LOG(("Invalid backward reference. pos: %d distance: %d "
//           "len: %d bytes left: %d\n",
//           pos, s->distance_code, i, s->meta_block_remaining_len));
//       return BROTLI_FAILURE(BROTLI_DECODER_ERROR_FORMAT_TRANSFORM);
//     }
//   } else {
//     BROTLI_LOG(("Invalid backward reference. pos: %d distance: %d "
//         "len: %d bytes left: %d\n",
//         pos, s->distance_code, i, s->meta_block_remaining_len));
//     return BROTLI_FAILURE(BROTLI_DECODER_ERROR_FORMAT_DICTIONARY);
//   }
// }


/* Find a longest backward match of &data[cur_ix] up to the length of
   max_length and stores the position cur_ix in the hash table.

   REQUIRES: FN(PrepareDistanceCache) must be invoked for current distance cache
             values; if this method is invoked repeatedly with the same distance
             cache values, it is enough to invoke FN(PrepareDistanceCache) once.

   Does not look for matches longer than max_length.
   Does not look for matches further away than max_backward.
   Writes the best match into |out|.
   |out|->score is updated only if a better match is found. */
static BROTLI_INLINE void FN(FindLongestMatch)(
    HashForgetfulChain* BROTLI_RESTRICT self,
    const BrotliEncoderDictionary* dictionary,
    const uint8_t* BROTLI_RESTRICT data, const size_t ring_buffer_mask,
    const int* BROTLI_RESTRICT distance_cache,
    const size_t cur_ix, const size_t max_length, const size_t max_backward,
    const size_t dictionary_distance, const size_t max_distance,
    HasherSearchResult* BROTLI_RESTRICT out,
    BackwardReference** backward_references,
    size_t* back_refs_position, size_t back_refs_size) {
  uint32_t* BROTLI_RESTRICT addr = FN(Addr)(self->extra);
  uint16_t* BROTLI_RESTRICT head = FN(Head)(self->extra);
  uint8_t* BROTLI_RESTRICT tiny_hashes = FN(TinyHash)(self->extra);
  FN(Bank)* BROTLI_RESTRICT banks = FN(Banks)(self->extra);
  const size_t cur_ix_masked = cur_ix & ring_buffer_mask;
  /* Don't accept a short copy from far away. */
  score_t min_score = out->score;
  score_t best_score = out->score;
  size_t best_len = out->len;
  size_t i;
  const size_t key = FN(HashBytes)(&data[cur_ix_masked]);
  const uint8_t tiny_hash = (uint8_t)(key);
  out->len = 0;
  out->len_code_delta = 0;

  while (*back_refs_position < back_refs_size &&
        (*backward_references)[*back_refs_position].position < cur_ix) {
     ++(*back_refs_position);
  }
  if (back_refs_size != 0) {
    /* If we have some backward reference stored for this position check it first
       or if we have backward reference before that intersect with this position */
    if (*back_refs_position < back_refs_size &&
       (*backward_references)[*back_refs_position].position == cur_ix) {
       const size_t backward = (size_t)(*backward_references)[*back_refs_position].distance;
       size_t prev_ix = (cur_ix - backward);
       if (prev_ix < cur_ix && backward <= max_backward) {
           prev_ix &= ring_buffer_mask;
           {
               size_t len = FindMatchLengthWithLimit(&data[prev_ix],
                                                     &data[cur_ix_masked],
                                                     max_length);
               if (len > (*backward_references)[*back_refs_position].copy_len) {
                 len = (*backward_references)[*back_refs_position].copy_len;
               }
               if (len >= 2) {
                  score_t score = BackwardReferenceScore(len, backward);
                  // if (best_score < score) {
                  if (BROTLI_TRUE) {
                      best_score = score;
                      best_len = len;
                      out->len = best_len;
                      out->distance = backward;
                      out->score = best_score;
                      out->used_stored = BROTLI_TRUE;
                      return;
                  }
                }
           }
       }
    }
  } else {
    /* Try last distance first. */
    for (i = 0; i < NUM_LAST_DISTANCES_TO_CHECK; ++i) {
      const size_t backward = (size_t)distance_cache[i];
      size_t prev_ix = (cur_ix - backward);
      /* For distance code 0 we want to consider 2-byte matches. */
      if (i > 0 && tiny_hashes[(uint16_t)prev_ix] != tiny_hash) continue;
      if (prev_ix >= cur_ix || backward > max_backward) {
        continue;
      }
      prev_ix &= ring_buffer_mask;
      {
        const size_t len = FindMatchLengthWithLimit(&data[prev_ix],
                                                    &data[cur_ix_masked],
                                                    max_length);
        if (len >= 2) {
          score_t score = BackwardReferenceScoreUsingLastDistance(len);
          if (best_score < score) {
            if (i != 0) score -= BackwardReferencePenaltyUsingLastDistance(i);
            if (best_score < score) {
              best_score = score;
              best_len = len;
              out->len = best_len;
              out->distance = backward;
              out->score = best_score;
            }
          }
        }
      }
    }
    {
      const size_t bank = key & (NUM_BANKS - 1);
      size_t backward = 0;
      size_t hops = self->max_hops;
      size_t delta = cur_ix - addr[key];
      size_t slot = head[key];
      while (hops--) {
        size_t prev_ix;
        size_t last = slot;
        backward += delta;
        if (backward > max_backward || (CAPPED_CHAINS && !delta)) break;
        prev_ix = (cur_ix - backward) & ring_buffer_mask;
        slot = banks[bank].slots[last].next;
        delta = banks[bank].slots[last].delta;
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
            }
          }
        }
      }
      FN(Store)(self, data, ring_buffer_mask, cur_ix);
    }
  }
  if (out->score == min_score) {
    if (back_refs_size == 0) {
      SearchInStaticDictionary(dictionary,
          self->common, &data[cur_ix_masked], max_length, dictionary_distance,
          max_distance, out, BROTLI_FALSE);
    } else {
      if (*back_refs_position < back_refs_size && (*backward_references)[*back_refs_position].position == cur_ix && (*backward_references)[*back_refs_position].distance > max_backward) {
        BROTLI_BOOL is_ok = GetStaticDictReference(cur_ix, (*backward_references)[*back_refs_position].distance,
                                                   (*backward_references)[*back_refs_position].copy_len, max_backward,
                                                   dictionary, max_distance, &data[cur_ix_masked], out);
        out->used_stored = BROTLI_TRUE;
      }
    }
  }
}

#undef BANK_SIZE
#undef BUCKET_SIZE
#undef CAPPED_CHAINS

#undef HashForgetfulChain
