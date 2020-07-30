/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Block split point selection utilities. */

#include "./block_splitter.h"

#include <string.h>  /* memcpy, memset */

#include "../common/platform.h"
#include "./bit_cost.h"
#include "./cluster.h"
#include "./command.h"
#include "./fast_log.h"
#include "./histogram.h"
#include "./memory.h"
#include "./quality.h"
#include "../include/brotli/encode.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

static const size_t kMaxLiteralHistograms = 100;
static const size_t kMaxCommandHistograms = 50;
static const double kLiteralBlockSwitchCost = 28.1;
static const double kCommandBlockSwitchCost = 13.5;
static const double kDistanceBlockSwitchCost = 14.6;
static const size_t kLiteralStrideLength = 70;
static const size_t kCommandStrideLength = 40;
static const size_t kSymbolsPerLiteralHistogram = 544;
static const size_t kSymbolsPerCommandHistogram = 530;
static const size_t kSymbolsPerDistanceHistogram = 544;
static const size_t kMinLengthForBlockSplitting = 128;
static const size_t kIterMulForRefining = 2;
static const size_t kMinItersForRefining = 100;

static size_t CountLiterals(const Command* cmds, const size_t num_commands) {
  /* Count how many we have. */
  size_t total_length = 0;
  size_t i;
  for (i = 0; i < num_commands; ++i) {
    total_length += cmds[i].insert_len_;
  }
  return total_length;
}

static void CopyLiteralsToByteArray(const Command* cmds,
                                    const size_t num_commands,
                                    const uint8_t* data,
                                    const size_t offset,
                                    const size_t mask,
                                    uint8_t* literals) {
  size_t pos = 0;
  size_t from_pos = offset & mask;
  size_t i;
  for (i = 0; i < num_commands; ++i) {
    size_t insert_len = cmds[i].insert_len_;
    if (from_pos + insert_len > mask) {
      size_t head_size = mask + 1 - from_pos;
      memcpy(literals + pos, data + from_pos, head_size);
      from_pos = 0;
      pos += head_size;
      insert_len -= head_size;
    }
    if (insert_len > 0) {
      memcpy(literals + pos, data + from_pos, insert_len);
      pos += insert_len;
    }
    from_pos = (from_pos + insert_len + CommandCopyLen(&cmds[i])) & mask;
  }
}

void BrotliSplitBlockCommandsFromStored(MemoryManager* m,
                                        const Command* cmds,
                                        const size_t num_commands,
                                        const size_t pos,
                                        const size_t mask,
                                        BlockSplit* cmd_split,
                                        BlockSplitFromDecoder* cmd_split_decoder,
                                        size_t* cur_block_decoder) {

    BROTLI_ENSURE_CAPACITY(
        m, uint8_t, cmd_split->types, cmd_split->types_alloc_size,
        cmd_split_decoder->num_blocks);
    BROTLI_ENSURE_CAPACITY(
        m, uint32_t, cmd_split->lengths, cmd_split->lengths_alloc_size,
        cmd_split_decoder->num_blocks);
    size_t cur_pos = pos;
    cmd_split->num_blocks = 0;
    cmd_split->num_types = 0;
    size_t cur_length = 0;
    int* types_mapping = (int*)malloc(sizeof(int) * cmd_split_decoder->num_types);
    for (int i = 0; i < cmd_split_decoder->num_types; ++i) {
      types_mapping[i] = -1;
    }

    for (int i = 0; i < num_commands; ++i) {
      const Command cmd = cmds[i];
      if (cur_pos >= cmd_split_decoder->positions_end[*cur_block_decoder] &&
            cur_length > 0) {
        if (types_mapping[cmd_split_decoder->types[*cur_block_decoder]] == -1) {
          types_mapping[cmd_split_decoder->types[*cur_block_decoder]] =
                                                          cmd_split->num_types;
        }
        cmd_split->types[cmd_split->num_blocks] =
                    types_mapping[cmd_split_decoder->types[*cur_block_decoder]];
        cmd_split->lengths[cmd_split->num_blocks] = cur_length;
        cur_length = 0;
        cmd_split->num_types = BROTLI_MAX(uint8_t, cmd_split->num_types,
                                  cmd_split->types[cmd_split->num_blocks] + 1);
        cmd_split->num_blocks++;
        (*cur_block_decoder)++;
      }
      while (cur_pos >= cmd_split_decoder->positions_end[*cur_block_decoder]) {
        cur_length = 0;
        (*cur_block_decoder)++;
      }

      if (cur_pos >= cmd_split_decoder->positions_begin[*cur_block_decoder] &&
          cur_pos < cmd_split_decoder->positions_end[*cur_block_decoder]) {
        cur_length++;
      } else {
        // TODO: log that back refs are wrong
      }
      cur_pos += cmds[i].insert_len_ + CommandCopyLen(&cmds[i]);
    }

    if (cur_length > 0) {
      if (types_mapping[cmd_split_decoder->types[*cur_block_decoder]] == -1) {
        types_mapping[cmd_split_decoder->types[*cur_block_decoder]] =
                                                            cmd_split->num_types;
      }
      cmd_split->types[cmd_split->num_blocks] =
                    types_mapping[cmd_split_decoder->types[*cur_block_decoder]];
      cmd_split->lengths[cmd_split->num_blocks] = cur_length;
      cmd_split->num_types = BROTLI_MAX(uint8_t, cmd_split->num_types,
                                    cmd_split->types[cmd_split->num_blocks] + 1);
      cmd_split->num_blocks++;
    }
}



void BrotliSplitBlockLiteralsFromStored(MemoryManager* m,
                                        const Command* cmds,
                                        const size_t num_commands,
                                        const size_t pos,
                                        const size_t mask,
                                        BlockSplit* literal_split,
                                        BlockSplitFromDecoder* literal_split_decoder,
                                        size_t* cur_block_decoder) {
  BROTLI_ENSURE_CAPACITY(
      m, uint8_t, literal_split->types, literal_split->types_alloc_size,
      literal_split_decoder->num_blocks);
  BROTLI_ENSURE_CAPACITY(
      m, uint32_t, literal_split->lengths, literal_split->lengths_alloc_size,
      literal_split_decoder->num_blocks);
  size_t cur_pos = pos;
  literal_split->num_blocks = 0;
  literal_split->num_types = 0;
  size_t cur_length = 0;
  int* types_mapping = (int*)malloc(sizeof(int) *
                                              literal_split_decoder->num_types);
  for (int i = 0; i < literal_split_decoder->num_types; ++i) {
    types_mapping[i] = -1;
  }

  int i = 0;
  while (i < num_commands) {
    if (cur_pos >= literal_split_decoder->positions_end[*cur_block_decoder]) {
      if (cur_length > 0) {
        if (types_mapping[literal_split_decoder->types[*cur_block_decoder]] == -1) {
          types_mapping[literal_split_decoder->types[*cur_block_decoder]] =
                                                      literal_split->num_types;
        }
        literal_split->types[literal_split->num_blocks] =
            types_mapping[literal_split_decoder->types[*cur_block_decoder]];
        literal_split->lengths[literal_split->num_blocks] = cur_length;
        cur_length = 0;
        literal_split->num_types = BROTLI_MAX(uint8_t, literal_split->num_types,
                          literal_split->types[literal_split->num_blocks] + 1);
        literal_split->num_blocks++;
      }
      (*cur_block_decoder)++;

    }
    while (cur_pos >= literal_split_decoder->positions_end[*cur_block_decoder]) {
      (*cur_block_decoder)++;
    }
    if (cur_pos < literal_split_decoder->positions_begin[*cur_block_decoder]) {
      if (cur_pos + cmds[i].insert_len_ <=
                  literal_split_decoder->positions_end[*cur_block_decoder]) {
        cur_length += cur_pos + cmds[i].insert_len_ -
                  literal_split_decoder->positions_begin[*cur_block_decoder];
        cur_pos += cmds[i].insert_len_ + CommandCopyLen(&cmds[i]);
        i++;
      } else {
        cur_length += literal_split_decoder->positions_end[*cur_block_decoder] -
                      literal_split_decoder->positions_begin[*cur_block_decoder];
        if (cur_length > 0) {
          if (types_mapping[literal_split_decoder->types[*cur_block_decoder]] == -1) {
            types_mapping[literal_split_decoder->types[*cur_block_decoder]] =
                                                        literal_split->num_types;
          }
          literal_split->types[literal_split->num_blocks] =
              types_mapping[literal_split_decoder->types[*cur_block_decoder]];
          literal_split->lengths[literal_split->num_blocks] = cur_length;
          cur_length = 0;
          literal_split->num_types = BROTLI_MAX(uint8_t, literal_split->num_types,
                            literal_split->types[literal_split->num_blocks] + 1);
          literal_split->num_blocks++;
        }
        (*cur_block_decoder)++;
      }
    } else if (cur_pos < literal_split_decoder->positions_end[*cur_block_decoder]) {
      if (cur_pos + cmds[i].insert_len_ <=
                literal_split_decoder->positions_end[*cur_block_decoder]) {
        cur_length += cmds[i].insert_len_;
        cur_pos += cmds[i].insert_len_ + CommandCopyLen(&cmds[i]);
        i++;
      } else {
        cur_length += literal_split_decoder->positions_end[*cur_block_decoder]
                                                                      - cur_pos;
        if (cur_length > 0) {
          if (types_mapping[literal_split_decoder->types[*cur_block_decoder]] == -1) {
            types_mapping[literal_split_decoder->types[*cur_block_decoder]] =
                                                        literal_split->num_types;
          }
          literal_split->types[literal_split->num_blocks] =
                  types_mapping[literal_split_decoder->types[*cur_block_decoder]];
          literal_split->lengths[literal_split->num_blocks] = cur_length;
          cur_length = 0;
          literal_split->num_types = BROTLI_MAX(uint8_t, literal_split->num_types,
                            literal_split->types[literal_split->num_blocks] + 1);
          literal_split->num_blocks++;
        }
        (*cur_block_decoder)++;
      }
    }
  }
  if (cur_length > 0) {
    if (types_mapping[literal_split_decoder->types[*cur_block_decoder]] == -1) {
      types_mapping[literal_split_decoder->types[*cur_block_decoder]] =
                                                        literal_split->num_types;
    }
    literal_split->types[literal_split->num_blocks] =
                types_mapping[literal_split_decoder->types[*cur_block_decoder]];
    literal_split->lengths[literal_split->num_blocks] = cur_length;
    literal_split->num_types = BROTLI_MAX(uint8_t, literal_split->num_types,
                            literal_split->types[literal_split->num_blocks] + 1);
    literal_split->num_blocks++;
  }
}


static BROTLI_INLINE uint32_t MyRand(uint32_t* seed) {
  /* Initial seed should be 7. In this case, loop length is (1 << 29). */
  *seed *= 16807U;
  return *seed;
}

static BROTLI_INLINE double BitCost(size_t count) {
  return count == 0 ? -2.0 : FastLog2(count);
}

#define HISTOGRAMS_PER_BATCH 64
#define CLUSTERS_PER_BATCH 16

#define FN(X) X ## Literal
#define DataType uint8_t
/* NOLINTNEXTLINE(build/include) */
#include "./block_splitter_inc.h"
#undef DataType
#undef FN

#define FN(X) X ## Command
#define DataType uint16_t
/* NOLINTNEXTLINE(build/include) */
#include "./block_splitter_inc.h"
#undef FN

#define FN(X) X ## Distance
/* NOLINTNEXTLINE(build/include) */
#include "./block_splitter_inc.h"
#undef DataType
#undef FN

void BrotliInitBlockSplit(BlockSplit* self) {
  self->num_types = 0;
  self->num_blocks = 0;
  self->types = 0;
  self->lengths = 0;
  self->types_alloc_size = 0;
  self->lengths_alloc_size = 0;
}

void BrotliDestroyBlockSplit(MemoryManager* m, BlockSplit* self) {
  BROTLI_FREE(m, self->types);
  BROTLI_FREE(m, self->lengths);
}

void BrotliSplitBlock(MemoryManager* m,
                      const Command* cmds,
                      const size_t num_commands,
                      const uint8_t* data,
                      const size_t pos,
                      const size_t mask,
                      const BrotliEncoderParams* params,
                      BlockSplit* literal_split,
                      BlockSplit* insert_and_copy_split,
                      BlockSplit* dist_split,
                      BlockSplitFromDecoder* literals_block_splits_decoder,
                      size_t* current_block_literals,
                      BlockSplitFromDecoder* cmds_block_splits_decoder,
                      size_t* current_block_cmds) {

  {
    size_t literals_count = CountLiterals(cmds, num_commands);
    uint8_t* literals = BROTLI_ALLOC(m, uint8_t, literals_count);
    if (BROTLI_IS_OOM(m) || BROTLI_IS_NULL(literals)) return;
    /* Create a continuous array of literals. */
    CopyLiteralsToByteArray(cmds, num_commands, data, pos, mask, literals);

    /* Create the block split on the array of literals.
-       Literal histograms have alphabet size 256. */
    if (literals_block_splits_decoder == NULL) {
      SplitByteVectorLiteral(
          m, literals, literals_count,
          kSymbolsPerLiteralHistogram, kMaxLiteralHistograms,
          kLiteralStrideLength, kLiteralBlockSwitchCost, params,
          literal_split);
    } else {
      BrotliSplitBlockLiteralsFromStored(m, cmds, num_commands, pos, mask,
                                         literal_split,
                                         literals_block_splits_decoder,
                                         current_block_literals);
    }
    if (BROTLI_IS_OOM(m)) return;
    BROTLI_FREE(m, literals);
  }

  {
    /* Compute prefix codes for commands. */
    uint16_t* insert_and_copy_codes = BROTLI_ALLOC(m, uint16_t, num_commands);
    size_t i;
    if (BROTLI_IS_OOM(m) || BROTLI_IS_NULL(insert_and_copy_codes)) return;
    for (i = 0; i < num_commands; ++i) {
      insert_and_copy_codes[i] = cmds[i].cmd_prefix_;
    }

    /* Create the block split on the array of command prefixes. */
    if (cmds_block_splits_decoder == NULL) {
      SplitByteVectorCommand(
          m, insert_and_copy_codes, num_commands,
          kSymbolsPerCommandHistogram, kMaxCommandHistograms,
          kCommandStrideLength, kCommandBlockSwitchCost, params,
          insert_and_copy_split);
    } else {
      BrotliSplitBlockCommandsFromStored(m, cmds, num_commands, pos, mask,
                                         insert_and_copy_split,
                                         cmds_block_splits_decoder,
                                         current_block_cmds);
    }
    if (BROTLI_IS_OOM(m)) return;
    /* TODO: reuse for distances? */
    BROTLI_FREE(m, insert_and_copy_codes);
  }

  {
    /* Create a continuous array of distance prefixes. */
    uint16_t* distance_prefixes = BROTLI_ALLOC(m, uint16_t, num_commands);
    size_t j = 0;
    size_t i;
    if (BROTLI_IS_OOM(m) || BROTLI_IS_NULL(distance_prefixes)) return;
    for (i = 0; i < num_commands; ++i) {
      const Command* cmd = &cmds[i];
      if (CommandCopyLen(cmd) && cmd->cmd_prefix_ >= 128) {
        distance_prefixes[j++] = cmd->dist_prefix_ & 0x3FF;
      }
    }
    /* Create the block split on the array of distance prefixes. */
    if (literals_block_splits_decoder == NULL) {
      SplitByteVectorDistance(
          m, distance_prefixes, j,
          kSymbolsPerDistanceHistogram, kMaxCommandHistograms,
          kCommandStrideLength, kDistanceBlockSwitchCost, params,
          dist_split);
    } else {
      SplitByteVectorDistance(
          m, distance_prefixes, j,
          kSymbolsPerDistanceHistogram, 1,
          kCommandStrideLength, kDistanceBlockSwitchCost, params,
          dist_split);
    }
    if (BROTLI_IS_OOM(m)) return;
    BROTLI_FREE(m, distance_prefixes);
  }
}

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif
