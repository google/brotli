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
// Build per-context histograms of literals, commands and distance codes.

#include "./histogram.h"

#include <stdint.h>
#include <cmath>

#include "./block_splitter.h"
#include "./command.h"
#include "./context.h"
#include "./prefix.h"

namespace brotli {

void BuildHistograms(
    const Command* cmds,
    const size_t num_commands,
    const BlockSplit& literal_split,
    const BlockSplit& insert_and_copy_split,
    const BlockSplit& dist_split,
    const uint8_t* ringbuffer,
    size_t start_pos,
    size_t mask,
    uint8_t prev_byte,
    uint8_t prev_byte2,
    const std::vector<int>& context_modes,
    std::vector<HistogramLiteral>* literal_histograms,
    std::vector<HistogramCommand>* insert_and_copy_histograms,
    std::vector<HistogramDistance>* copy_dist_histograms) {
  size_t pos = start_pos;
  BlockSplitIterator literal_it(literal_split);
  BlockSplitIterator insert_and_copy_it(insert_and_copy_split);
  BlockSplitIterator dist_it(dist_split);
  for (int i = 0; i < num_commands; ++i) {
    const Command &cmd = cmds[i];
    insert_and_copy_it.Next();
    (*insert_and_copy_histograms)[insert_and_copy_it.type_].Add(
        cmd.cmd_prefix_);
    for (int j = 0; j < cmd.insert_len_; ++j) {
      literal_it.Next();
      int context = (literal_it.type_ << kLiteralContextBits) +
          Context(prev_byte, prev_byte2, context_modes[literal_it.type_]);
      (*literal_histograms)[context].Add(ringbuffer[pos & mask]);
      prev_byte2 = prev_byte;
      prev_byte = ringbuffer[pos & mask];
      ++pos;
    }
    pos += cmd.copy_len_;
    if (cmd.copy_len_ > 0) {
      prev_byte2 = ringbuffer[(pos - 2) & mask];
      prev_byte = ringbuffer[(pos - 1) & mask];
      if (cmd.cmd_prefix_ >= 128) {
        dist_it.Next();
        int context = (dist_it.type_ << kDistanceContextBits) +
            cmd.DistanceContext();
        (*copy_dist_histograms)[context].Add(cmd.dist_prefix_);
      }
    }
  }
}

}  // namespace brotli
