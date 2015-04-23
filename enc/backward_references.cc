// Copyright 2013 Google Inc. All Rights Reserved.
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
// Function to find backward reference copies.

#include "./backward_references.h"

#include <algorithm>
#include <vector>

#include "./command.h"

namespace brotli {

template<typename Hasher, bool kUseCostModel, bool kUseDictionary>
void CreateBackwardReferences(size_t num_bytes,
                              size_t position,
                              const uint8_t* ringbuffer,
                              size_t ringbuffer_mask,
                              const float* literal_cost,
                              size_t literal_cost_mask,
                              const size_t max_backward_limit,
                              const double base_min_score,
                              const int quality,
                              Hasher* hasher,
                              int* dist_cache,
                              int* last_insert_len,
                              Command* commands,
                              int* num_commands) {
  if (num_bytes >= 3 && position >= 3) {
    // Prepare the hashes for three last bytes of the last write.
    // These could not be calculated before, since they require knowledge
    // of both the previous and the current block.
    hasher->Store(&ringbuffer[(position - 3) & ringbuffer_mask],
                  position - 3);
    hasher->Store(&ringbuffer[(position - 2) & ringbuffer_mask],
                  position - 2);
    hasher->Store(&ringbuffer[(position - 1) & ringbuffer_mask],
                  position - 1);
  }
  const Command * const orig_commands = commands;
  int insert_length = *last_insert_len;
  size_t i = position & ringbuffer_mask;
  const int i_diff = position - i;
  const size_t i_end = i + num_bytes;

  // For speed up heuristics for random data.
  const int random_heuristics_window_size = quality < 9 ? 64 : 512;
  int apply_random_heuristics = i + random_heuristics_window_size;

  double average_cost = 5.4;
  if (kUseCostModel) {
    average_cost = 0.0;
    for (int k = position; k < position + num_bytes; ++k) {
      average_cost += literal_cost[k & literal_cost_mask];
    }
    if (num_bytes > 0) {
      average_cost /= num_bytes;
    }
  }

  // M1 match is for considering for two repeated copies, if moving
  // one literal form the previous copy to the current one allows the
  // current copy to be more efficient (because the way static dictionary
  // codes words). M1 matching improves text compression density by ~0.15 %.
  bool match_found_M1 = false;
  int best_len_M1 = 0;
  int best_len_code_M1 = 0;
  int best_dist_M1 = 0;
  double best_score_M1 = 0;
  while (i + 3 < i_end) {
    int max_length = i_end - i;
    size_t max_distance = std::min(i + i_diff, max_backward_limit);
    double min_score = base_min_score;
    if (kUseCostModel && insert_length < 8) {
      double cost_diff[8] =
          { 0.1, 0.038, 0.019, 0.013, 0.001, 0.001, 0.001, 0.001 };
      min_score += cost_diff[insert_length];
    }
    int best_len = 0;
    int best_len_code = 0;
    int best_dist = 0;
    double best_score = min_score;
    bool match_found = hasher->FindLongestMatch(
        ringbuffer, ringbuffer_mask,
        literal_cost, literal_cost_mask, average_cost,
        dist_cache, i + i_diff, max_length, max_distance,
        &best_len, &best_len_code, &best_dist, &best_score);
    if (match_found) {
      if (kUseDictionary && match_found_M1 && best_score_M1 > best_score) {
        // Two copies after each other. Take the last literal from the
        // last copy, and use it as the first of this one.
        Command prev_cmd = commands[-1];
        commands[-1] = Command(prev_cmd.insert_len_,
                               prev_cmd.copy_len_ - 1,
                               prev_cmd.copy_len_ - 1,
                               prev_cmd.DistanceCode());
        hasher->Store(ringbuffer + i, i + i_diff);
        --i;
        best_len = best_len_M1;
        best_len_code = best_len_code_M1;
        best_dist = best_dist_M1;
        best_score = best_score_M1;
      } else {
        // Found a match. Let's look for something even better ahead.
        int delayed_backward_references_in_row = 0;
        for (;;) {
          --max_length;
          int best_len_2 = quality < 4 ? std::min(best_len - 1, max_length) : 0;
          int best_len_code_2 = 0;
          int best_dist_2 = 0;
          double best_score_2 = min_score;
          max_distance = std::min(i + i_diff + 1, max_backward_limit);
          hasher->Store(ringbuffer + i, i + i_diff);
          match_found = hasher->FindLongestMatch(
              ringbuffer, ringbuffer_mask,
              literal_cost, literal_cost_mask, average_cost,
              dist_cache, i + i_diff + 1, max_length, max_distance,
              &best_len_2, &best_len_code_2, &best_dist_2, &best_score_2);
          double cost_diff_lazy = 7.0;
          if (kUseCostModel) {
            cost_diff_lazy = 0.0;
            if (best_len >= 4) {
              cost_diff_lazy +=
                  literal_cost[(i + 4) & literal_cost_mask] - average_cost;
            }
            {
              const int tail_length = best_len_2 - best_len + 1;
              for (int k = 0; k < tail_length; ++k) {
                cost_diff_lazy -=
                    literal_cost[(i + best_len + k) & literal_cost_mask] -
                    average_cost;
              }
            }
            // If we are not inserting any symbols, inserting one is more
            // expensive than if we were inserting symbols anyways.
            if (insert_length < 1) {
              cost_diff_lazy += 0.97;
            }
            // Add bias to slightly avoid lazy matching.
            cost_diff_lazy += 2.0 + delayed_backward_references_in_row * 0.2;
            cost_diff_lazy += 0.04 * literal_cost[i & literal_cost_mask];
          }
          if (match_found && best_score_2 >= best_score + cost_diff_lazy) {
            // Ok, let's just write one byte for now and start a match from the
            // next byte.
            ++i;
            ++insert_length;
            best_len = best_len_2;
            best_len_code = best_len_code_2;
            best_dist = best_dist_2;
            best_score = best_score_2;
            if (++delayed_backward_references_in_row < 4) {
              continue;
            }
          }
          break;
        }
      }
      apply_random_heuristics =
          i + 2 * best_len + random_heuristics_window_size;
      max_distance = std::min(i + i_diff, max_backward_limit);
      int distance_code = best_dist + 16;
      if (best_dist <= max_distance) {
        if (best_dist == dist_cache[0]) {
          distance_code = 1;
        } else if (best_dist == dist_cache[1]) {
          distance_code = 2;
        } else if (best_dist == dist_cache[2]) {
          distance_code = 3;
        } else if (best_dist == dist_cache[3]) {
          distance_code = 4;
        } else if (quality > 1 && best_dist >= 6) {
          for (int k = 4; k < kNumDistanceShortCodes; ++k) {
            int idx = kDistanceCacheIndex[k];
            int candidate = dist_cache[idx] + kDistanceCacheOffset[k];
            static const int kLimits[16] = { 0, 0, 0, 0,
                                             6, 6, 11, 11,
                                             11, 11, 11, 11,
                                             12, 12, 12, 12 };
            if (best_dist == candidate && best_dist >= kLimits[k]) {
              distance_code = k + 1;
              break;
            }
          }
        }
        if (distance_code > 1) {
          dist_cache[3] = dist_cache[2];
          dist_cache[2] = dist_cache[1];
          dist_cache[1] = dist_cache[0];
          dist_cache[0] = best_dist;
        }
      }
      Command cmd(insert_length, best_len, best_len_code, distance_code);
      *commands++ = cmd;
      insert_length = 0;
      if (kUseDictionary) {
        ++i;
        // Copy all copied literals to the hasher, except the last one.
        // We cannot store the last one yet, otherwise we couldn't find
        // the possible M1 match.
        for (int j = 1; j < best_len - 1; ++j) {
          if (i + 3 < i_end) {
            hasher->Store(ringbuffer + i, i + i_diff);
          }
          ++i;
        }
        // Prepare M1 match.
        if (hasher->HasStaticDictionary() &&
            best_len >= 4 && i + 20 < i_end && best_dist <= max_distance) {
          max_distance = std::min(i + i_diff, max_backward_limit);
          best_score_M1 = min_score;
          match_found_M1 = hasher->FindLongestMatch(
              ringbuffer, ringbuffer_mask,
              literal_cost, literal_cost_mask, average_cost,
              dist_cache, i + i_diff, i_end - i, max_distance,
              &best_len_M1, &best_len_code_M1, &best_dist_M1, &best_score_M1);
        } else {
          match_found_M1 = false;
        }
        if (kUseCostModel) {
          // This byte is just moved from the previous copy to the current,
          // that is no gain.
          best_score_M1 -= literal_cost[i & literal_cost_mask];
          // Adjust for losing the opportunity for lazy matching.
          best_score_M1 -= 3.75;
        }
        // Store the last one of the match.
        if (i + 3 < i_end) {
          hasher->Store(ringbuffer + i, i + i_diff);
        }
        ++i;
      } else {
        // Put the hash keys into the table, if there are enough
        // bytes left.
        for (int j = 1; j < best_len; ++j) {
          hasher->Store(&ringbuffer[i + j], i + i_diff + j);
        }
        i += best_len;
      }
    } else {
      match_found_M1 = false;
      ++insert_length;
      hasher->Store(ringbuffer + i, i + i_diff);
      ++i;
      // If we have not seen matches for a long time, we can skip some
      // match lookups. Unsuccessful match lookups are very very expensive
      // and this kind of a heuristic speeds up compression quite
      // a lot.
      if (i > apply_random_heuristics) {
        // Going through uncompressible data, jump.
        if (i > apply_random_heuristics + 4 * random_heuristics_window_size) {
          // It is quite a long time since we saw a copy, so we assume
          // that this data is not compressible, and store hashes less
          // often. Hashes of non compressible data are less likely to
          // turn out to be useful in the future, too, so we store less of
          // them to not to flood out the hash table of good compressible
          // data.
          int i_jump = std::min(i + 16, i_end - 4);
          for (; i < i_jump; i += 4) {
            hasher->Store(ringbuffer + i, i + i_diff);
            insert_length += 4;
          }
        } else {
          int i_jump = std::min(i + 8, i_end - 3);
          for (; i < i_jump; i += 2) {
            hasher->Store(ringbuffer + i, i + i_diff);
            insert_length += 2;
          }
        }
      }
    }
  }
  insert_length += (i_end - i);
  *last_insert_len = insert_length;
  *num_commands += (commands - orig_commands);
}

void CreateBackwardReferences(size_t num_bytes,
                              size_t position,
                              const uint8_t* ringbuffer,
                              size_t ringbuffer_mask,
                              const float* literal_cost,
                              size_t literal_cost_mask,
                              const size_t max_backward_limit,
                              const double base_min_score,
                              const int quality,
                              Hashers* hashers,
                              int hash_type,
                              int* dist_cache,
                              int* last_insert_len,
                              Command* commands,
                              int* num_commands) {
  switch (hash_type) {
    case 1:
      CreateBackwardReferences<Hashers::H1, false, false>(
          num_bytes, position, ringbuffer, ringbuffer_mask,
          literal_cost, literal_cost_mask, max_backward_limit, base_min_score,
          quality, hashers->hash_h1.get(), dist_cache, last_insert_len,
          commands, num_commands);
      break;
    case 2:
      CreateBackwardReferences<Hashers::H2, false, false>(
          num_bytes, position, ringbuffer, ringbuffer_mask,
          literal_cost, literal_cost_mask, max_backward_limit, base_min_score,
          quality, hashers->hash_h2.get(), dist_cache, last_insert_len,
          commands, num_commands);
      break;
    case 3:
      CreateBackwardReferences<Hashers::H3, false, false>(
          num_bytes, position, ringbuffer, ringbuffer_mask,
          literal_cost, literal_cost_mask, max_backward_limit, base_min_score,
          quality, hashers->hash_h3.get(), dist_cache, last_insert_len,
          commands, num_commands);
      break;
    case 4:
      CreateBackwardReferences<Hashers::H4, false, false>(
          num_bytes, position, ringbuffer, ringbuffer_mask,
          literal_cost, literal_cost_mask, max_backward_limit, base_min_score,
          quality, hashers->hash_h4.get(), dist_cache, last_insert_len,
          commands, num_commands);
      break;
    case 5:
      CreateBackwardReferences<Hashers::H5, false, false>(
          num_bytes, position, ringbuffer, ringbuffer_mask,
          literal_cost, literal_cost_mask, max_backward_limit, base_min_score,
          quality, hashers->hash_h5.get(), dist_cache, last_insert_len,
          commands, num_commands);
      break;
    case 6:
      CreateBackwardReferences<Hashers::H6, false, false>(
          num_bytes, position, ringbuffer, ringbuffer_mask,
          literal_cost, literal_cost_mask, max_backward_limit, base_min_score,
          quality, hashers->hash_h6.get(), dist_cache, last_insert_len,
          commands, num_commands);
      break;
    case 7:
      CreateBackwardReferences<Hashers::H7, false, false>(
          num_bytes, position, ringbuffer, ringbuffer_mask,
          literal_cost, literal_cost_mask, max_backward_limit, base_min_score,
          quality, hashers->hash_h7.get(), dist_cache, last_insert_len,
          commands, num_commands);
      break;
    case 8:
      CreateBackwardReferences<Hashers::H8, true, true>(
          num_bytes, position, ringbuffer, ringbuffer_mask,
          literal_cost, literal_cost_mask, max_backward_limit, base_min_score,
          quality, hashers->hash_h8.get(), dist_cache, last_insert_len,
          commands, num_commands);
      break;
    case 9:
      CreateBackwardReferences<Hashers::H9, true, false>(
          num_bytes, position, ringbuffer, ringbuffer_mask,
          literal_cost, literal_cost_mask, max_backward_limit, base_min_score,
          quality, hashers->hash_h9.get(), dist_cache, last_insert_len,
          commands, num_commands);
      break;
    default:
      break;
  }
}

}  // namespace brotli
