/* Copyright 2015 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

#include "./state.h"

#include <stdlib.h>  /* free, malloc */

#include "./huffman.h"
#include "./types.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/* Declared in decode.h */
int BrotliStateIsStreamStart(const BrotliState* s);
int BrotliStateIsStreamEnd(const BrotliState* s);

static void* DefaultAllocFunc(void* opaque, size_t size) {
  BROTLI_UNUSED(opaque);
  return malloc(size);
}

static void DefaultFreeFunc(void* opaque, void* address) {
  BROTLI_UNUSED(opaque);
  free(address);
}

void BrotliStateInit(BrotliState* s) {
  BrotliStateInitWithCustomAllocators(s, 0, 0, 0);
}

void BrotliStateInitWithCustomAllocators(BrotliState* s,
    brotli_alloc_func alloc_func, brotli_free_func free_func, void* opaque) {
  if (!alloc_func) {
    s->alloc_func = DefaultAllocFunc;
    s->free_func = DefaultFreeFunc;
    s->memory_manager_opaque = 0;
  } else {
    s->alloc_func = alloc_func;
    s->free_func = free_func;
    s->memory_manager_opaque = opaque;
  }

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
  BROTLI_FREE(s, s->context_modes);
  BROTLI_FREE(s, s->context_map);
  BROTLI_FREE(s, s->dist_context_map);

  BrotliHuffmanTreeGroupRelease(s, &s->literal_hgroup);
  BrotliHuffmanTreeGroupRelease(s, &s->insert_copy_hgroup);
  BrotliHuffmanTreeGroupRelease(s, &s->distance_hgroup);
}

void BrotliStateCleanup(BrotliState* s) {
  BrotliStateCleanupAfterMetablock(s);

  BROTLI_FREE(s, s->ringbuffer);
  BROTLI_FREE(s, s->block_type_trees);
}

int BrotliStateIsStreamStart(const BrotliState* s) {
  return (s->state == BROTLI_STATE_UNINITED &&
      BrotliGetAvailableBits(&s->br) == 0);
}

int BrotliStateIsStreamEnd(const BrotliState* s) {
  return s->state == BROTLI_STATE_DONE;
}

void BrotliHuffmanTreeGroupInit(BrotliState* s, HuffmanTreeGroup* group,
    uint32_t alphabet_size, uint32_t ntrees) {
  /* Pack two allocations into one */
  const size_t max_table_size = kMaxHuffmanTableSize[(alphabet_size + 31) >> 5];
  const size_t code_size = sizeof(HuffmanCode) * ntrees * max_table_size;
  const size_t htree_size = sizeof(HuffmanCode*) * ntrees;
  char* p = (char*)BROTLI_ALLOC(s, code_size + htree_size);
  group->alphabet_size = (uint16_t)alphabet_size;
  group->num_htrees = (uint16_t)ntrees;
  group->codes = (HuffmanCode*)p;
  group->htrees = (HuffmanCode**)(p + code_size);
}

void BrotliHuffmanTreeGroupRelease(BrotliState* s, HuffmanTreeGroup* group) {
  BROTLI_FREE(s, group->codes);
  group->htrees = NULL;
}

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif
