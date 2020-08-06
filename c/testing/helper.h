/* Copyright 2020 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

#ifndef BROTLI_TEST_HELPER
#define BROTLI_TEST_HELPER

#include "../third_party/Unity/src/unity.h"
#include <brotli/encode.h>
#include <brotli/decode.h>
#include "../enc/block_splitter.c"
#include "../enc/cluster.c"
#include "../enc/memory.c"
#include "../enc/bit_cost.c"
#include "../enc/metablock.c"
#include "../enc/histogram.c"
#include "../enc/entropy_encode.c"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>


size_t FindFileSize(FILE* file) {
  fseek(file, 0, SEEK_END);
  size_t size = ftell(file);
  fseek(file, 0, SEEK_SET);
  return size;
}

FILE* OpenFile(const char* filename, const char* mode) {
  FILE* file = fopen(filename, mode);
  if (file == NULL) {
    perror("fopen failed");
  }
  return file;
}

bool ReadData(FILE* file, unsigned char** data, size_t* size) {
  *size = FindFileSize(file);
  *data = (unsigned char*) malloc(*size);
  if (0 == fread(*data, 1, *size, file)) {
    return false;
  }
  return true;
}

int CountUniqueElements(uint8_t* array, size_t length) {
     if (length <= 0) return 0;
     int unique_count = 1;

     for (int outer = 1; outer < length; ++outer) {
        int is_unique = 1;
        for (int inner = 0; is_unique && inner < outer; ++inner) {
             if (array[inner] == array[outer]) is_unique = 0;
        }
        if (is_unique) ++unique_count;
     }
     return unique_count;
}

bool BrotliDecompress(const unsigned char* input_data, size_t input_size, unsigned char* output_data,
                        size_t* output_buffer_size, bool save_commands,
                        BackwardReferenceFromDecoder** backward_references,
                        size_t* back_refs_size,
                        BlockSplitFromDecoder* literals_block_splits,
                        BlockSplitFromDecoder* insert_copy_length_block_splits) {
  BROTLI_BOOL save_brotli_commands = BROTLI_FALSE;
  if (save_commands) {
    save_brotli_commands = BROTLI_TRUE;
  }
  if (BrotliDecoderDecompress(input_size, input_data, output_buffer_size, output_data,
                              save_brotli_commands, backward_references,
                              back_refs_size, literals_block_splits,
                              insert_copy_length_block_splits) != 1) {
    return false;
  }
  return true;
}

size_t BrotliCompress(int level, int window, const unsigned char* input_data, size_t input_size,
                      unsigned char* output_data, size_t* output_buffer_size,
                      BackwardReferenceFromDecoder** backward_references, size_t back_refs_size,
                      BlockSplitFromDecoder* literals_block_splits,
                      BlockSplitFromDecoder* insert_copy_length_block_splits) {
  if (!BrotliEncoderCompress(level, window, BROTLI_MODE_GENERIC, input_size, input_data,
                             output_buffer_size, output_data, backward_references,
                             back_refs_size, literals_block_splits,
                             insert_copy_length_block_splits)) {
    return false;
  }
  return true;
}

bool BrotliCompressDecompress(const unsigned char* input_data, size_t input_size,
                              int level,
                              BackwardReferenceFromDecoder** backward_references,
                              size_t* back_refs_size,
                              BlockSplitFromDecoder* literals_block_splits,
                              BlockSplitFromDecoder* insert_copy_length_block_splits) {
  size_t compressed_buffer_size = input_size * 3;
  unsigned char* compressed_data = (unsigned char*) malloc(compressed_buffer_size);
  BackwardReferenceFromDecoder* backward_references_ = NULL;
  size_t back_refs_size_ = 0;
  int window = MinWindowLargerThanFile(input_size, DEFAULT_WINDOW);
  BlockSplitFromDecoder* literals_block_splits_ = NULL;
  BlockSplitFromDecoder* insert_copy_length_block_splits_ = NULL;
  if (!BrotliCompress(level, window, input_data, input_size,
                      compressed_data, &compressed_buffer_size,
                      &backward_references_, back_refs_size_,
                      literals_block_splits_,
                      insert_copy_length_block_splits_)) {
    return false;
  }
  size_t decopressed_size = input_size;
  unsigned char* decompressed_data = (unsigned char*) malloc(decopressed_size);
  if (!BrotliDecompress(compressed_data, compressed_buffer_size,
                        decompressed_data, &decopressed_size, true,
                        backward_references, back_refs_size,
                        literals_block_splits, insert_copy_length_block_splits)) {
    return false;
  }
  return true;
}

bool GetBackwardReferences(const unsigned char* input_data, size_t input_size,
                           int level,
                           BackwardReferenceFromDecoder** backward_references,
                           size_t* backward_references_size) {
  BlockSplitFromDecoder literals_block_splits;
  BlockSplitFromDecoder insert_copy_length_block_splits;
  if (!BrotliCompressDecompress(input_data, input_size, level,
                                backward_references, backward_references_size,
                                &literals_block_splits,
                                &insert_copy_length_block_splits)) {
    return false;
  }
  return true;
}

bool GetBlockSplits(const unsigned char* input_data, size_t input_size,
                   int level, BlockSplitFromDecoder* literals_block_splits,
                   BlockSplitFromDecoder* insert_copy_length_block_splits) {
  BackwardReferenceFromDecoder* backward_references;
  size_t back_refs_size;
  if (!BrotliCompressDecompress(input_data, input_size, level,
                                &backward_references, &back_refs_size,
                                literals_block_splits,
                                insert_copy_length_block_splits)) {
    return false;
  }
  return true;
}

bool GetNewBackwardReferences(const unsigned char* input_data, size_t input_size,
                             int level, int start, int end,
                             BackwardReferenceFromDecoder** new_backward_references,
                             size_t* new_backward_references_size,
                             unsigned char** removed_data, size_t* removed_data_size) {
  BlockSplitFromDecoder literals_block_splits;
  BlockSplitFromDecoder insert_copy_length_block_splits;
  BackwardReferenceFromDecoder* backward_references;
  size_t back_refs_size;
  if (!BrotliCompressDecompress(input_data, input_size, level,
                                &backward_references, &back_refs_size,
                                &literals_block_splits,
                                &insert_copy_length_block_splits)) {
    return false;
  }

  *removed_data = (unsigned char*) malloc(input_size);
  *removed_data_size = 0;
  int window = MinWindowLargerThanFile(input_size, DEFAULT_WINDOW);
  *new_backward_references_size = RemoveBackwardReferencesPart(
                                backward_references, back_refs_size,
                                start, end, new_backward_references, window);
  for (int i = 0; i < input_size; ++i) {
    if (i < start || i >= end) {
      (*removed_data)[*removed_data_size] = input_data[i];
      (*removed_data_size)++;
    }
  }
  return true;
}

bool GetNewBlockSplits(const unsigned char* input_data, size_t input_size,
                       int level, int start, int end,
                       BlockSplitFromDecoder* new_literals_block_splits,
                       BlockSplitFromDecoder* new_insert_copy_length_block_splits,
                       unsigned char** removed_data, size_t* removed_data_size) {
  BlockSplitFromDecoder literals_block_splits;
  BlockSplitFromDecoder insert_copy_length_block_splits;
  BackwardReferenceFromDecoder* backward_references;
  size_t back_refs_size;
  if (!BrotliCompressDecompress(input_data, input_size, level,
                                &backward_references, &back_refs_size,
                                &literals_block_splits,
                                &insert_copy_length_block_splits)) {
    return false;
  }
  RemoveBlockSplittingPart(&literals_block_splits, start, end,
                           new_literals_block_splits);
  RemoveBlockSplittingPart(&insert_copy_length_block_splits, start, end,
                           new_insert_copy_length_block_splits);

  *removed_data = (unsigned char*) malloc(input_size);
  *removed_data_size = 0;

  for (int i = 0; i < input_size; ++i) {
    if (i < start || i >= end) {
      (*removed_data)[*removed_data_size] = input_data[i];
      (*removed_data_size)++;
    }
  }
  return true;
}

#endif  /* BROTLI_TEST_HELPER */
