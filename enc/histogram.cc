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
    const std::vector<Command>& cmds,
    const BlockSplit& literal_split,
    const BlockSplit& insert_and_copy_split,
    const BlockSplit& dist_split,
    const uint8_t* ringbuffer,
    size_t pos,
    size_t mask,
    const std::vector<int>& context_modes,
    std::vector<HistogramLiteral>* literal_histograms,
    std::vector<HistogramCommand>* insert_and_copy_histograms,
    std::vector<HistogramDistance>* copy_dist_histograms) {
  BlockSplitIterator literal_it(literal_split);
  BlockSplitIterator insert_and_copy_it(insert_and_copy_split);
  BlockSplitIterator dist_it(dist_split);
  for (int i = 0; i < cmds.size(); ++i) {
    const Command &cmd = cmds[i];
    insert_and_copy_it.Next();
    (*insert_and_copy_histograms)[insert_and_copy_it.type_].Add(
        cmd.command_prefix_);
    for (int j = 0; j < cmd.insert_length_; ++j) {
      literal_it.Next();
      uint8_t prev_byte = pos > 0 ? ringbuffer[(pos - 1) & mask] : 0;
      uint8_t prev_byte2 = pos > 1 ? ringbuffer[(pos - 2) & mask] : 0;
      int context = (literal_it.type_ << kLiteralContextBits) +
          Context(prev_byte, prev_byte2, context_modes[literal_it.type_]);
      (*literal_histograms)[context].Add(ringbuffer[pos & mask]);
      ++pos;
    }
    pos += cmd.copy_length_;
    if (cmd.copy_length_ > 0 && cmd.distance_prefix_ != 0xffff) {
      dist_it.Next();
      int context = (dist_it.type_ << kDistanceContextBits) +
          ((cmd.copy_length_code_ > 4) ? 3 : cmd.copy_length_code_ - 2);
      (*copy_dist_histograms)[context].Add(cmd.distance_prefix_);
    }
  }
}

void BuildLiteralHistogramsForBlockType(
    const std::vector<Command>& cmds,
    const BlockSplit& literal_split,
    const uint8_t* ringbuffer,
    size_t pos,
    size_t mask,
    int block_type,
    int context_mode,
    std::vector<HistogramLiteral>* histograms) {
  BlockSplitIterator literal_it(literal_split);
  for (int i = 0; i < cmds.size(); ++i) {
    const Command &cmd = cmds[i];
    for (int j = 0; j < cmd.insert_length_; ++j) {
      literal_it.Next();
      if (literal_it.type_ == block_type) {
        uint8_t prev_byte = pos > 0 ? ringbuffer[(pos - 1) & mask] : 0;
        uint8_t prev_byte2 = pos > 1 ? ringbuffer[(pos - 2) & mask] : 0;
        int context = Context(prev_byte, prev_byte2, context_mode);
        (*histograms)[context].Add(ringbuffer[pos & mask]);
      }
      ++pos;
    }
    pos += cmd.copy_length_;
  }
}

}  // namespace brotli
