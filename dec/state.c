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

#include "./huffman.h"
#include "./state.h"

#include <stdlib.h>
#include <string.h>

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

void BrotliStateInit(BrotliState* s) {
  BrotliInitBitReader(&s->br);
  s->state = BROTLI_STATE_UNINITED;
  s->substate_metablock_header = BROTLI_STATE_METABLOCK_HEADER_NONE;
  s->substate_tree_group = BROTLI_STATE_TREE_GROUP_NONE;
  s->substate_context_map = BROTLI_STATE_CONTEXT_MAP_NONE;
  s->substate_uncompressed = BROTLI_STATE_UNCOMPRESSED_NONE;
  s->substate_huffman = BROTLI_STATE_HUFFMAN_NONE;
  s->substate_decode_uint8 = BROTLI_STATE_DECODE_UINT8_NONE;
  s->substate_read_block_length = BROTLI_STATE_READ_BLOCK_LENGTH_NONE;

  s->buffer_length = 0;
  s->loop_counter = 0;
  s->pos = 0;
  s->rb_roundtrips = 0;
  s->partial_pos_out = 0;

  s->block_type_trees = NULL;
  s->block_len_trees = NULL;
  s->ringbuffer = NULL;

  s->context_map = NULL;
  s->context_modes = NULL;
  s->dist_context_map = NULL;
  s->context_map_slice = NULL;
  s->dist_context_map_slice = NULL;

  s->sub_loop_counter = 0;

  s->literal_hgroup.codes = NULL;
  s->literal_hgroup.htrees = NULL;
  s->insert_copy_hgroup.codes = NULL;
  s->insert_copy_hgroup.htrees = NULL;
  s->distance_hgroup.codes = NULL;
  s->distance_hgroup.htrees = NULL;


  s->custom_dict = NULL;
  s->custom_dict_size = 0;

  s->is_last_metablock = 0;
  s->window_bits = 0;
  s->max_distance = 0;
  s->dist_rb[0] = 16;
  s->dist_rb[1] = 15;
  s->dist_rb[2] = 11;
  s->dist_rb[3] = 4;
  s->dist_rb_idx = 0;
  s->block_type_trees = NULL;
  s->block_len_trees = NULL;

  /* Make small negative indexes addressable. */
  s->symbol_lists = &s->symbols_lists_array[BROTLI_HUFFMAN_MAX_CODE_LENGTH + 1];

  s->mtf_upper_bound = 255;

  s->legacy_input_buffer = 0;
  s->legacy_output_buffer = 0;
  s->legacy_input_len = 0;
  s->legacy_output_len = 0;
  s->legacy_input_pos = 0;
  s->legacy_output_pos = 0;
}

void BrotliStateMetablockBegin(BrotliState* s) {
  s->meta_block_remaining_len = 0;
  s->block_length[0] = 1U << 28;
  s->block_length[1] = 1U << 28;
  s->block_length[2] = 1U << 28;
  s->num_block_types[0] = 1;
  s->num_block_types[1] = 1;
  s->num_block_types[2] = 1;
  s->block_type_rb[0] = 1;
  s->block_type_rb[1] = 0;
  s->block_type_rb[2] = 1;
  s->block_type_rb[3] = 0;
  s->block_type_rb[4] = 1;
  s->block_type_rb[5] = 0;
  s->context_map = NULL;
  s->context_modes = NULL;
  s->dist_context_map = NULL;
  s->context_map_slice = NULL;
  s->literal_htree_index = 0;
  s->literal_htree = NULL;
  s->dist_context_map_slice = NULL;
  s->dist_htree_index = 0;
  s->context_lookup1 = NULL;
  s->context_lookup2 = NULL;
  s->literal_hgroup.codes = NULL;
  s->literal_hgroup.htrees = NULL;
  s->insert_copy_hgroup.codes = NULL;
  s->insert_copy_hgroup.htrees = NULL;
  s->distance_hgroup.codes = NULL;
  s->distance_hgroup.htrees = NULL;
}

void BrotliStateCleanupAfterMetablock(BrotliState* s) {
  BROTLI_FREE(s->context_modes);
  BROTLI_FREE(s->context_map);
  BROTLI_FREE(s->dist_context_map);

  BrotliHuffmanTreeGroupRelease(&s->literal_hgroup);
  BrotliHuffmanTreeGroupRelease(&s->insert_copy_hgroup);
  BrotliHuffmanTreeGroupRelease(&s->distance_hgroup);
}

void BrotliStateCleanup(BrotliState* s) {
  BrotliStateCleanupAfterMetablock(s);

  BROTLI_FREE(s->ringbuffer);
  BROTLI_FREE(s->block_type_trees);
  BROTLI_FREE(s->legacy_input_buffer);
  BROTLI_FREE(s->legacy_output_buffer);
}

int BrotliStateIsStreamStart(const BrotliState* s) {
  return (s->state == BROTLI_STATE_UNINITED &&
      BrotliGetAvailableBits(&s->br) == 0);
}

int BrotliStateIsStreamEnd(const BrotliState* s) {
  return s->state == BROTLI_STATE_DONE;
}


#if defined(__cplusplus) || defined(c_plusplus)
} /* extern "C" */
#endif
