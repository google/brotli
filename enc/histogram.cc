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
    const uint8_t* input_buffer,
    size_t pos,
    int context_mode,
    int distance_context_mode,
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
      uint8_t prev_byte = pos > 0 ? input_buffer[pos - 1] : 0;
      uint8_t prev_byte2 = pos > 1 ? input_buffer[pos - 2] : 0;
      uint8_t prev_byte3 = pos > 2 ? input_buffer[pos - 3] : 0;
      int context = (literal_it.type_ * NumContexts(context_mode) +
                     Context(prev_byte, prev_byte2, prev_byte3, context_mode));
      (*literal_histograms)[context].Add(input_buffer[pos]);
      ++pos;
    }
    pos += cmd.copy_length_;
    if (cmd.copy_length_ > 0 && cmd.distance_prefix_ != 0xffff) {
      dist_it.Next();
      int context = dist_it.type_;
      if (distance_context_mode > 0) {
        context <<= 2;
        context += (cmd.copy_length_ > 4) ? 3 : cmd.copy_length_ - 2;
      }
      (*copy_dist_histograms)[context].Add(cmd.distance_prefix_);
    }
  }
}

}  // namespace brotli
