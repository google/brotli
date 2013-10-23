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
#include "./hash.h"
#include "./literal_cost.h"

namespace brotli {

void CreateBackwardReferences(const uint8_t* data,
                              int length,
                              std::vector<Command>* commands) {
  HashLongestMatch<13,11> *hasher = new HashLongestMatch<13,11>;
  float *literal_cost = new float[length];
  EstimateBitCostsForLiterals(length, data, literal_cost);
  hasher->SetLiteralCost(literal_cost);

  // Length heuristic that seems to help probably by better selection
  // of lazy matches of similar lengths.
  int insert_length = 0;
  size_t i = 0;

  double average_cost = 0.0;
  for (int i = 0; i < length; ++i) {
    average_cost += literal_cost[i];
  }
  average_cost /= length;
  hasher->set_average_cost(average_cost);

  while (i + 2 < length) {
    size_t best_len = 0;
    size_t best_dist = 0;
    double best_score = 0;
    const size_t max_distance = std::min(i, 1UL << 24);
    hasher->set_insert_length(insert_length);
    bool match_found = hasher->FindLongestMatch(
        data, i, length - i, max_distance,
        &best_len, &best_dist, &best_score);
    if (match_found) {
      // Found a match. Let's look for something even better ahead.
      int delayed_backward_references_in_row = 0;
      while (i + 4 < length &&
             delayed_backward_references_in_row < 4) {
        size_t best_len_2 = 0;
        size_t best_dist_2 = 0;
        double best_score_2 = 0;
        hasher->Store(data + i, i);
        match_found = hasher->FindLongestMatch(
            data, i + 1, length - i - 1, max_distance,
            &best_len_2, &best_dist_2, &best_score_2);
        double cost_diff_lazy = 0;
        if (best_len >= 4) {
          cost_diff_lazy += hasher->literal_cost(i + 4) - average_cost;
        }
        {
          const int tail_length = best_len_2 - best_len + 1;
          for (int k = 0; k < tail_length; ++k) {
            cost_diff_lazy -= hasher->literal_cost(i + best_len + k) -
                average_cost;
          }
        }
        // If we are not inserting any symbols, inserting one is more
        // expensive than if we were inserting symbols anyways.
        if (insert_length < 1) {
          cost_diff_lazy += 1.0;
        }
        // Add bias to slightly avoid lazy matching.
        cost_diff_lazy += 2.0 + delayed_backward_references_in_row * 0.2;
        cost_diff_lazy += 0.04 * hasher->literal_cost(i);

        if (match_found && best_score_2 >= best_score + cost_diff_lazy) {
          // Ok, let's just write one byte for now and start a match from the
          // next byte.
          ++insert_length;
          ++delayed_backward_references_in_row;
          best_len = best_len_2;
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
      cmd.copy_distance_ = best_dist;
      commands->push_back(cmd);
      hasher->set_last_distance(best_dist);

      insert_length = 0;
      ++i;
      for (int j = 1; j < best_len; ++j) {
        if (i + 2 < length) {
          hasher->Store(data + i, i);
        }
        ++i;
      }
    } else {
      ++insert_length;
      hasher->Store(data + i, i);
      ++i;
    }
  }
  insert_length += (length - i);

  if (insert_length > 0) {
    Command cmd;
    cmd.insert_length_ = insert_length;
    cmd.copy_length_ = 0;
    cmd.copy_distance_ = 0;
    commands->push_back(cmd);
  }

  delete[] literal_cost;
  delete hasher;
}

}  // namespace brotli
