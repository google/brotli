/* Copyright 2015 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

// Algorithms for distributing the literals and commands of a metablock between
// block types and contexts.

#ifndef BROTLI_ENC_METABLOCK_H_
#define BROTLI_ENC_METABLOCK_H_

#include <vector>

#include "./command.h"
#include "./histogram.h"

namespace brotli {

struct BlockSplit {
  BlockSplit(void) : num_types(0) {}

  size_t num_types;
  std::vector<uint8_t> types;
  std::vector<uint32_t> lengths;
};

struct MetaBlockSplit {
  BlockSplit literal_split;
  BlockSplit command_split;
  BlockSplit distance_split;
  std::vector<uint32_t> literal_context_map;
  std::vector<uint32_t> distance_context_map;
  std::vector<HistogramLiteral> literal_histograms;
  std::vector<HistogramCommand> command_histograms;
  std::vector<HistogramDistance> distance_histograms;
};

// Uses the slow shortest-path block splitter and does context clustering.
void BuildMetaBlock(const uint8_t* ringbuffer,
                    const size_t pos,
                    const size_t mask,
                    uint8_t prev_byte,
                    uint8_t prev_byte2,
                    const Command* cmds,
                    size_t num_commands,
                    ContextType literal_context_mode,
                    MetaBlockSplit* mb);

// Uses a fast greedy block splitter that tries to merge current block with the
// last or the second last block and does not do any context modeling.
void BuildMetaBlockGreedy(const uint8_t* ringbuffer,
                          size_t pos,
                          size_t mask,
                          const Command *commands,
                          size_t n_commands,
                          MetaBlockSplit* mb);

// Uses a fast greedy block splitter that tries to merge current block with the
// last or the second last block and uses a static context clustering which
// is the same for all block types.
void BuildMetaBlockGreedyWithContexts(const uint8_t* ringbuffer,
                                      size_t pos,
                                      size_t mask,
                                      uint8_t prev_byte,
                                      uint8_t prev_byte2,
                                      ContextType literal_context_mode,
                                      size_t num_contexts,
                                      const uint32_t* static_context_map,
                                      const Command *commands,
                                      size_t n_commands,
                                      MetaBlockSplit* mb);

void OptimizeHistograms(size_t num_direct_distance_codes,
                        size_t distance_postfix_bits,
                        MetaBlockSplit* mb);

}  // namespace brotli

#endif  // BROTLI_ENC_METABLOCK_H_
