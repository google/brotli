/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

// Build per-context histograms of literals, commands and distance codes.

#include "./histogram.h"

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
    const std::vector<ContextType>& context_modes,
    std::vector<HistogramLiteral>* literal_histograms,
    std::vector<HistogramCommand>* insert_and_copy_histograms,
    std::vector<HistogramDistance>* copy_dist_histograms) {
  size_t pos = start_pos;
  BlockSplitIterator literal_it(literal_split);
  BlockSplitIterator insert_and_copy_it(insert_and_copy_split);
  BlockSplitIterator dist_it(dist_split);
  for (size_t i = 0; i < num_commands; ++i) {
    const Command &cmd = cmds[i];
    insert_and_copy_it.Next();
    (*insert_and_copy_histograms)[insert_and_copy_it.type_].Add(
        cmd.cmd_prefix_);
    for (size_t j = cmd.insert_len_; j != 0; --j) {
      literal_it.Next();
      size_t context = (literal_it.type_ << kLiteralContextBits) +
          Context(prev_byte, prev_byte2, context_modes[literal_it.type_]);
      (*literal_histograms)[context].Add(ringbuffer[pos & mask]);
      prev_byte2 = prev_byte;
      prev_byte = ringbuffer[pos & mask];
      ++pos;
    }
    pos += cmd.copy_len();
    if (cmd.copy_len()) {
      prev_byte2 = ringbuffer[(pos - 2) & mask];
      prev_byte = ringbuffer[(pos - 1) & mask];
      if (cmd.cmd_prefix_ >= 128) {
        dist_it.Next();
        size_t context = (dist_it.type_ << kDistanceContextBits) +
            cmd.DistanceContext();
        (*copy_dist_histograms)[context].Add(cmd.dist_prefix_);
      }
    }
  }
}

}  // namespace brotli
