/* NOLINT(build/header_guard) */
/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* template parameters: FN */

#define Hasher HASHER()

static void FN(CreateBackwardReferences)(MemoryManager* m,
                                         size_t num_bytes,
                                         size_t position,
                                         int is_last,
                                         const uint8_t* ringbuffer,
                                         size_t ringbuffer_mask,
                                         const int quality,
                                         const int lgwin,
                                         Hasher* hasher,
                                         int* dist_cache,
                                         size_t* last_insert_len,
                                         Command* commands,
                                         size_t* num_commands,
                                         size_t* num_literals) {
  /* Set maximum distance, see section 9.1. of the spec. */
  const size_t max_backward_limit = MaxBackwardLimit(lgwin);

  const Command * const orig_commands = commands;
  size_t insert_length = *last_insert_len;
  const size_t pos_end = position + num_bytes;
  const size_t store_end = num_bytes >= FN(StoreLookahead)() ?
      position + num_bytes - FN(StoreLookahead)() + 1 : position;

  /* For speed up heuristics for random data. */
  const size_t random_heuristics_window_size = quality < 9 ? 64 : 512;
  size_t apply_random_heuristics = position + random_heuristics_window_size;

  /* Minimum score to accept a backward reference. */
  const double kMinScore = 4.0;

  FN(Init)(m, hasher, ringbuffer, lgwin, position, num_bytes, is_last);
  if (BROTLI_IS_OOM(m)) return;
  FN(StitchToPreviousBlock)(hasher, num_bytes, position,
                            ringbuffer, ringbuffer_mask);

  while (position + FN(HashTypeLength)() < pos_end) {
    size_t max_length = pos_end - position;
    size_t max_distance = BROTLI_MIN(size_t, position, max_backward_limit);
    size_t best_len = 0;
    size_t best_len_code = 0;
    size_t best_dist = 0;
    double best_score = kMinScore;
    int is_match_found = FN(FindLongestMatch)(hasher, ringbuffer,
        ringbuffer_mask, dist_cache, position, max_length, max_distance,
        &best_len, &best_len_code, &best_dist, &best_score);
    if (is_match_found) {
      /* Found a match. Let's look for something even better ahead. */
      int delayed_backward_references_in_row = 0;
      --max_length;
      for (;; --max_length) {
        size_t best_len_2 =
            quality < 5 ? BROTLI_MIN(size_t, best_len - 1, max_length) : 0;
        size_t best_len_code_2 = 0;
        size_t best_dist_2 = 0;
        double best_score_2 = kMinScore;
        const double cost_diff_lazy = 7.0;
        max_distance = BROTLI_MIN(size_t, position + 1, max_backward_limit);
        is_match_found = FN(FindLongestMatch)(hasher, ringbuffer,
            ringbuffer_mask, dist_cache, position + 1, max_length, max_distance,
            &best_len_2, &best_len_code_2, &best_dist_2, &best_score_2);
        if (is_match_found && best_score_2 >= best_score + cost_diff_lazy) {
          /* Ok, let's just write one byte for now and start a match from the
             next byte. */
          ++position;
          ++insert_length;
          best_len = best_len_2;
          best_len_code = best_len_code_2;
          best_dist = best_dist_2;
          best_score = best_score_2;
          if (++delayed_backward_references_in_row < 4 &&
              position + FN(HashTypeLength)() < pos_end) {
            continue;
          }
        }
        break;
      }
      apply_random_heuristics =
          position + 2 * best_len + random_heuristics_window_size;
      max_distance = BROTLI_MIN(size_t, position, max_backward_limit);
      {
        /* The first 16 codes are special shortcodes,
           and the minimum offset is 1. */
        size_t distance_code =
            ComputeDistanceCode(best_dist, max_distance, quality, dist_cache);
        if (best_dist <= max_distance && distance_code > 0) {
          dist_cache[3] = dist_cache[2];
          dist_cache[2] = dist_cache[1];
          dist_cache[1] = dist_cache[0];
          dist_cache[0] = (int)best_dist;
        }
        InitCommand(
            commands++, insert_length, best_len, best_len_code, distance_code);
      }
      *num_literals += insert_length;
      insert_length = 0;
      /* Put the hash keys into the table, if there are enough bytes left.
         Depending on the hasher implementation, it can push all positions
         in the given range or only a subset of them. */
      FN(StoreRange)(hasher, ringbuffer, ringbuffer_mask, position + 2,
                     BROTLI_MIN(size_t, position + best_len, store_end));
      position += best_len;
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
            FN(Store)(hasher, ringbuffer, ringbuffer_mask, position);
            insert_length += 4;
          }
        } else {
          const size_t kMargin =
              BROTLI_MAX(size_t, FN(StoreLookahead)() - 1, 2);
          size_t pos_jump =
              BROTLI_MIN(size_t, position + 8, pos_end - kMargin);
          for (; position < pos_jump; position += 2) {
            FN(Store)(hasher, ringbuffer, ringbuffer_mask, position);
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

#undef Hasher
