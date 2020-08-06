/* Copyright 2020 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

#include "../compress_similar_files/compress_similar_files.c"
#include "helper.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>


bool TestSkipBlocksAndMergeSaveTypes() {
  BlockSplitFromDecoder block_splits;
  block_splits.num_blocks = 6;
  block_splits.num_types = 3;
  uint8_t types[6] = {0, 1, 0, 2, 1, 0};
  uint32_t positions_begin[6] = {0, 520, 562, 700, 1020, 1500};
  uint32_t positions_end[6] = {520, 562, 700, 1020, 1500, 2100};

  block_splits.types = (uint8_t*) malloc(sizeof(uint8_t) * 10);
  block_splits.positions_begin = (uint32_t*) malloc(sizeof(uint8_t) * 10);
  block_splits.positions_end = (uint32_t*) malloc(sizeof(uint8_t) * 10);
  block_splits.types = types;
  block_splits.positions_begin = positions_begin;
  block_splits.positions_end = positions_end;
  MemoryManager m;
  BrotliInitMemoryManager(&m, 0, 0, 0);
  Command* cmds = (Command* )malloc(sizeof(Command) * 10);
  BrotliDistanceParams dist_params = {0, 0, 64, 64, 67108860};
  InitCommand(&cmds[0], /*dist=*/ &dist_params, /*insertlen=*/10, /*copylen*/7,
  /*copylen_code_delta=*/0, /*distance_code=*/613); // -> position 17
  InitCommand(&cmds[1], /*dist=*/ &dist_params, /*insertlen=*/30, /*copylen*/54,
  /*copylen_code_delta=*/0, /*distance_code=*/103); // -> position 101
  InitCommand(&cmds[2], /*dist=*/ &dist_params, /*insertlen=*/4, /*copylen*/123,
  /*copylen_code_delta=*/0, /*distance_code=*/30); // -> position 228
  InitCommand(&cmds[3], /*dist=*/ &dist_params, /*insertlen=*/230, /*copylen*/14,
  /*copylen_code_delta=*/0, /*distance_code=*/101); // -> position 472
  InitCommand(&cmds[4], /*dist=*/ &dist_params, /*insertlen=*/40, /*copylen*/89,
  /*copylen_code_delta=*/0, /*distance_code=*/1023); // -> position 601
  InitCommand(&cmds[5], /*dist=*/ &dist_params, /*insertlen=*/106, /*copylen*/301,
  /*copylen_code_delta=*/0, /*distance_code=*/2010); // -> position 1008
  InitCommand(&cmds[6], /*dist=*/ &dist_params, /*insertlen=*/3, /*copylen*/15,
  /*copylen_code_delta=*/0, /*distance_code=*/104); // -> position 1026
  InitCommand(&cmds[7], /*dist=*/ &dist_params, /*insertlen=*/59, /*copylen*/398,
  /*copylen_code_delta=*/0, /*distance_code=*/807); // -> position 1483
  InitCommand(&cmds[8], /*dist=*/ &dist_params, /*insertlen=*/221, /*copylen*/202,
  /*copylen_code_delta=*/0, /*distance_code=*/1023); // -> position 1906
  InitCommand(&cmds[9], /*dist=*/ &dist_params, /*insertlen=*/38, /*copylen*/155,
  /*copylen_code_delta=*/0, /*distance_code=*/506); // -> position 2100

  BlockSplit cmd_split;
  cmd_split.types = (uint8_t*)malloc(sizeof(uint8_t) * 10);
  cmd_split.lengths = (uint32_t*)malloc(sizeof(uint32_t) * 10);
  cmd_split.types_alloc_size = 10;
  cmd_split.lengths_alloc_size = 10;
  size_t cur_block_decoder = 0;
  int num_commands = 10;
  BrotliSplitBlockCommandsFromStored(&m, cmds, num_commands, 0, 0, &cmd_split,
                                      &block_splits, &cur_block_decoder);

  BlockSplit lit_split;
  cur_block_decoder = 0;
  lit_split.types = (uint8_t*)malloc(sizeof(uint8_t) * 10);
  lit_split.lengths = (uint32_t*)malloc(sizeof(uint32_t) * 10);
  lit_split.types_alloc_size = 10;
  lit_split.lengths_alloc_size = 10;

  BrotliSplitBlockLiteralsFromStored(&m, cmds, num_commands, 0, 0, &lit_split,
                                      &block_splits, &cur_block_decoder);

  /* Check commands split */
  /* Check adjacent types */
  for (int i = 1; i < cmd_split.num_blocks; ++i) {
    if (cmd_split.types[i] == cmd_split.types[i - 1]) {
      return false;
    }
  }
  /* Check non zero length */
  for (int i = 0; i < cmd_split.num_blocks; ++i) {
    if (cmd_split.lengths[i] == 0) {
      return false;
    }
  }
  /* Check num types */
  if (CountUniqueElements(cmd_split.types, cmd_split.num_blocks)
             != cmd_split.num_types) {
    return false;
  }
  /* Check that amount of symbols in blocks is right */
  int count_cmds = 0;
  for (int i = 0; i < cmd_split.num_blocks; ++i) {
    count_cmds += cmd_split.lengths[i];
  }
  if (count_cmds != num_commands) {
    return false;
  }

  /* Check literals split */
  /* Check adjacent types */
  for (int i = 1; i < lit_split.num_blocks; ++i) {
    if (lit_split.types[i] == lit_split.types[i - 1]) {
      return false;
    }
  }
  /* Check non zero length */
  for (int i = 0; i < lit_split.num_blocks; ++i) {
    if (lit_split.lengths[i] == 0) {
      return false;
    }
  }
  /* Check num types */
  if (CountUniqueElements(lit_split.types, lit_split.num_blocks)
             != lit_split.num_types) {
    return false;
  }
  /* Check that amount of symbols in blocks is right */
  int count_litarals = 0;
  for (int i = 0; i < lit_split.num_blocks; ++i) {
    count_litarals += lit_split.lengths[i];
  }
  int num_literals = 0;
  for (int i = 0; i < num_commands; ++i) {
    num_literals += cmds[i].insert_len_;
  }
  if (count_litarals != num_literals) {
    return false;
  }
  return true;
}

bool TestSimple() {
  BlockSplitFromDecoder block_splits;
  block_splits.num_blocks = 6;
  block_splits.num_types = 3;
  uint8_t types[2] = {0, 1};
  uint32_t positions_begin[2] = {0, 520};
  uint32_t positions_end[2] = {520, 562};

  block_splits.types = (uint8_t*) malloc(sizeof(uint8_t) * 10);
  block_splits.positions_begin = (uint32_t*) malloc(sizeof(uint8_t) * 10);
  block_splits.positions_end = (uint32_t*) malloc(sizeof(uint8_t) * 10);
  block_splits.types = types;
  block_splits.positions_begin = positions_begin;
  block_splits.positions_end = positions_end;
  MemoryManager m;
  BrotliInitMemoryManager(&m, 0, 0, 0);
  Command* cmds = (Command* )malloc(sizeof(Command) * 10);
  BrotliDistanceParams dist_params = {0, 0, 64, 64, 67108860};
  InitCommand(&cmds[0], /*dist=*/ &dist_params, /*insertlen=*/10, /*copylen*/230,
  /*copylen_code_delta=*/0, /*distance_code=*/613); // -> position 240
  InitCommand(&cmds[1], /*dist=*/ &dist_params, /*insertlen=*/20, /*copylen*/283,
  /*copylen_code_delta=*/0, /*distance_code=*/103); // -> position 543
  InitCommand(&cmds[2], /*dist=*/ &dist_params, /*insertlen=*/1, /*copylen*/18,
  /*copylen_code_delta=*/0, /*distance_code=*/30); // -> position 228

  BlockSplit cmd_split;
  cmd_split.types = (uint8_t*)malloc(sizeof(uint8_t) * 10);
  cmd_split.lengths = (uint32_t*)malloc(sizeof(uint32_t) * 10);
  cmd_split.types_alloc_size = 10;
  cmd_split.lengths_alloc_size = 10;
  size_t cur_block_decoder = 0;
  int num_commands = 3;
  BrotliSplitBlockCommandsFromStored(&m, cmds, num_commands, 0, 0, &cmd_split,
                                      &block_splits, &cur_block_decoder);
  BlockSplit lit_split;
  cur_block_decoder = 0;
  lit_split.types = (uint8_t*)malloc(sizeof(uint8_t) * 10);
  lit_split.lengths = (uint32_t*)malloc(sizeof(uint32_t) * 10);
  lit_split.types_alloc_size = 10;
  lit_split.lengths_alloc_size = 10;
  BrotliSplitBlockLiteralsFromStored(&m, cmds, num_commands, 0, 0, &lit_split,
                                      &block_splits, &cur_block_decoder);
  /* Check commands split */
  /* Check adjacent types */
  for (int i = 1; i < cmd_split.num_blocks; ++i) {
    if (cmd_split.types[i] == cmd_split.types[i - 1]) {
      return false;
    }
  }
  /* Check non zero length */
  for (int i = 0; i < cmd_split.num_blocks; ++i) {
    if (cmd_split.lengths[i] == 0) {
      return false;
    }
  }
  /* Check num types */
  if (CountUniqueElements(cmd_split.types, cmd_split.num_blocks)
             != cmd_split.num_types) {
    return false;
  }
  /* Check that amount of symbols in blocks is right */
  int count_cmds = 0;
  for (int i = 0; i < cmd_split.num_blocks; ++i) {
    count_cmds += cmd_split.lengths[i];
  }
  if (count_cmds != num_commands) {
    return false;
  }

  /* Check literals split */
  /* Check adjacent types */
  for (int i = 1; i < lit_split.num_blocks; ++i) {
    if (lit_split.types[i] == lit_split.types[i - 1]) {
      return false;
    }
  }
  /* Check non zero length */
  for (int i = 0; i < lit_split.num_blocks; ++i) {
    if (lit_split.lengths[i] == 0) {
      return false;
    }
  }
  /* Check num types */
  if (CountUniqueElements(lit_split.types, lit_split.num_blocks)
             != lit_split.num_types) {
    return false;
  }
  /* Check that amount of symbols in blocks is right */
  int count_litarals = 0;
  for (int i = 0; i < lit_split.num_blocks; ++i) {
    count_litarals += lit_split.lengths[i];
  }
  int num_literals = 0;
  for (int i = 0; i < num_commands; ++i) {
    num_literals += cmds[i].insert_len_;
  }
  if (count_litarals != num_literals) {
    return false;
  }
  return true;
}

bool TestOneBlockType() {
  BlockSplitFromDecoder block_splits;
  block_splits.num_blocks = 6;
  block_splits.num_types = 3;
  uint8_t types[1] = {0};
  uint32_t positions_begin[1] = {0};
  uint32_t positions_end[1] = {520};

  block_splits.types = (uint8_t*) malloc(sizeof(uint8_t) * 10);
  block_splits.positions_begin = (uint32_t*) malloc(sizeof(uint8_t) * 10);
  block_splits.positions_end = (uint32_t*) malloc(sizeof(uint8_t) * 10);
  block_splits.types = types;
  block_splits.positions_begin = positions_begin;
  block_splits.positions_end = positions_end;
  MemoryManager m;
  BrotliInitMemoryManager(&m, 0, 0, 0);
  Command* cmds = (Command* )malloc(sizeof(Command) * 10);
  BrotliDistanceParams dist_params = {0, 0, 64, 64, 67108860};
  InitCommand(&cmds[0], /*dist=*/ &dist_params, /*insertlen=*/10, /*copylen*/130,
  /*copylen_code_delta=*/0, /*distance_code=*/613); // -> position 140
  InitCommand(&cmds[1], /*dist=*/ &dist_params, /*insertlen=*/20, /*copylen*/283,
  /*copylen_code_delta=*/0, /*distance_code=*/103); // -> position 443
  InitCommand(&cmds[2], /*dist=*/ &dist_params, /*insertlen=*/2, /*copylen*/75,
  /*copylen_code_delta=*/0, /*distance_code=*/30); // -> position 228

  BlockSplit cmd_split;
  cmd_split.types = (uint8_t*)malloc(sizeof(uint8_t) * 10);
  cmd_split.lengths = (uint32_t*)malloc(sizeof(uint32_t) * 10);
  cmd_split.types_alloc_size = 10;
  cmd_split.lengths_alloc_size = 10;
  size_t cur_block_decoder = 0;
  int num_commands = 3;
  BrotliSplitBlockCommandsFromStored(&m, cmds, num_commands, 0, 0, &cmd_split,
                                      &block_splits, &cur_block_decoder);
  BlockSplit lit_split;
  cur_block_decoder = 0;
  lit_split.types = (uint8_t*)malloc(sizeof(uint8_t) * 10);
  lit_split.lengths = (uint32_t*)malloc(sizeof(uint32_t) * 10);
  lit_split.types_alloc_size = 10;
  lit_split.lengths_alloc_size = 10;
  BrotliSplitBlockLiteralsFromStored(&m, cmds, num_commands, 0, 0, &lit_split,
                                      &block_splits, &cur_block_decoder);

  /* Check commands split */
  /* Check adjacent types */
  for (int i = 1; i < cmd_split.num_blocks; ++i) {
    if (cmd_split.types[i] == cmd_split.types[i - 1]) {
      return false;
    }
  }
  /* Check non zero length */
  for (int i = 0; i < cmd_split.num_blocks; ++i) {
    if (cmd_split.lengths[i] == 0) {
      return false;
    }
  }
  /* Check num types */
  if (CountUniqueElements(cmd_split.types, cmd_split.num_blocks)
             != cmd_split.num_types) {
    return false;
  }

  /* Check that amount of symbols in blocks is right */
  int count_cmds = 0;
  for (int i = 0; i < cmd_split.num_blocks; ++i) {
    count_cmds += cmd_split.lengths[i];
  }
  if (count_cmds != num_commands) {
    return false;
  }
  /* Check literals split */
  /* Check adjacent types */
  for (int i = 1; i < lit_split.num_blocks; ++i) {
    if (lit_split.types[i] == lit_split.types[i - 1]) {
      return false;
    }
  }
  /* Check non zero length */
  for (int i = 0; i < lit_split.num_blocks; ++i) {
    if (lit_split.lengths[i] == 0) {
      return false;
    }
  }
  /* Check num types */
  if (CountUniqueElements(lit_split.types, lit_split.num_blocks)
             != lit_split.num_types) {
    return false;
  }
  /* Check that amount of symbols in blocks is right */
  int count_litarals = 0;
  for (int i = 0; i < lit_split.num_blocks; ++i) {
    count_litarals += lit_split.lengths[i];
  }
  int num_literals = 0;
  for (int i = 0; i < num_commands; ++i) {
    num_literals += cmds[i].insert_len_;
  }
  if (count_litarals != num_literals) {
    return false;
  }
  return true;
}
