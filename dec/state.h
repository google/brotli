/* Copyright 2015 Google Inc. All Rights Reserved.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

/* Brotli state for partial streaming decoding. */

#ifndef BROTLI_DEC_STATE_H_
#define BROTLI_DEC_STATE_H_

#include <stdio.h>
#include "./bit_reader.h"
#include "./huffman.h"
#include "./streams.h"
#include "./types.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

typedef enum {
  BROTLI_STATE_UNINITED,
  BROTLI_STATE_BITREADER_WARMUP,
  BROTLI_STATE_METABLOCK_BEGIN,
  BROTLI_STATE_METABLOCK_HEADER,
  BROTLI_STATE_CONTEXT_MODES,
  BROTLI_STATE_COMMAND_BEGIN,
  BROTLI_STATE_COMMAND_INNER,
  BROTLI_STATE_UNCOMPRESSED,
  BROTLI_STATE_METADATA,
  BROTLI_STATE_COMMAND_INNER_WRITE,
  BROTLI_STATE_METABLOCK_DONE,
  BROTLI_STATE_COMMAND_POST_WRITE_1,
  BROTLI_STATE_COMMAND_POST_WRITE_2,
  BROTLI_STATE_COMMAND_POST_WRAP_COPY,
  BROTLI_STATE_HUFFMAN_CODE_0,
  BROTLI_STATE_HUFFMAN_CODE_1,
  BROTLI_STATE_HUFFMAN_CODE_2,
  BROTLI_STATE_HUFFMAN_CODE_3,
  BROTLI_STATE_CONTEXT_MAP_1,
  BROTLI_STATE_CONTEXT_MAP_2,
  BROTLI_STATE_TREE_GROUP,
  BROTLI_STATE_DONE
} BrotliRunningState;

typedef enum {
  BROTLI_STATE_METABLOCK_HEADER_NONE,
  BROTLI_STATE_METABLOCK_HEADER_EMPTY,
  BROTLI_STATE_METABLOCK_HEADER_NIBBLES,
  BROTLI_STATE_METABLOCK_HEADER_SIZE,
  BROTLI_STATE_METABLOCK_HEADER_UNCOMPRESSED,
  BROTLI_STATE_METABLOCK_HEADER_RESERVED,
  BROTLI_STATE_METABLOCK_HEADER_BYTES,
  BROTLI_STATE_METABLOCK_HEADER_METADATA
} BrotliRunningMetablockHeaderState;

typedef enum {
  BROTLI_STATE_UNCOMPRESSED_NONE,
  BROTLI_STATE_UNCOMPRESSED_SHORT,
  BROTLI_STATE_UNCOMPRESSED_FILL,
  BROTLI_STATE_UNCOMPRESSED_COPY,
  BROTLI_STATE_UNCOMPRESSED_WRITE_1,
  BROTLI_STATE_UNCOMPRESSED_WRITE_2,
  BROTLI_STATE_UNCOMPRESSED_WRITE_3
} BrotliRunningUncompressedState;

typedef enum {
  BROTLI_STATE_TREE_GROUP_NONE,
  BROTLI_STATE_TREE_GROUP_LOOP
} BrotliRunningTreeGroupState;

typedef enum {
  BROTLI_STATE_CONTEXT_MAP_NONE,
  BROTLI_STATE_CONTEXT_MAP_READ_PREFIX,
  BROTLI_STATE_CONTEXT_MAP_HUFFMAN,
  BROTLI_STATE_CONTEXT_MAP_DECODE
} BrotliRunningContextMapState;

typedef enum {
  BROTLI_STATE_HUFFMAN_NONE,
  BROTLI_STATE_HUFFMAN_LENGTH_SYMBOLS
} BrotliRunningHuffmanState;

typedef enum {
  BROTLI_STATE_DECODE_UINT8_NONE,
  BROTLI_STATE_DECODE_UINT8_SHORT,
  BROTLI_STATE_DECODE_UINT8_LONG
} BrotliRunningDecodeUint8State;

typedef struct {
  BrotliRunningState state;
  /* This counter is reused for several disjoint loops. */
  BrotliBitReader br;
  int loop_counter;
  int pos;
  int max_backward_distance;
  int max_backward_distance_minus_custom_dict_size;
  int max_distance;
  int ringbuffer_size;
  int ringbuffer_mask;
  int dist_rb_idx;
  int dist_rb[4];
  uint8_t* ringbuffer;
  uint8_t* ringbuffer_end;
  HuffmanCode* htree_command;
  const uint8_t* context_lookup1;
  const uint8_t* context_lookup2;
  uint8_t* context_map_slice;
  uint8_t* dist_context_map_slice;

  /* This ring buffer holds a few past copy distances that will be used by */
  /* some special distance codes. */
  HuffmanTreeGroup literal_hgroup;
  HuffmanTreeGroup insert_copy_hgroup;
  HuffmanTreeGroup distance_hgroup;
  HuffmanCode* block_type_trees;
  HuffmanCode* block_len_trees;
  /* This is true if the literal context map histogram type always matches the
  block type. It is then not needed to keep the context (faster decoding). */
  int trivial_literal_context;
  int distance_context;
  int meta_block_remaining_len;
  int block_length[3];
  int num_block_types[3];
  int block_type_rb[6];
  int distance_postfix_bits;
  int num_direct_distance_codes;
  int distance_postfix_mask;
  int num_dist_htrees;
  uint8_t* dist_context_map;
  HuffmanCode *literal_htree;
  uint8_t literal_htree_index;
  uint8_t dist_htree_index;
  uint8_t repeat_code_len;
  uint8_t prev_code_len;


  int copy_length;
  int distance_code;

  /* For partial write operations */
  int to_write;
  int partially_written;

  /* For ReadHuffmanCode */
  uint32_t symbol;
  uint32_t repeat;
  uint32_t space;

  HuffmanCode table[32];
  /* List of of symbol chains. */
  uint16_t* symbol_lists;
  /* Storage from symbol_lists. */
  uint16_t symbols_lists_array[BROTLI_HUFFMAN_MAX_CODE_LENGTH + 1 +
      BROTLI_HUFFMAN_MAX_CODE_LENGTHS_SIZE];
  /* Tails of symbol chains. */
  int next_symbol[32];
  uint8_t code_length_code_lengths[18];
  /* Population counts for the code lengths */
  uint16_t code_length_histo[16];

  /* For HuffmanTreeGroupDecode */
  int htree_index;
  HuffmanCode* next;

  /* For DecodeContextMap */
  int context_index;
  int max_run_length_prefix;
  HuffmanCode context_map_table[BROTLI_HUFFMAN_MAX_TABLE_SIZE];

  /* For InverseMoveToFrontTransform */
  int mtf_upper_bound;
  uint8_t mtf[256];

  /* For custom dictionaries */
  const uint8_t* custom_dict;
  int custom_dict_size;

  /* less used attributes are in the end of this struct */
  /* States inside function calls */
  BrotliRunningMetablockHeaderState substate_metablock_header;
  BrotliRunningTreeGroupState substate_tree_group;
  BrotliRunningContextMapState substate_context_map;
  BrotliRunningUncompressedState substate_uncompressed;
  BrotliRunningHuffmanState substate_huffman;
  BrotliRunningDecodeUint8State substate_decode_uint8;

  uint8_t is_last_metablock;
  uint8_t is_uncompressed;
  uint8_t is_metadata;
  uint8_t size_nibbles;
  uint32_t window_bits;

  /* For CopyUncompressedBlockToOutput */
  int nbytes;

  int num_literal_htrees;
  uint8_t* context_map;
  uint8_t* context_modes;
} BrotliState;

void BrotliStateInit(BrotliState* s);
void BrotliStateCleanup(BrotliState* s);
void BrotliStateMetablockBegin(BrotliState* s);
void BrotliStateCleanupAfterMetablock(BrotliState* s);

#if defined(__cplusplus) || defined(c_plusplus)
} /* extern "C" */
#endif

#endif  /* BROTLI_DEC_STATE_H_ */
