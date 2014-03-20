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

template<typename Hasher>
void CreateBackwardReferences(size_t num_bytes,
                              size_t position,
                              const uint8_t* ringbuffer,
                              const float* literal_cost,
                              size_t ringbuffer_mask,
                              const size_t max_backward_limit,
                              Hasher* hasher,
                              std::vector<Command>* commands) {
  // Length heuristic that seems to help probably by better selection
  // of lazy matches of similar lengths.
  int insert_length = 0;
  size_t i = position & ringbuffer_mask;
  const int i_diff = position - i;
  const size_t i_end = i + num_bytes;

  const int random_heuristics_window_size = 512;
  int apply_random_heuristics = i + random_heuristics_window_size;

  double average_cost = 0.0;
  for (int k = position; k < position + num_bytes; ++k) {
    average_cost += literal_cost[k & ringbuffer_mask];
  }
  average_cost /= num_bytes;
  hasher->set_average_cost(average_cost);

  // M1 match is for considering for two repeated copies, if moving
  // one literal form the previous copy to the current one allows the
  // current copy to be more efficient (because the way static dictionary
  // codes words). M1 matching improves text compression density by ~0.15 %.
  bool match_found_M1 = false;
  size_t best_len_M1 = 0;
  size_t best_len_code_M1 = 0;
  size_t best_dist_M1 = 0;
  double best_score_M1 = 0;
  while (i + 2 < i_end) {
    size_t best_len = 0;
    size_t best_len_code = 0;
    size_t best_dist = 0;
    double best_score = 0;
    size_t max_distance = std::min(i + i_diff, max_backward_limit);
    bool in_dictionary;
    hasher->set_insert_length(insert_length);
    bool match_found = hasher->FindLongestMatch(
        ringbuffer, literal_cost, ringbuffer_mask,
        i + i_diff, i_end - i, max_distance,
        &best_len, &best_len_code, &best_dist, &best_score,
        &in_dictionary);
    bool best_in_dictionary = in_dictionary;
    if (match_found) {
      if (match_found_M1 && best_score_M1 > best_score) {
        // Two copies after each other. Take the last literal from the
        // last copy, and use it as the first of this one.
        (commands->rbegin())->copy_length_ -= 1;
        (commands->rbegin())->copy_length_code_ -= 1;
        hasher->Store(ringbuffer + i, i + i_diff);
        --i;
        best_len = best_len_M1;
        best_len_code = best_len_code_M1;
        best_dist = best_dist_M1;
        best_score = best_score_M1;
        // in_dictionary doesn't need to be correct, but it is the only
        // reason why M1 matching should be beneficial here. Setting it here
        // will only disable further M1 matching against this copy.
        best_in_dictionary = true;
        in_dictionary = true;
      } else {
        // Found a match. Let's look for something even better ahead.
        int delayed_backward_references_in_row = 0;
        while (i + 4 < i_end &&
               delayed_backward_references_in_row < 4) {
          size_t best_len_2 = 0;
          size_t best_len_code_2 = 0;
          size_t best_dist_2 = 0;
          double best_score_2 = 0;
          max_distance = std::min(i + i_diff + 1, max_backward_limit);
          hasher->Store(ringbuffer + i, i + i_diff);
          match_found = hasher->FindLongestMatch(
              ringbuffer, literal_cost, ringbuffer_mask,
              i + i_diff + 1, i_end - i - 1, max_distance,
              &best_len_2, &best_len_code_2, &best_dist_2, &best_score_2,
              &in_dictionary);
          double cost_diff_lazy = 0;
          if (best_len >= 4) {
            cost_diff_lazy +=
                literal_cost[(i + 4) & ringbuffer_mask] - average_cost;
          }
          {
            const int tail_length = best_len_2 - best_len + 1;
            for (int k = 0; k < tail_length; ++k) {
              cost_diff_lazy -=
                  literal_cost[(i + best_len + k) & ringbuffer_mask] -
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
          cost_diff_lazy += 0.04 * literal_cost[i & ringbuffer_mask];

          if (match_found && best_score_2 >= best_score + cost_diff_lazy) {
            // Ok, let's just write one byte for now and start a match from the
            // next byte.
            ++insert_length;
            ++delayed_backward_references_in_row;
            best_len = best_len_2;
            best_len_code = best_len_code_2;
            best_dist = best_dist_2;
            best_score = best_score_2;
            best_in_dictionary = in_dictionary;
            i++;
          } else {
            break;
          }
        }
      }
      apply_random_heuristics =
          i + 2 * best_len + random_heuristics_window_size;
      Command cmd;
      cmd.insert_length_ = insert_length;
      cmd.copy_length_ = best_len;
      cmd.copy_length_code_ = best_len_code;
      cmd.copy_distance_ = best_dist;
      commands->push_back(cmd);
      insert_length = 0;
      ++i;
      if (best_dist <= std::min(i + i_diff, max_backward_limit)) {
        hasher->set_last_distance(best_dist);
      }

      // Copy all copied literals to the hasher, except the last one.
      // We cannot store the last one yet, otherwise we couldn't find
      // the possible M1 match.
      for (int j = 1; j < best_len - 1; ++j) {
        if (i + 2 < i_end) {
          hasher->Store(ringbuffer + i, i + i_diff);
        }
        ++i;
      }
      // Prepare M1 match.
      if (hasher->HasStaticDictionary() &&
          best_len >= 4 && i + 20 < i_end && !best_in_dictionary) {
        max_distance = std::min(i + i_diff, max_backward_limit);
        match_found_M1 = hasher->FindLongestMatch(
            ringbuffer, literal_cost, ringbuffer_mask,
            i + i_diff, i_end - i, max_distance,
            &best_len_M1, &best_len_code_M1, &best_dist_M1, &best_score_M1,
            &in_dictionary);
      } else {
        match_found_M1 = false;
        in_dictionary = false;
      }
      // This byte is just moved from the previous copy to the current,
      // that is no gain.
      best_score_M1 -= literal_cost[i & ringbuffer_mask];
      // Adjust for losing the opportunity for lazy matching.
      best_score_M1 -= 3.75;

      // Store the last one of the match.
      if (i + 2 < i_end) {
        hasher->Store(ringbuffer + i, i + i_diff);
      }
      ++i;
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
          int i_jump = std::min(i + 8, i_end - 2);
          for (; i < i_jump; i += 2) {
            hasher->Store(ringbuffer + i, i + i_diff);
            insert_length += 2;
          }
        }
      }
    }
  }
  insert_length += (i_end - i);

  if (insert_length > 0) {
    Command cmd;
    cmd.insert_length_ = insert_length;
    cmd.copy_length_ = 0;
    cmd.copy_distance_ = 0;
    commands->push_back(cmd);
  }
}

void CreateBackwardReferences(size_t num_bytes,
                              size_t position,
                              const uint8_t* ringbuffer,
                              const float* literal_cost,
                              size_t ringbuffer_mask,
                              const size_t max_backward_limit,
                              Hashers* hashers,
                              Hashers::Type hash_type,
                              std::vector<Command>* commands) {
  switch (hash_type) {
    case Hashers::HASH_15_8_4:
      CreateBackwardReferences(
          num_bytes, position, ringbuffer, literal_cost,
          ringbuffer_mask, max_backward_limit,
          hashers->hash_15_8_4.get(),
          commands);
      break;
    case Hashers::HASH_15_8_2:
      CreateBackwardReferences(
          num_bytes, position, ringbuffer, literal_cost,
          ringbuffer_mask, max_backward_limit,
          hashers->hash_15_8_2.get(),
          commands);
      break;
    default:
      break;
  }
}


}  // namespace brotli
