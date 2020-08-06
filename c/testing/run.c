/* Copyright 2020 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/


#include "backward_reference_reuse.h"
#include "backward_references_collection.h"
#include "block_splits_collection.h"
#include "block_splits_mapping.h"
#include "metablock_block_splits.h"

void RunTest(const char* name, bool result) {
  if (result) {
    printf("%s %s\n", name, "passed.");
  } else {
    printf("%s %s\n", name, "failed.");
  }
}

char* Concat(const char *s1, const char *s2, const char *s3){
    char *result = malloc(strlen(s1) + strlen(s2) + strlen(s3) + 1);
    strcpy(result, s1);
    strcat(result, s2);
    strcat(result, s3);
    return result;
}

bool TestEqualTexts(unsigned char* input_data, size_t input_size,
                    unsigned char* output_data, size_t output_size) {
  if (input_size != output_size) {
    return false;
  }
  if (memcmp(input_data, output_data, input_size) != 0) {
    return false;
  }
  return true;
}


int main() {
  char* files[2] = {"files/mr", "files/dickens"};
  /* Check backward reference collection */
  char* part_name = "Backward reference collection for ";
  for (int i = 0; i < 2; ++i) {
    FILE* infile = OpenFile(files[i], "rb");

    if (infile == NULL) {
      exit(1);
    }
    unsigned char* input_data = NULL;
    size_t input_size = 0;
    ReadData(infile, &input_data, &input_size);
    fclose(infile);
    size_t output_buffer_size = input_size * 2;
    unsigned char* output_data = (unsigned char*) malloc(output_buffer_size);

    BackwardReferenceFromDecoder* backward_references;
    size_t back_refs_size;
    GetBackwardReferences(input_data, input_size, 9,
                          &backward_references, &back_refs_size);

    RunTest(Concat(part_name, files[i], ": TestFirstLastPosition"),
        TestFirstLastPosition(backward_references, back_refs_size,
                              input_data, input_size));
    RunTest(Concat(part_name, files[i], ": TestEqualSubstrings"),
      TestEqualSubstrings(backward_references, back_refs_size,
                          input_data, input_size));
    RunTest(Concat(part_name, files[i], ": TestSortedPositions"),
        TestSortedPositions(backward_references, back_refs_size));
  }

  /* Check block splits collection */
  for (int i = 0; i < 2; ++i) {
    FILE* infile = OpenFile(files[i], "rb");

    if (infile == NULL) {
      exit(1);
    }
    unsigned char* input_data = NULL;
    size_t input_size = 0;
    ReadData(infile, &input_data, &input_size);
    fclose(infile);
    size_t output_buffer_size = input_size * 2;
    unsigned char* output_data = (unsigned char*) malloc(output_buffer_size);

    BlockSplitFromDecoder literals_block_splits;
    BlockSplitFromDecoder insert_copy_length_block_splits;

    GetBlockSplits(input_data, input_size, 9,
                   &literals_block_splits,
                   &insert_copy_length_block_splits);
    part_name = "Literals block splits collection for ";
    RunTest(Concat(part_name, files[i], ": TestFirstLastPositions"),
        TestFirstLastPositions(input_size, &literals_block_splits));
    RunTest(Concat(part_name, files[i], ": TestIncreasingPositions"),
        TestIncreasingPositions(&literals_block_splits));
    RunTest(Concat(part_name, files[i], ": TestAdjacentTypes"),
        TestAdjacentTypes(&literals_block_splits));
    RunTest(Concat(part_name, files[i], ": TestNumTypes"),
        TestNumTypes(&literals_block_splits));
    part_name = "Commands block splits collection for ";
    RunTest(Concat(part_name, files[i], ": TestFirstLastPositions"),
        TestFirstLastPositions(input_size, &insert_copy_length_block_splits));
    RunTest(Concat(part_name, files[i], ": TestIncreasingPositions"),
        TestIncreasingPositions(&insert_copy_length_block_splits));
    RunTest(Concat(part_name, files[i], ": TestAdjacentTypes"),
        TestAdjacentTypes(&insert_copy_length_block_splits));
    RunTest(Concat(part_name, files[i], ": TestNumTypes"),
        TestNumTypes(&insert_copy_length_block_splits));
  }

  /* Check backward reference adjustments */
  part_name = "Backward reference adjustment for ";
  for (int i = 0; i < 2; ++i) {
    FILE* infile = OpenFile(files[i], "rb");
    if (infile == NULL) {
      exit(1);
    }
    unsigned char* input_data = NULL;
    size_t input_size = 0;
    ReadData(infile, &input_data, &input_size);
    fclose(infile);
    size_t output_buffer_size = input_size * 2;
    unsigned char* output_data = (unsigned char*) malloc(output_buffer_size);

    BackwardReferenceFromDecoder* new_backward_references = NULL;
    size_t new_backward_references_size = 0;
    unsigned char* removed_data = (unsigned char*) malloc(input_size);
    size_t removed_data_size = 0;
    GetNewBackwardReferences(input_data, input_size, 9, 100, 500,
                            &new_backward_references, &new_backward_references_size,
                            &removed_data, &removed_data_size);
    RunTest(Concat(part_name, files[i], ": TestFirstLastPosition"),
      TestFirstLastPosition(new_backward_references,
                new_backward_references_size, removed_data, removed_data_size));
    RunTest(Concat(part_name, files[i], ": TestEqualSubstrings"),
      TestEqualSubstrings(new_backward_references, new_backward_references_size,
                          removed_data, removed_data_size));
    RunTest(Concat(part_name, files[i], ": TestSortedPositions"),
      TestSortedPositions(new_backward_references, new_backward_references_size));
  }

  /* Check block splits adjustments */
  for (int i = 0; i < 2; ++i) {
    FILE* infile = OpenFile(files[i], "rb");

    if (infile == NULL) {
      exit(1);
    }
    unsigned char* input_data = NULL;
    size_t input_size = 0;
    ReadData(infile, &input_data, &input_size);
    fclose(infile);
    BlockSplitFromDecoder new_literals_block_splits;
    BlockSplitFromDecoder new_commands_block_splits;
    unsigned char* removed_data = NULL;
    size_t removed_data_size = 0;
    GetNewBlockSplits(input_data, input_size, 9, 100, 500,
                      &new_literals_block_splits,
                      &new_commands_block_splits,
                      &removed_data, &removed_data_size);
    part_name = "Literals block splits adjustment for ";
    RunTest(Concat(part_name, files[i], ": TestFirstLastPositions"),
      TestFirstLastPositions(removed_data_size, &new_literals_block_splits));
    RunTest(Concat(part_name, files[i], ": TestIncreasingPositions"),
      TestIncreasingPositions(&new_literals_block_splits));
    RunTest(Concat(part_name, files[i], ": TestAdjacentTypes"),
      TestAdjacentTypes(&new_literals_block_splits));
    RunTest(Concat(part_name, files[i], ": TestNumTypes"),
      TestNumTypes(&new_literals_block_splits));
    part_name = "Commands block splits adjustment for ";
    RunTest(Concat(part_name, files[i], ": TestFirstLastPositions"),
      TestFirstLastPositions(removed_data_size, &new_commands_block_splits));
    RunTest(Concat(part_name, files[i], ": TestIncreasingPositions"),
      TestIncreasingPositions(&new_commands_block_splits));
    RunTest(Concat(part_name, files[i], ": TestAdjacentTypes"),
      TestAdjacentTypes(&new_commands_block_splits));
    RunTest(Concat(part_name, files[i], ": TestNumTypes"),
      TestNumTypes(&new_commands_block_splits));
  }

  /* Check block splits mapping */
  RunTest("Block splits mapping: TestSimple", TestSimple());
  RunTest("Block splits mapping: TestSkipBlocksAndMergeSaveTypes",
          TestSkipBlocksAndMergeSaveTypes());
  RunTest("Block splits mapping: TestOneBlockType", TestOneBlockType());

  /* Check block histograms */
  RunTest("Block splits histograms: TestBlocksHistograms",
          TestBlocksHistograms());

  /* Check backward reference reuse */
  part_name = "Backward reference reuse for ";
  for (int i = 0; i < 2; ++i) {
    FILE* infile = OpenFile(files[i], "rb");
    if (infile == NULL) {
      exit(1);
    }
    unsigned char* input_data = NULL;
    size_t input_size = 0;
    ReadData(infile, &input_data, &input_size);
    fclose(infile);
    RunTest(Concat(part_name, files[i], ": TestReusageRateSameFile"),
      TestReusageRateSameFile(input_data, input_size, 9));
    RunTest(Concat(part_name, files[i], ": TestReusageRateNewFile"),
      TestReusageRateNewFile(input_data, input_size, 9));
  }

  /* Check that overall results are decompressible */
  for (int i = 0; i < 2; ++i) {
    FILE* infile = OpenFile(files[i], "rb");

    if (infile == NULL) {
      exit(1);
    }
    unsigned char* input_data = NULL;
    size_t input_size = 0;
    ReadData(infile, &input_data, &input_size);
    fclose(infile);

    BackwardReferenceFromDecoder* new_backward_references = NULL;
    size_t new_backward_references_size = 0;
    unsigned char* removed_data = (unsigned char*) malloc(input_size);
    size_t removed_data_size = 0;
    GetNewBackwardReferences(input_data, input_size, 9, 100, 500,
                            &new_backward_references, &new_backward_references_size,
                            &removed_data, &removed_data_size);
    BlockSplitFromDecoder new_literals_block_splits;
    BlockSplitFromDecoder new_commands_block_splits;
    GetNewBlockSplits(input_data, input_size, 9, 100, 500,
                      &new_literals_block_splits,
                      &new_commands_block_splits,
                      &removed_data, &removed_data_size);
    size_t decompressed_size = removed_data_size * 2;
    unsigned char* decompressed_data = (unsigned char*) malloc(decompressed_size);
    BrotliCompressDecompressReusage(removed_data, removed_data_size, 9,
                                    new_backward_references,
                                    new_backward_references_size,
                                    &new_literals_block_splits,
                                    &new_commands_block_splits,
                                    &decompressed_data, &decompressed_size);
    RunTest("TestCheckDecompressible",
      TestEqualTexts(removed_data, removed_data_size, decompressed_data,
                     decompressed_size));
  }

  return 0;
}
