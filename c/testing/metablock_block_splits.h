/* Copyright 2020 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

#include "../compress_similar_files/compress_similar_files.c"
#include "helper.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>


bool TestBlocksHistograms() {
  BlockSplitFromDecoder lit_block_splits;
  lit_block_splits.num_blocks = 4;
  lit_block_splits.num_types = 2;
  uint8_t types[4] = {0, 1, 0, 1};
  uint32_t positions_begin[4] = {0, 73, 158, 230};
  uint32_t positions_end[4] = {73, 158, 230, 256};

  lit_block_splits.types = (uint8_t*) malloc(sizeof(uint8_t) * 10);
  lit_block_splits.positions_begin = (uint32_t*) malloc(sizeof(uint8_t) * 10);
  lit_block_splits.positions_end = (uint32_t*) malloc(sizeof(uint8_t) * 10);
  lit_block_splits.types = types;
  lit_block_splits.positions_begin = positions_begin;
  lit_block_splits.positions_end = positions_end;

  BlockSplitFromDecoder cmd_block_splits;
  cmd_block_splits.num_blocks = 3;
  cmd_block_splits.num_types = 3;
  uint8_t types_cmd[3] = {0, 1, 2};
  uint32_t positions_begin_cmd[3] = {0, 151, 180};
  uint32_t positions_end_cmd[3] = {151, 180, 256};

  cmd_block_splits.types = (uint8_t*) malloc(sizeof(uint8_t) * 10);
  cmd_block_splits.positions_begin = (uint32_t*) malloc(sizeof(uint8_t) * 10);
  cmd_block_splits.positions_end = (uint32_t*) malloc(sizeof(uint8_t) * 10);
  cmd_block_splits.types = types_cmd;
  cmd_block_splits.positions_begin = positions_begin_cmd;
  cmd_block_splits.positions_end = positions_end_cmd;

  MemoryManager m;
  BrotliInitMemoryManager(&m, 0, 0, 0);
  Command* cmds = (Command* )malloc(sizeof(Command) * 10);
  BrotliDistanceParams dist_params = {0, 0, 64, 64, 67108860};
  InitCommand(&cmds[0], /*dist=*/ &dist_params, /*insertlen=*/10, /*copylen*/7,
  /*copylen_code_delta=*/0, /*distance_code=*/613); // -> position 17
  InitCommand(&cmds[1], /*dist=*/ &dist_params, /*insertlen=*/30, /*copylen*/54,
  /*copylen_code_delta=*/0, /*distance_code=*/103); // -> position 101
  InitCommand(&cmds[2], /*dist=*/ &dist_params, /*insertlen=*/4, /*copylen*/53,
  /*copylen_code_delta=*/0, /*distance_code=*/30); // -> position 158
  InitCommand(&cmds[3], /*dist=*/ &dist_params, /*insertlen=*/10, /*copylen*/14,
  /*copylen_code_delta=*/0, /*distance_code=*/101); // -> position 182
  InitCommand(&cmds[4], /*dist=*/ &dist_params, /*insertlen=*/21, /*copylen*/38,
  /*copylen_code_delta=*/0, /*distance_code=*/1023); // -> position 241
  InitCommand(&cmds[5], /*dist=*/ &dist_params, /*insertlen=*/2, /*copylen*/13,
  /*copylen_code_delta=*/0, /*distance_code=*/2010); // -> position 256
  int num_commands = 6;
  ContextLut literal_context_lut;
  MetaBlockSplit mb;
  InitMetaBlockSplit(&mb);
  uint8_t ringbuffer[256] = {21, 27, 14, 20, 12, 29, 27, 28, 3, 10, 29, 8, 9, 18,
    19, 26, 13, 5, 19, 26, 0, 14, 14, 2, 2, 28, 24, 25, 25, 5, 19, 15, 22, 14,
    17, 26, 16, 19, 20, 8, 24, 0, 14, 20, 19, 12, 20, 21, 11, 27, 28,
       19,  4, 11, 10, 20, 27, 22, 11,  9, 14,  7, 15,  5, 20,  4,  2,  9,
       28, 10,  3,  6, 25,  8, 18, 19,  0, 18, 15, 24,  3, 17, 24, 22, 19,
       22, 23, 28, 14,  6, 21, 17, 12, 29,  0, 13, 14, 13, 12,  9, 20, 18,
       16, 29, 27, 16, 20, 24, 24,  7,  8, 22,  3, 26,  0, 28, 12, 13, 15,
       10, 12,  9, 17, 17, 19,  9, 13,  9, 18,  3,  9,  6, 11,  5, 28,  6,
       20, 22, 23, 22, 21,  4, 22,  0,  8,  4, 28, 12,  9,  3, 21, 23, 12,
       12, 16, 25,  9, 26,  6,  2, 29, 20, 16, 21,  2, 20, 27,  2, 16, 21,
       19,  0, 22,  8, 26, 11, 20, 10,  4, 21,  3, 12, 25,  5, 18, 27, 19,
       17,  4, 21, 16, 21,  6,  0, 14, 26,  2, 27, 14, 10, 21, 16, 18,  6,
        1,  0, 24, 22, 16,  1,  6,  1, 28, 17,  6, 22,  4, 18,  7, 29, 22,
       22, 12,  6, 16,  9, 17,  4,  5, 28, 17, 17,  8, 19,  9, 21, 27, 24,
       12, 27, 29, 25, 15, 20, 11,  6, 14, 19, 10, 19,  3, 16, 10, 25,  7, 21};

  size_t lit_cur_block = 0;
  size_t cmd_cur_block = 0;
  BrotliBuildMetaBlockGreedyInternal(&m, ringbuffer, 0, 0, 0,
      0, literal_context_lut, 1, NULL, cmds, num_commands,
      &lit_block_splits, &lit_cur_block,
      &cmd_block_splits, &cmd_cur_block, &mb);

  /* Check the literals histogram sizes */
  if (mb.literal_histograms_size != 2) {
    return false;
  }
  if (mb.literal_histograms[0].total_count_ != 71) {
    return false;
  }
  if (mb.literal_histograms[1].total_count_ != 6) {
    return false;
  }
  
  /* Check the commands histogram sizes */
  if (mb.command_histograms_size != 3) {
    return false;
  }
  if (mb.command_histograms[0].total_count_ != 3) {
    return false;
  }
  if (mb.command_histograms[1].total_count_ != 1) {
    return false;
  }
  if (mb.command_histograms[2].total_count_ != 2) {
    return false;
  }

  /* Check the commands histogram itself */
  if (mb.command_histograms[0].data_[230] != 1) {
    return false;
  }
  if (mb.command_histograms[0].data_[261] != 1) {
    return false;
  }
  if (mb.command_histograms[0].data_[351] != 1) {
    return false;
  }
  if (mb.command_histograms[1].data_[322] != 1) {
    return false;
  }
  if (mb.command_histograms[2].data_[209] != 1) {
    return false;
  }
  if (mb.command_histograms[2].data_[342] != 1) {
    return false;
  }
  return true;
}
