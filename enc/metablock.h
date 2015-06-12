// Copyright 2015 Google Inc. All Rights Reserved.
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
// Algorithms for distributing the literals and commands of a metablock between
// block types and contexts.

#ifndef BROTLI_ENC_METABLOCK_H_
#define BROTLI_ENC_METABLOCK_H_

#include <vector>

#include "./command.h"
#include "./histogram.h"

namespace brotli {

struct BlockSplit {
  BlockSplit() : num_types(0) {}

  int num_types;
  std::vector<int> types;
  std::vector<int> lengths;
};

struct MetaBlockSplit {
  BlockSplit literal_split;
  BlockSplit command_split;
  BlockSplit distance_split;
  std::vector<int> literal_context_map;
  std::vector<int> distance_context_map;
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
                    int literal_context_mode,
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
                                      int literal_context_mode,
                                      int num_contexts,
                                      const int* static_context_map,
                                      const Command *commands,
                                      size_t n_commands,
                                      MetaBlockSplit* mb);

void OptimizeHistograms(int num_direct_distance_codes,
                        int distance_postfix_bits,
                        MetaBlockSplit* mb);

}  // namespace brotli

#endif  // BROTLI_ENC_METABLOCK_H_
