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

  double average_cost = 0.0;
  for (int k = position; k < position + num_bytes; ++k) {
    average_cost += literal_cost[k & ringbuffer_mask];
  }
  average_cost /= num_bytes;
  hasher->set_average_cost(average_cost);

  while (i + 2 < i_end) {
    size_t best_len = 0;
    size_t best_len_code = 0;
    size_t best_dist = 0;
    double best_score = 0;
    size_t max_distance = std::min(i + i_diff, max_backward_limit);
    hasher->set_insert_length(insert_length);
    bool match_found = hasher->FindLongestMatch(
        ringbuffer, literal_cost, ringbuffer_mask,
        i + i_diff, i_end - i, max_distance,
        &best_len, &best_len_code, &best_dist, &best_score);
    if (match_found) {
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
            &best_len_2, &best_len_code_2, &best_dist_2, &best_score_2);
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
          i++;
        } else {
          break;
        }
      }
      Command cmd;
      cmd.insert_length_ = insert_length;
      cmd.copy_length_ = best_len;
      cmd.copy_length_code_ = best_len_code;
      cmd.copy_distance_ = best_dist;
      commands->push_back(cmd);
      hasher->set_last_distance(best_dist);

      insert_length = 0;
      ++i;
      for (int j = 1; j < best_len; ++j) {
        if (i + 2 < i_end) {
          hasher->Store(ringbuffer + i, i + i_diff);
        }
        ++i;
      }
    } else {
      ++insert_length;
      hasher->Store(ringbuffer + i, i + i_diff);
      ++i;
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

}  // namespace brotli
