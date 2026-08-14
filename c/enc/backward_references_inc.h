/* NOLINT(build/header_guard) */
/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* template parameters: EXPORT_FN, FN */

#ifndef BROTLI_READ_RING_BUFFER_64_DEFINED
#define BROTLI_READ_RING_BUFFER_64_DEFINED
static BROTLI_INLINE uint64_t ReadRingBuffer64(const uint8_t* ringbuffer, size_t mask, size_t pos) {
  size_t idx = pos & mask;
  if (idx + 8 <= mask + 1) {
    return BrotliUnalignedRead64(&ringbuffer[idx]);
  } else {
    uint64_t val;
    uint8_t* val_ptr = (uint8_t*)&val;
    size_t i;
    for (i = 0; i < 8; ++i) {
      val_ptr[i] = ringbuffer[(pos + i) & mask];
    }
    return val;
  }
}
#endif

static BROTLI_NOINLINE void EXPORT_FN(CreateBackwardReferences)(
    size_t num_bytes, size_t position,
    const uint8_t* ringbuffer, size_t ringbuffer_mask,
    ContextLut literal_context_lut, const BrotliEncoderParams* params,
    Hasher* hasher, int* dist_cache, size_t* last_insert_len,
    Command* commands, size_t* num_commands, size_t* num_literals) {
  HASHER()* privat = &hasher->privat.FN(_);
  /* Set maximum distance, see section 9.1. of the spec. */
  const size_t max_backward_limit = BROTLI_MAX_BACKWARD_LIMIT(params->lgwin);
  const size_t position_offset = params->stream_offset;

  const Command* const orig_commands = commands;
  size_t insert_length = *last_insert_len;
  const size_t pos_end = position + num_bytes;
  const size_t store_end = num_bytes >= FN(StoreLookahead)() ?
      position + num_bytes - FN(StoreLookahead)() + 1 : position;

  /* For speed up heuristics for random data. */
  const size_t random_heuristics_window_size =
      LiteralSpreeLengthForSparseSearch(params);
  size_t apply_random_heuristics = position + random_heuristics_window_size;
  const size_t gap = params->dictionary.compound.total_size;

  /* Minimum score to accept a backward reference. */
  const score_t kMinScore = BROTLI_SCORE_BASE + 100;

  FN(PrepareDistanceCache)(privat, dist_cache);

  size_t next_base64_pos = pos_end;
  if (params->base64_mode &&
      (hasher->common.num_base64_regions < params->max_base64_regions ||
       hasher->common.num_sub_b64_regions < params->max_base64_regions)) {
    next_base64_pos =
        FindNextBase64Trigger(ringbuffer, ringbuffer_mask, position, pos_end);
  }
  while (position + FN(HashTypeLength)() < pos_end) {
    if (position >= next_base64_pos) {
      /* Find where it ends */
      size_t scan_pos = position + kBase64TriggerLen;
      size_t first_equal_pos = 0;
      while (scan_pos < pos_end) {
        uint8_t c = ringbuffer[scan_pos & ringbuffer_mask];
        if (IsBase64Char(c)) {
          if (first_equal_pos != 0) {
            scan_pos = first_equal_pos;
            break;
          }
          scan_pos++;
        } else if (c == '=') {
          if (first_equal_pos == 0) {
            first_equal_pos = scan_pos;
          }
          scan_pos++;
        } else {
          break;
        }
      }
      /* Jump directly to the end of base64 block */
      /* Skip the ';base64,' trigger */
      size_t start_pos = position + kBase64TriggerLen;
      size_t length = scan_pos - start_pos;
      /* Exclude '=' characters from the flat 6-bit entropy block */
      while (length > 0 &&
             ringbuffer[(start_pos + length - 1) & ringbuffer_mask] == '=') {
        length--;
      }
      if (length >= kMinBase64DeduplicationLen) {
        if (params->base64_mode >= 2) {
          BROTLI_BOOL match_found = BROTLI_FALSE;
          size_t best_hist_pos = 0;
          size_t mlen = scan_pos - position;
          BROTLI_BOOL is_macro = (length >= params->min_base64_region_len);
          Base64Region* search_regions = is_macro ? hasher->common.base64_regions : hasher->common.sub_b64_regions;
          size_t search_num = is_macro ? hasher->common.num_base64_regions : hasher->common.num_sub_b64_regions;
          size_t r;
          for (r = search_num; r > 0; --r) {
            size_t idx = r - 1;
            size_t hist_start_literal_pos =
                search_regions[idx].start_literal_pos;
            size_t hist_length = search_regions[idx].length;
            /* Avoid size_t underflow if start_literal_pos is small */
            if (hist_start_literal_pos >= kBase64TriggerLen) {
              size_t hist_pos = hist_start_literal_pos - kBase64TriggerLen;
              size_t dist = position - hist_pos;
              /* Verify distance complies with maximum backward limits, ringbuffer capacity,
                 and encoder configuration to prevent referencing overwritten history. */
              if (dist >= 1 &&
                  dist <= max_backward_limit &&
                  dist <= ringbuffer_mask &&
                  dist <= params->dist.max_distance) {
                /* Count historical trailing '=' padding characters to compute exact payload size */
                size_t hist_num_equals = 0;
                while (hist_start_literal_pos + hist_length + hist_num_equals < position &&
                       ringbuffer[(hist_start_literal_pos + hist_length + hist_num_equals) & ringbuffer_mask] == '=') {
                  hist_num_equals++;
                }
                /* O(1) length check before comparing payload bytes. This ensures that
                   non-duplicate blocks (common in production) are rejected immediately in O(1)
                   without triggering O(N * length) string comparisons. */
                if (hist_length + hist_num_equals == mlen - kBase64TriggerLen) {
                  size_t compare_len = hist_length + hist_num_equals;
                  /* Dual-Ended (Prefix + Suffix) 64-Bit Payload Quick Rejection */
                  uint64_t hist_prefix = ReadRingBuffer64(ringbuffer, ringbuffer_mask, hist_start_literal_pos);
                  uint64_t curr_prefix = ReadRingBuffer64(ringbuffer, ringbuffer_mask, position + kBase64TriggerLen);
                  if (hist_prefix == curr_prefix) {
                    uint64_t hist_suffix = ReadRingBuffer64(ringbuffer, ringbuffer_mask, hist_start_literal_pos + compare_len - 8);
                    uint64_t curr_suffix = ReadRingBuffer64(ringbuffer, ringbuffer_mask, position + kBase64TriggerLen + compare_len - 8);
                    if (hist_suffix == curr_suffix) {
                      if (RingBufferCompare(ringbuffer, ringbuffer_mask,
                                            hist_start_literal_pos,
                                            position + kBase64TriggerLen,
                                            compare_len)) {
                        match_found = BROTLI_TRUE;
                        best_hist_pos = hist_pos;
                        /* Nearest-Neighbor Anchor Tracking & MRU Cache Promotion */
                        search_regions[idx].start_literal_pos = start_pos;
                        if (idx < search_num - 1) {
                          Base64Region matched_reg = search_regions[idx];
                          size_t k;
                          for (k = idx; k < search_num - 1; ++k) {
                            search_regions[k] = search_regions[k + 1];
                          }
                          search_regions[search_num - 1] = matched_reg;
                        }
                        break;
                      }
                    }
                  }
                }
              }
            }
          }
          if (match_found && mlen >= kMinBase64DeduplicationLen) {
            size_t dictionary_start = BROTLI_MIN(size_t,
                position + position_offset, max_backward_limit);
            size_t dist = position - best_hist_pos;
            size_t distance_code = ComputeDistanceCode(
                dist, dictionary_start + gap, dist_cache);
            if ((dist <= (dictionary_start + gap)) && distance_code > 0) {
              dist_cache[3] = dist_cache[2];
              dist_cache[2] = dist_cache[1];
              dist_cache[1] = dist_cache[0];
              dist_cache[0] = (int)dist;
              FN(PrepareDistanceCache)(privat, dist_cache);
            }
            InitCommand(commands++, &params->dist, insert_length,
                mlen, 0, distance_code);
            *num_literals += insert_length;
            insert_length = 0;

            /* Bounded Entry/Exit Anchor Hasher Seeding */
            {
              size_t entry_start = position + 2;
              size_t entry_end = BROTLI_MIN(size_t, position + 6, store_end);
              if (entry_start < entry_end) {
                FN(StoreRange)(privat, ringbuffer, ringbuffer_mask, entry_start, entry_end);
              }
              size_t exit_start = (position + mlen >= 4) ? (position + mlen - 4) : 0;
              if (exit_start < entry_end) {
                exit_start = entry_end;
              }
              size_t exit_end = BROTLI_MIN(size_t, position + mlen, store_end);
              if (exit_start < exit_end) {
                FN(StoreRange)(privat, ringbuffer, ringbuffer_mask, exit_start, exit_end);
              }
            }

            position += mlen;
            apply_random_heuristics = position + random_heuristics_window_size;
            if (hasher->common.num_base64_regions < params->max_base64_regions ||
                hasher->common.num_sub_b64_regions < params->max_base64_regions) {
              next_base64_pos = FindNextBase64Trigger(ringbuffer, ringbuffer_mask,
                                                      position, pos_end);
            } else {
              next_base64_pos = pos_end;
            }
            continue;
          }

          if (is_macro) {
            if (hasher->common.num_base64_regions < params->max_base64_regions) {
              hasher->common.base64_regions[hasher->common.num_base64_regions]
                  .start_literal_pos = start_pos;
              hasher->common.base64_regions[hasher->common.num_base64_regions]
                  .length = length;
              hasher->common.num_base64_regions++;
            }
          } else {
            if (hasher->common.num_sub_b64_regions < params->max_base64_regions) {
              hasher->common.sub_b64_regions[hasher->common.num_sub_b64_regions]
                  .start_literal_pos = start_pos;
              hasher->common.sub_b64_regions[hasher->common.num_sub_b64_regions]
                  .length = length;
              hasher->common.num_sub_b64_regions++;
            }
          }
        } else {
          /* HEAD Base64 Mode: pure detection & histogram splitting, no deduplication */
          if (hasher->common.num_base64_regions < params->max_base64_regions) {
            hasher->common.base64_regions[hasher->common.num_base64_regions]
                .start_literal_pos = start_pos;
            hasher->common.base64_regions[hasher->common.num_base64_regions]
                .length = length;
            hasher->common.num_base64_regions++;
          }
        }
        insert_length += (scan_pos - position);
        position = scan_pos;
        apply_random_heuristics = position + random_heuristics_window_size;
        FN(StoreRange)(privat, ringbuffer, ringbuffer_mask, position,
                       BROTLI_MIN(size_t, position + 4, store_end));
        if (hasher->common.num_base64_regions < params->max_base64_regions ||
            hasher->common.num_sub_b64_regions < params->max_base64_regions) {
          next_base64_pos = FindNextBase64Trigger(ringbuffer, ringbuffer_mask,
                                                  position, pos_end);
        } else {
          next_base64_pos = pos_end;
        }
        continue;
      } else {
        if (hasher->common.num_base64_regions < params->max_base64_regions ||
            hasher->common.num_sub_b64_regions < params->max_base64_regions) {
          next_base64_pos = FindNextBase64Trigger(ringbuffer, ringbuffer_mask,
                                                  scan_pos, pos_end);
        } else {
          next_base64_pos = pos_end;
        }
        continue;
      }
    }
    size_t max_length = pos_end - position;
    size_t max_distance = BROTLI_MIN(size_t, position, max_backward_limit);
    size_t dictionary_start = BROTLI_MIN(size_t,
        position + position_offset, max_backward_limit);
    HasherSearchResult sr;
    int dict_id = 0;
    uint8_t p1 = 0;
    uint8_t p2 = 0;
    if (params->dictionary.contextual.context_based) {
      p1 = position >= 1 ?
          ringbuffer[(size_t)(position - 1) & ringbuffer_mask] : 0;
      p2 = position >= 2 ?
          ringbuffer[(size_t)(position - 2) & ringbuffer_mask] : 0;
      dict_id = params->dictionary.contextual.context_map[
          BROTLI_CONTEXT(p1, p2, literal_context_lut)];
    }
    sr.len = 0;
    sr.len_code_delta = 0;
    sr.distance = 0;
    sr.score = kMinScore;
    FN(FindLongestMatch)(privat, params->dictionary.contextual.dict[dict_id],
        ringbuffer, ringbuffer_mask, dist_cache, position, max_length,
        max_distance, dictionary_start + gap, params->dist.max_distance, &sr);
    if (ENABLE_COMPOUND_DICTIONARY) {
      LookupCompoundDictionaryMatch(&params->dictionary.compound, ringbuffer,
          ringbuffer_mask, dist_cache, position, max_length,
          dictionary_start, params->dist.max_distance, &sr);
    }
    if (sr.score > kMinScore) {
      /* Found a match. Let's look for something even better ahead. */
      int delayed_backward_references_in_row = 0;
      --max_length;
      for (;; --max_length) {
        const score_t cost_diff_lazy = 175;
        HasherSearchResult sr2;
        sr2.len = params->quality < MIN_QUALITY_FOR_EXTENSIVE_REFERENCE_SEARCH ?
            BROTLI_MIN(size_t, sr.len - 1, max_length) : 0;
        sr2.len_code_delta = 0;
        sr2.distance = 0;
        sr2.score = kMinScore;
        max_distance = BROTLI_MIN(size_t, position + 1, max_backward_limit);
        dictionary_start = BROTLI_MIN(size_t,
            position + 1 + position_offset, max_backward_limit);
        if (params->dictionary.contextual.context_based) {
          p2 = p1;
          p1 = ringbuffer[position & ringbuffer_mask];
          dict_id = params->dictionary.contextual.context_map[
              BROTLI_CONTEXT(p1, p2, literal_context_lut)];
        }
        FN(FindLongestMatch)(privat,
            params->dictionary.contextual.dict[dict_id],
            ringbuffer, ringbuffer_mask, dist_cache, position + 1, max_length,
            max_distance, dictionary_start + gap, params->dist.max_distance,
            &sr2);
        if (ENABLE_COMPOUND_DICTIONARY) {
          LookupCompoundDictionaryMatch(
              &params->dictionary.compound, ringbuffer,
              ringbuffer_mask, dist_cache, position + 1, max_length,
              dictionary_start, params->dist.max_distance, &sr2);
        }
        if (sr2.score >= sr.score + cost_diff_lazy) {
          /* Ok, let's just write one byte for now and start a match from the
             next byte. */
          ++position;
          ++insert_length;
          sr = sr2;
          if (++delayed_backward_references_in_row < 4 &&
              position + FN(HashTypeLength)() < pos_end) {
            continue;
          }
        }
        break;
      }
      apply_random_heuristics =
          position + 2 * sr.len + random_heuristics_window_size;
      dictionary_start = BROTLI_MIN(size_t,
          position + position_offset, max_backward_limit);
      {
        /* The first 16 codes are special short-codes,
           and the minimum offset is 1. */
        size_t distance_code = ComputeDistanceCode(
            sr.distance, dictionary_start + gap, dist_cache);
        if ((sr.distance <= (dictionary_start + gap)) && distance_code > 0) {
          dist_cache[3] = dist_cache[2];
          dist_cache[2] = dist_cache[1];
          dist_cache[1] = dist_cache[0];
          dist_cache[0] = (int)sr.distance;
          FN(PrepareDistanceCache)(privat, dist_cache);
        }
        InitCommand(commands++, &params->dist, insert_length,
            sr.len, sr.len_code_delta, distance_code);
      }
      *num_literals += insert_length;
      insert_length = 0;
      /* Put the hash keys into the table, if there are enough bytes left.
         Depending on the hasher implementation, it can push all positions
         in the given range or only a subset of them.
         Avoid hash poisoning with RLE data. */
      {
        size_t range_start = position + 2;
        size_t range_end = BROTLI_MIN(size_t, position + sr.len, store_end);
        if (sr.distance < (sr.len >> 2)) {
          range_start = BROTLI_MIN(size_t, range_end, BROTLI_MAX(size_t,
              range_start, position + sr.len - (sr.distance << 2)));
        }
        FN(StoreRange)(privat, ringbuffer, ringbuffer_mask, range_start,
                       range_end);
      }
      position += sr.len;
    } else {
      ++insert_length;
      ++position;
      /* If we have not seen matches for a long time, we can skip some
         match lookups. Unsuccessful match lookups are very very expensive
         and this kind of a heuristic speeds up compression quite
         a lot. */
      if (position > apply_random_heuristics) {
        /* Going through uncompressible data, jump. */
        if (position >
            apply_random_heuristics + 4 * random_heuristics_window_size) {
          /* It is quite a long time since we saw a copy, so we assume
             that this data is not compressible, and store hashes less
             often. Hashes of non compressible data are less likely to
             turn out to be useful in the future, too, so we store less of
             them to not to flood out the hash table of good compressible
             data. */
          const size_t kMargin =
              BROTLI_MAX(size_t, FN(StoreLookahead)() - 1, 4);
          size_t pos_jump =
              BROTLI_MIN(size_t, position + 16, pos_end - kMargin);
          for (; position < pos_jump; position += 4) {
            FN(Store)(privat, ringbuffer, ringbuffer_mask, position);
            insert_length += 4;
          }
        } else {
          const size_t kMargin =
              BROTLI_MAX(size_t, FN(StoreLookahead)() - 1, 2);
          size_t pos_jump =
              BROTLI_MIN(size_t, position + 8, pos_end - kMargin);
          for (; position < pos_jump; position += 2) {
            FN(Store)(privat, ringbuffer, ringbuffer_mask, position);
            insert_length += 2;
          }
        }
      }
    }
  }
  insert_length += pos_end - position;
  *last_insert_len = insert_length;
  *num_commands += (size_t)(commands - orig_commands);
}
