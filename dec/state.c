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
  s->state = BROTLI_STATE_UNINITED;
  s->substate_metablock_header = BROTLI_STATE_METABLOCK_HEADER_NONE;
  s->substate_tree_group = BROTLI_STATE_TREE_GROUP_NONE;
  s->substate_context_map = BROTLI_STATE_CONTEXT_MAP_NONE;
  s->substate_uncompressed = BROTLI_STATE_UNCOMPRESSED_NONE;
  s->substate_huffman = BROTLI_STATE_HUFFMAN_NONE;
  s->substate_decode_uint8 = BROTLI_STATE_DECODE_UINT8_NONE;

  s->block_type_trees = NULL;
  s->block_len_trees = NULL;
  s->ringbuffer = NULL;

  s->context_map = NULL;
  s->context_modes = NULL;
  s->dist_context_map = NULL;
  s->context_map_slice = NULL;
  s->dist_context_map_slice = NULL;

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
}

void BrotliStateMetablockBegin(BrotliState* s) {
  s->meta_block_remaining_len = 0;
  s->block_length[0] = 1 << 28;
  s->block_length[1] = 1 << 28;
  s->block_length[2] = 1 << 28;
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
  if (s->context_modes != 0) {
    free(s->context_modes);
    s->context_modes = NULL;
  }
  if (s->context_map != 0) {
    free(s->context_map);
    s->context_map = NULL;
  }
  if (s->dist_context_map != 0) {
    free(s->dist_context_map);
    s->dist_context_map = NULL;
  }

  BrotliHuffmanTreeGroupRelease(&s->literal_hgroup);
  BrotliHuffmanTreeGroupRelease(&s->insert_copy_hgroup);
  BrotliHuffmanTreeGroupRelease(&s->distance_hgroup);
  s->literal_hgroup.codes = NULL;
  s->literal_hgroup.htrees = NULL;
  s->insert_copy_hgroup.codes = NULL;
  s->insert_copy_hgroup.htrees = NULL;
  s->distance_hgroup.codes = NULL;
  s->distance_hgroup.htrees = NULL;
}

void BrotliStateCleanup(BrotliState* s) {
  if (s->context_modes != 0) {
    free(s->context_modes);
  }
  if (s->context_map != 0) {
    free(s->context_map);
  }
  if (s->dist_context_map != 0) {
    free(s->dist_context_map);
  }
  BrotliHuffmanTreeGroupRelease(&s->literal_hgroup);
  BrotliHuffmanTreeGroupRelease(&s->insert_copy_hgroup);
  BrotliHuffmanTreeGroupRelease(&s->distance_hgroup);

  if (s->ringbuffer != 0) {
    free(s->ringbuffer);
  }
  if (s->block_type_trees != 0) {
    free(s->block_type_trees);
  }
}

#if defined(__cplusplus) || defined(c_plusplus)
} /* extern "C" */
#endif
