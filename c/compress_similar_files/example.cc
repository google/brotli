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


// #include <stdlib.h>
// #include <string.h>
// #include <stdio.h>
// #include <brotli/encode.h>
// #include <brotli/decode.h>
// #include <zlib.h>
// #include <time.h>
// #include <iostream>
// #include <algorithm>
// #include <sstream>
// #include <iomanip>
// #include <fstream>
// #include <stdexcept>
// #include <string>
// #include <chrono>
// #include <thread>


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



// // percentage < 50
// DeletionCompressionStatistics UsualDeletion(const unsigned char* compressed_data, size_t compressed_size,
//                                             unsigned char* output_compressed_data, size_t output_compressed_size,
//                                             int level, int percentage) {
//
//     // std::cout << "UsualDeletion\n";
//     // Decompress
//     size_t decompressed_buffer_size = compressed_size * 100;
//     unsigned char* decompressed_data = (unsigned char*) malloc(decompressed_buffer_size);
//     BackwardReferenceFromDecoder* backward_references;
//     size_t back_refs_size;
//     BlockSplitFromDecoder literals_block_splits;
//     BlockSplitFromDecoder insert_copy_length_block_splits;
//     clock_t start = clock();
//     size_t decopressed_size = BrotliDecompress(compressed_data, compressed_size,
//                                                decompressed_data, decompressed_buffer_size, false,
//                                                &backward_references, &back_refs_size,
//                                                &literals_block_splits, &insert_copy_length_block_splits);
//     clock_t end = clock();
//     float elapsed_time_decompress = (float) (end - start) / CLOCKS_PER_SEC;
//     // std::cout << "Decompressed\n";
//
//     // Delete
//     size_t start_index = (size_t)(decopressed_size / 2) - (int)((float)(percentage * decopressed_size) / 100 / 2);
//     size_t end_index = (size_t)(decopressed_size / 2) + (int)((float)(percentage * decopressed_size) / 100 / 2);
//
//     start = clock();
//     unsigned char* removed_data = (unsigned char*) malloc(decopressed_size);
//     size_t output_idx = 0;
//     for (int i = 0; i < decopressed_size; ++i) {
//       if (i < start_index || i >= end_index) {
//         removed_data[output_idx] = decompressed_data[i];
//         output_idx++;
//       }
//     }
//     size_t removed_data_size = output_idx;
//     // std::cout << "removed_data_size " << removed_data_size << "\n";
//     end = clock();
//     float elapsed_time_delete = (float) (end - start) / CLOCKS_PER_SEC;
//     // std::cout << "Deleted\n";
//
//     // Compress
//     int window = MinWindowLargerThanFile(removed_data_size, DEFAULT_WINDOW);
//     BackwardReferenceFromDecoder* backward_references_ = NULL;
//     size_t back_refs_size_ = 0;
//     BlockSplitFromDecoder* literals_block_splits_ = NULL;
//     BlockSplitFromDecoder* insert_copy_length_block_splits_ = NULL;
//     start = clock();
//     size_t output_size = BrotliCompress(level, window, removed_data, removed_data_size,
//                                         output_compressed_data, output_compressed_size,
//                                         backward_references_, back_refs_size_,
//                                         literals_block_splits_, insert_copy_length_block_splits_);
//
//     end = clock();
//     float elapsed_time_compress = (float) (end - start) / CLOCKS_PER_SEC;
//     // std::cout << "Compressed\n";
//
//     float comp_speed = (float) (removed_data_size) / (elapsed_time_compress * 1024 * 1024);
//     float dec_speed = (float) (decopressed_size) / (elapsed_time_decompress * 1024 * 1024);
//     float del_speed = (float) (decopressed_size) / (elapsed_time_delete * 1024 * 1024);
//
//     return DeletionCompressionStatistics(output_size, (float)removed_data_size/(float)output_size, dec_speed, del_speed, comp_speed, elapsed_time_compress + elapsed_time_decompress + elapsed_time_delete);
//
//     // return DeletionCompressionStatistics(output_size, (float)removed_data_size/(float)output_size, elapsed_time_decompress, elapsed_time_delete, elapsed_time_compress);
// }

// // percentage < 50
// DeletionCompressionStatistics ReuseDeletion(const unsigned char* compressed_data, size_t compressed_size,
//                                             unsigned char* output_compressed_data, size_t output_compressed_size,
//                                             int level, int percentage, int const1, int const2, int const3) {
//
//     // std::cout << "ReuseDeletion\n";
//     // Decompress
//     // std::string command = "rm backward_references.txt";
//     // system(command.c_str());
//     size_t decompressed_buffer_size = compressed_size * 100;
//     unsigned char* decompressed_data = (unsigned char*) malloc(decompressed_buffer_size);
//     BlockSplitFromDecoder literals_block_splits;
//     BlockSplitFromDecoder insert_copy_length_block_splits;
//     BackwardReferenceFromDecoder* backward_references;
//     size_t back_refs_size;
//     clock_t start = clock();
//     size_t decopressed_size = BrotliDecompress(compressed_data, compressed_size,
//                                               decompressed_data, decompressed_buffer_size, true,
//                                               &backward_references, &back_refs_size,
//                                               &literals_block_splits, &insert_copy_length_block_splits);
//     clock_t end = clock();
//     float elapsed_time_decompress = (float) (end - start) / CLOCKS_PER_SEC;
//     // std::cout << "Decompressed\n";
//
//     // Delete and adjust back refs to file
//     size_t start_index = (size_t)(decopressed_size / 2) - (int)((float)(percentage * decopressed_size) / 100 / 2);
//     size_t end_index = (size_t)(decopressed_size / 2) + (int)((float)(percentage * decopressed_size) / 100 / 2);
//     int window = MinWindowLargerThanFile(decopressed_size, DEFAULT_WINDOW);
//
//     // BackwardReferenceFromDecoder* backward_references = NULL;
//     // std::string filename = "backward_references.txt";
//     // size_t back_refs_size = ReadStoredBackwardReferences(filename.c_str(),
//     //                                                      &backward_references);
//
//     BackwardReferenceFromDecoder* new_backward_references = NULL;
//     unsigned char* removed_data = (unsigned char*) malloc(decopressed_size);
//     size_t removed_data_size = 0;
//     start = clock();
//     size_t new_backward_references_size = RemoveDataPart(decompressed_data, decopressed_size, backward_references, back_refs_size,
//                                                           start_index, end_index,
//                                                           &removed_data, &removed_data_size, &new_backward_references, window,
//                                                           const1, const2, const3);
//     BlockSplitFromDecoder new_literals_block_splits;
//     BlockSplitFromDecoder new_insert_copy_length_block_splits;
//     // if (literals_block_splits != NULL) {
//     // std::cout << "num_blocks " << literals_block_splits.num_blocks << "\n";
//     // }
//     // printf("Literals Delete\n");
//     RemoveBlockSplittingPart(&literals_block_splits, start_index, end_index,
//                              &new_literals_block_splits);
//     // printf("Commands Delete\n");
//     RemoveBlockSplittingPart(&insert_copy_length_block_splits, start_index, end_index,
//                              &new_insert_copy_length_block_splits);
//     // printf("removed_data_size=%zu, init_size=%zu\n", removed_data_size, decopressed_size);
//     // printf("Literals Before, %zu, %zu:\n", literals_block_splits.num_blocks, literals_block_splits.num_types);
//     // for (int i = 0; i < literals_block_splits.num_blocks; ++i) {
//     //   printf("%u, ", literals_block_splits.types[i]);
//     // }
//     // printf("\n");
//     // for (int i = 0; i < literals_block_splits.num_blocks; ++i) {
//     //   printf("%u-%u, ", literals_block_splits.positions_begin[i], literals_block_splits.positions_end[i]);
//     // }
//     // printf("\n");
//     // printf("Literals:, %zu, %zu\n", new_literals_block_splits.num_blocks, new_literals_block_splits.num_types);
//     // for (int i = 0; i < new_literals_block_splits.num_blocks; ++i) {
//     //   printf("%u, ", new_literals_block_splits.types[i]);
//     // }
//     // printf("\n");
//     // for (int i = 0; i < new_literals_block_splits.num_blocks; ++i) {
//     //   printf("%u-%u, ", new_literals_block_splits.positions_begin[i], new_literals_block_splits.positions_end[i]);
//     // }
//     // printf("\n");
//     // printf("Commands Before, %zu, %zu:\n", insert_copy_length_block_splits.num_blocks, insert_copy_length_block_splits.num_types);
//     // for (int i = 0; i < insert_copy_length_block_splits.num_blocks; ++i) {
//     //   printf("%u, ", insert_copy_length_block_splits.types[i]);
//     // }
//     // printf("\n");
//     // for (int i = 0; i < insert_copy_length_block_splits.num_blocks; ++i) {
//     //   printf("%u-%u, ", insert_copy_length_block_splits.positions_begin[i], insert_copy_length_block_splits.positions_end[i]);
//     // }
//     // printf("\n");
//     //
//     // printf("Commands, %zu, %zu:\n", new_insert_copy_length_block_splits.num_blocks, new_insert_copy_length_block_splits.num_types);
//     // for (int i = 0; i < new_insert_copy_length_block_splits.num_blocks; ++i) {
//     //   printf("%u, ", new_insert_copy_length_block_splits.types[i]);
//     // }
//     // printf("\n");
//     // for (int i = 0; i < new_insert_copy_length_block_splits.num_blocks; ++i) {
//     //   printf("%u-%u, ", new_insert_copy_length_block_splits.positions_begin[i], new_insert_copy_length_block_splits.positions_end[i]);
//     // }
//     // printf("\n");
//
//     // std::cout << "new_backward_references_size " << new_backward_references_size << "\n";
//     // for (int i = 0; i < new_backward_references_size; ++i) {
//     //   std::cout << new_backward_references[i].position << "\n";//" " << *(*new_backward_references + i).copy_len << " " << *(*new_backward_references + i).distance << "\n";
//     // }
//     // std::cout << "decopressed_size = " << decopressed_size << "end_index - start_index = " << end_index - start_index << "\n";
//     // // size_t removed_data_size = decopressed_size - (end_index - start_index);
//     // std::cout << "removed_data_size " << removed_data_size << "\n";
//     end = clock();
//     float elapsed_time_delete = (float) (end - start) / CLOCKS_PER_SEC;
//     // std::cout << "Deleted\n";
//
//     // Compress
//     window = MinWindowLargerThanFile(removed_data_size, DEFAULT_WINDOW);
//
//     start = clock();
//     // size_t output_size = BrotliCompress(level, window, removed_data, removed_data_size,
//     //                                     output_compressed_data, output_compressed_size,
//     //                                     &new_backward_references, new_backward_references_size,
//     //                                     &new_literals_block_splits, &new_insert_copy_length_block_splits);
//     // BlockSplitFromDecoder* new_literals_block_splits_ = NULL;
//     // BlockSplitFromDecoder* new_insert_copy_length_block_splits_ = NULL;
//     size_t output_size = BrotliCompress(level, window, removed_data, removed_data_size,
//                                         output_compressed_data, output_compressed_size,
//                                         new_backward_references, new_backward_references_size,
//                                         &new_literals_block_splits, &new_insert_copy_length_block_splits);
//
//     end = clock();
//     float elapsed_time_compress = (float) (end - start) / CLOCKS_PER_SEC;
//     // std::cout << "Compressed\n";
//
//     float comp_speed = (float) (removed_data_size) / (elapsed_time_compress * 1024 * 1024);
//     float dec_speed = (float) (decopressed_size) / (elapsed_time_decompress * 1024 * 1024);
//     float del_speed = (float) (decopressed_size) / (elapsed_time_delete * 1024 * 1024);
//
//
//     // check that the result is decompressible
//     size_t decompressed_size_ = removed_data_size * 2;
//     unsigned char* decompressed_data_ = (unsigned char*) malloc(decompressed_size_);
//     size_t total_decompress_size = 0;
//     BackwardReferenceFromDecoder* backward_references_;
//     size_t back_refs_size_;
//     BlockSplitFromDecoder literals_block_splits_;
//     BlockSplitFromDecoder insert_copy_length_block_splits_;
//     total_decompress_size = BrotliDecompress(output_compressed_data, output_compressed_size,
//                                             decompressed_data_, decompressed_size_,
//                                             false,
//                                             &backward_references_, &back_refs_size_,
//                                             &literals_block_splits_, &insert_copy_length_block_splits_);
//
//     assert(total_decompress_size == removed_data_size);
//     assert(memcmp(decompressed_data_, removed_data, removed_data_size)==0);
//
//
//     return DeletionCompressionStatistics(output_size, (float)removed_data_size/(float)output_size, dec_speed, del_speed, comp_speed, elapsed_time_compress + elapsed_time_decompress + elapsed_time_delete);
// }
//
//
// void DeleteFragmentCompression(const unsigned char* input_data, size_t input_size,
//                                 unsigned char* output_compressed_data, size_t output_compressed_size,
//                                 std::string file_name, std::ostream & results,
//                                 int level, int percentage) {
//     results << "\"usual\":{";
//     std::string name = "brotli" + std::to_string(level);
//     // Input is a Brotli 11 compressed file
//     size_t compressed_buffer_size = input_size * 3;
//     unsigned char* compressed_data = (unsigned char*) malloc(compressed_buffer_size);
//     BackwardReferenceFromDecoder* backward_references = NULL;
//     size_t back_refs_size = 0;
//     int window = MinWindowLargerThanFile(input_size, DEFAULT_WINDOW);
//     BlockSplitFromDecoder* literals_block_splits = NULL;
//     BlockSplitFromDecoder* insert_copy_length_block_splits = NULL;
//     size_t compressed_data_size = BrotliCompress(11, window, input_data, input_size,
//                                               compressed_data, compressed_buffer_size,
//                                               backward_references, back_refs_size,
//                                               literals_block_splits,
//                                               insert_copy_length_block_splits);
//     // std::cout << "Compressed\n";
//     // std::cout << (float)input_size / (float)compressed_data_size << "\n";
//     // Usual deletion
//     DeletionCompressionStatistics comp_results = UsualDeletion(compressed_data, compressed_data_size,
//                                                                output_compressed_data, output_compressed_size,
//                                                                level, percentage);
//     results << "\"" << name  << "_compressed_size\":" << std::setprecision(4) << comp_results.compressed_size << ", \"";
//     results << name << "_compression_rate\":" << std::setprecision(4) << comp_results.compression_rate << ", \"";
//     results << name << "_overall_time\":" << std::setprecision(4) << comp_results.overall_time << ", \"";
//     results << name << "_decompression_speed\":" << std::setprecision(4) << comp_results.decompression_speed << ", \"";
//     results << name << "_deletion_speed\":" << std::setprecision(4) << comp_results.deletion_speed << ", \"";
//     results << name << "_compression_speed\":" << std::setprecision(4) << comp_results.compression_speed << "},\n";
//
//     // std::cout << comp_results.compressed_size << " " << comp_results.compression_rate << " " << comp_results.decompression_speed
//     // << " " << comp_results.deletion_speed << " " << comp_results.compression_speed << "\n";
//
//     // Reuse deletion
//     results << "\"reuse\":";
//
//     int const1 = 3;
//     int const2 = 5;
//     int const3 = 3;
//     results << "{";
//     comp_results = ReuseDeletion(compressed_data, compressed_data_size,
//                                  output_compressed_data, output_compressed_size,
//                                  level, percentage, const1, const2, const3);
//     results << "\"" << name  << "_compressed_size\":" << std::setprecision(4) << comp_results.compressed_size << ", \"";
//     results << name << "_compression_rate\":" << std::setprecision(4) << comp_results.compression_rate << ", \"";
//     results << name << "_overall_time\":" << std::setprecision(4) << comp_results.overall_time << ", \"";
//     results << name << "_decompression_speed\":" << std::setprecision(4) << comp_results.decompression_speed << ", \"";
//     results << name << "_deletion_speed\":" << std::setprecision(4) << comp_results.deletion_speed << ", \"";
//     results << name << "_compression_speed\":" << std::setprecision(4) << comp_results.compression_speed << "}\n";
// }


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
    std::cout << "exit(1)\n";
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

  // check that the result is decompressible
  size_t decompressed_size_ = input_size * 2;
  unsigned char* decompressed_data_ = (unsigned char*) malloc(decompressed_size_);
  size_t total_decompress_size = 0;
  BackwardReferenceFromDecoder* backward_references_;
  size_t back_refs_size_;
  BlockSplitFromDecoder literals_block_splits_;
  BlockSplitFromDecoder insert_copy_length_block_splits_;
  std::cout << "BrotliDecompress before\n";
  total_decompress_size = BrotliDecompress(compressed_data, compressed_size,
                                          decompressed_data_, decompressed_size_,
                                          true,
                                          &backward_references_, &back_refs_size_,
                                          &literals_block_splits_, &insert_copy_length_block_splits_);
  std::cout << "BrotliDecompress after\n";
  assert(total_decompress_size == input_size);
  assert(memcmp(decompressed_data_, input_data, input_size)==0);
  std::cout << "Checked\n";


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
  std::cout << "Output size : " << output_buffer_size <<
                ", elapsed_time = " << elapsed_time << "\n";
  return 0;
}
