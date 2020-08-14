// Copyright 2020 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <brotli/encode.h>
#include <brotli/decode.h>
#include "compress_similar_files.h"

#include <stdlib.h>
#include <iostream>
#include <string.h>
#include <stdio.h>
#include <fstream>
#include <stdexcept>
#include <time.h>
#include <chrono>

size_t FileSize(FILE* file) {
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

void ReadData(FILE* file, unsigned char** data, size_t* size) {
  *size = FileSize(file);
  *data = (unsigned char*) malloc(*size);
  if (0 == fread(*data, 1, *size, file)) {
    throw "Failed to read from file";
  }
  return;
}

size_t BrotliDecompress(const unsigned char* input_data, size_t input_size, unsigned char* output_data,
                        size_t output_buffer_size, bool save_commands,
                        BackwardReferenceFromDecoder** backward_references,
                        size_t* back_refs_size,
                        BlockSplitFromDecoder* literals_block_splits,
                        BlockSplitFromDecoder* insert_copy_length_block_splits) {
  BROTLI_BOOL save_brotli_commands = BROTLI_FALSE;
  if (save_commands) {
    save_brotli_commands = BROTLI_TRUE;
  }
  if (BrotliDecoderDecompress(input_size, input_data, &output_buffer_size, output_data,
                              save_brotli_commands, backward_references,
                              back_refs_size, literals_block_splits,
                              insert_copy_length_block_splits) != 1) {
    throw "Failure in BrotliDecompress";
  }
  return output_buffer_size;
}


int main (int argc, char** argv) {

  int level = std::stoi(std::string(argv[1]));
  std::string file_name = std::string(argv[2]);
  int start = std::stoi(std::string(argv[3]));
  int end = std::stoi(std::string(argv[4]));

  FILE* infile = OpenFile(file_name.c_str(), "rb");
  if (infile == NULL) {
    exit(1);
  }
  unsigned char* input_data = NULL;
  size_t input_size = 0;
  ReadData(infile, &input_data, &input_size);
  fclose(infile);
  size_t compressed_size = input_size * 2;
  unsigned char* compressed_data = (unsigned char*) malloc(compressed_size);
  size_t output_buffer_size = input_size * 2;
  unsigned char* output_data = (unsigned char*) malloc(output_buffer_size);

  clock_t start_time = clock();
  /* Compress input_data */
  int window = MinWindowLargerThanFile(input_size, DEFAULT_WINDOW);
  BackwardReferenceFromDecoder* backward_references = NULL;
  size_t back_refs_size = 0;
  BlockSplitFromDecoder* literals_block_splits = NULL;
  BlockSplitFromDecoder* insert_copy_length_block_splits = NULL;
  if (!BrotliEncoderCompress(11, window, BROTLI_MODE_GENERIC,
                             input_size, input_data,
                             &compressed_size, compressed_data,
                             backward_references,
                             back_refs_size, literals_block_splits,
                             insert_copy_length_block_splits)) {
    throw "Failure in BrotliCompress";
  }
  free(input_data);

  /* Compress a file which is an input file with [start, end) fragment removed.
     Use artifacts from input_data compression to recompress. */
  if (!BrotliEncoderCompressSimilarDeletion(level, BROTLI_MODE_GENERIC,
                                            compressed_size, compressed_data,
                                            start, end,
                                            &output_buffer_size, output_data)) {
    throw "Failure in BrotliCompressSimilarDeletion";
  }
  clock_t end_time = clock();
  float elapsed_time = (float) (end_time - start_time) / CLOCKS_PER_SEC;

  /* In case you want to check that the result is decompressible */
  unsigned char* removed_data = (unsigned char*) malloc(input_size);
  size_t output_idx = 0;
  for (int i = 0; i < input_size; ++i) {
    if (i < start || i >= end) {
      (*removed_data)[output_idx] = input_data[i];
      output_idx++;
    }
  }
  *removed_data_size = output_idx;

  size_t decompressed_size_ = input_size * 2;
  unsigned char* decompressed_data_ = (unsigned char*) malloc(decompressed_size_);
  size_t total_decompress_size = 0;
  BackwardReferenceFromDecoder* backward_references_;
  size_t back_refs_size_;
  BlockSplitFromDecoder literals_block_splits_;
  BlockSplitFromDecoder insert_copy_length_block_splits_;
  total_decompress_size = BrotliDecompress(compressed_data, compressed_size,
                                          output_data, output_buffer_size,
                                          true,
                                          &backward_references_, &back_refs_size_,
                                          &literals_block_splits_, &insert_copy_length_block_splits_);
  assert(total_decompress_size == removed_data_size);
  assert(memcmp(decompressed_data_, removed_data, removed_data_size)==0);

  free(decompressed_data_);
  free(compressed_data);
  free(output_data);
  std::cout << "Output size = " << output_buffer_size <<
                ", elapsed_time = " << elapsed_time << "\n";
  return 0;
}
