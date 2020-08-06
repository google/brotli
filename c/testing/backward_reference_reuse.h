/* Copyright 2020 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

#include "../compress_similar_files/compress_similar_files.c"
#include "helper.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>


bool TestReusageRateSameFile(unsigned char* input_data, size_t input_size, int level) {
  BackwardReferenceFromDecoder* backward_references;
  size_t back_refs_size;
  GetBackwardReferences(input_data, input_size, level,
                        &backward_references, &back_refs_size);

  size_t compressed_buffer_size = input_size * 3;
  unsigned char* compressed_data = (unsigned char*) malloc(compressed_buffer_size);
  int window = MinWindowLargerThanFile(input_size, DEFAULT_WINDOW);
  BlockSplitFromDecoder* literals_block_splits_ = NULL;
  BlockSplitFromDecoder* insert_copy_length_block_splits_ = NULL;
  if (!BrotliCompress(level, window, input_data, input_size,
                      compressed_data, &compressed_buffer_size,
                      &backward_references, back_refs_size,
                      literals_block_splits_,
                      insert_copy_length_block_splits_)) {
    return false;
  }
  size_t decopressed_size = input_size;
  unsigned char* decompressed_data = (unsigned char*) malloc(decopressed_size);
  BackwardReferenceFromDecoder* backward_references_used;
  size_t back_refs_size_used;
  BlockSplitFromDecoder literals_block_splits;
  BlockSplitFromDecoder insert_copy_length_block_splits;
  if (!BrotliDecompress(compressed_data, compressed_buffer_size,
                        decompressed_data, &decopressed_size, true,
                        &backward_references_used, &back_refs_size_used,
                        &literals_block_splits, &insert_copy_length_block_splits)) {
    return false;
  }

  /* Check the reuse rate */
  int index_stored = 0;
  int index_used = 0;
  int count_equal = 0;
  while (index_stored < back_refs_size && index_used < back_refs_size_used) {
    if (backward_references[index_stored].position <
          backward_references_used[index_used].position) {
      index_stored++;
    } else if (backward_references[index_stored].position >
          backward_references_used[index_used].position) {
      index_used++;
    } else {
      if (backward_references[index_stored].distance ==
          backward_references_used[index_used].distance) {
        count_equal++;
        index_stored++;
        index_used++;
      }
    }
  }
  if ((float)count_equal / (float)back_refs_size < 0.97) {
    return false;
  }
  return true;
}

bool TestReusageRateNewFile(unsigned char* input_data, size_t input_size, int level) {
  BackwardReferenceFromDecoder* backward_references;
  size_t back_refs_size = 0;
  unsigned char* removed_data = (unsigned char*) malloc(input_size);
  size_t removed_data_size = 0;
  GetNewBackwardReferences(input_data, input_size, 9, 100, 500,
                          &backward_references, &back_refs_size,
                          &removed_data, &removed_data_size);

  size_t compressed_buffer_size = removed_data_size * 3;
  unsigned char* compressed_data = (unsigned char*) malloc(compressed_buffer_size);
  int window = MinWindowLargerThanFile(input_size, DEFAULT_WINDOW);
  BlockSplitFromDecoder* literals_block_splits_ = NULL;
  BlockSplitFromDecoder* insert_copy_length_block_splits_ = NULL;
  if (!BrotliCompress(level, window, removed_data, removed_data_size,
                      compressed_data, &compressed_buffer_size,
                      &backward_references, back_refs_size,
                      literals_block_splits_,
                      insert_copy_length_block_splits_)) {
    return false;
  }
  size_t decopressed_size = removed_data_size;
  unsigned char* decompressed_data = (unsigned char*) malloc(decopressed_size);
  BackwardReferenceFromDecoder* backward_references_used;
  size_t back_refs_size_used;
  BlockSplitFromDecoder literals_block_splits;
  BlockSplitFromDecoder insert_copy_length_block_splits;
  if (!BrotliDecompress(compressed_data, compressed_buffer_size,
                        decompressed_data, &decopressed_size, true,
                        &backward_references_used, &back_refs_size_used,
                        &literals_block_splits, &insert_copy_length_block_splits)) {
    return false;
  }

  /* Check the reuse rate */
  int index_stored = 0;
  int index_used = 0;
  int count_equal = 0;
  while (index_stored < back_refs_size && index_used < back_refs_size_used) {
    if (backward_references[index_stored].position <
          backward_references_used[index_used].position) {
      index_stored++;
    } else if (backward_references[index_stored].position >
          backward_references_used[index_used].position) {
      index_used++;
    } else {
      if (backward_references[index_stored].distance ==
          backward_references_used[index_used].distance) {
        count_equal++;
        index_stored++;
        index_used++;
      }
    }
  }
  if ((float)count_equal / (float)back_refs_size < 0.97) {
    return false;
  }
  return true;
}
