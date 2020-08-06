/* Copyright 2020 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/


#include "backward_reference_reuse.h"
#include "backward_references_collection.h"
#include "block_splits_collection.h"
#include "block_splits_mapping.h"
#include "metablock_block_splits.h"


int main() {
  char* files[2] = {"files/mr", "files/dickens"};

  /* Check backward reference collection */
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
    if (TestFirstLastPosition(backward_references, back_refs_size,
                           input_data, input_size)) {
      printf("Backward reference collection for %s: TestFirstLastPosition passed\n", files[i]);
    } else {
      printf("Backward reference collection for %s: TestFirstLastPosition failed\n", files[i]);
    }
    if (TestEqualSubstrings(backward_references, back_refs_size,
                        input_data, input_size)) {
      printf("Backward reference collection for %s: TestEqualSubstrings passed\n", files[i]);
    } else {
      printf("Backward reference collection for %s: TestEqualSubstrings failed\n", files[i]);
    }
    if (TestSortedPositions(backward_references, back_refs_size)) {
      printf("Backward reference collection for %s: TestSortedPositions passed\n", files[i]);
    } else {
      printf("Backward reference collection for %s: TestSortedPositions failed\n", files[i]);
    }
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
    if (TestFirstLastPositions(input_size, &literals_block_splits)) {
      printf("Literals block splits collection for %s: TestFirstLastPositions passed\n", files[i]);
    } else {
      printf("Literals block splits collection for %s: TestFirstLastPositions failed\n", files[i]);
    }
    if (TestFirstLastPositions(input_size, &insert_copy_length_block_splits)) {
      printf("Commands block splits collection for %s: TestFirstLastPositions passed\n", files[i]);
    } else {
      printf("Commands block splits collection for %s: TestFirstLastPositions failed\n", files[i]);
    }
    if (TestIncreasingPositions(&literals_block_splits)) {
      printf("Literals block splits collection for %s: TestIncreasingPositions passed\n", files[i]);
    } else {
      printf("Literals block splits collection for %s: TestIncreasingPositions failed\n", files[i]);
    }
    if (TestIncreasingPositions(&insert_copy_length_block_splits)) {
      printf("Commands block splits collection for %s: TestIncreasingPositions passed\n", files[i]);
    } else {
      printf("Commands block splits collection for %s: TestIncreasingPositions failed\n", files[i]);
    }
    if (TestAdjacentTypes(&literals_block_splits)) {
      printf("Literals block splits collection for %s: TestAdjacentTypes passed\n", files[i]);
    } else {
      printf("Literals block splits collection for %s: TestAdjacentTypes failed\n", files[i]);
    }
    if (TestAdjacentTypes(&insert_copy_length_block_splits)) {
      printf("Commands block splits collection for %s: TestAdjacentTypes passed\n", files[i]);
    } else {
      printf("Commands block splits collection for %s: TestAdjacentTypes failed\n", files[i]);
    }
    if (TestNumTypes(&literals_block_splits)) {
      printf("Literals block splits collection for %s: TestNumTypes passed\n", files[i]);
    } else {
      printf("Literals block splits collection for %s: TestNumTypes failed\n", files[i]);
    }
    if (TestNumTypes(&insert_copy_length_block_splits)) {
      printf("Commands block splits collection for %s: TestNumTypes passed\n", files[i]);
    } else {
      printf("Commands block splits collection for %s: TestNumTypes failed\n", files[i]);
    }
  }

  /* Check backward reference adjustments */
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
    if (TestFirstLastPosition(new_backward_references, new_backward_references_size,
                              removed_data, removed_data_size)) {
      printf("Backward reference adjustment for %s: TestFirstLastPosition passed\n", files[i]);
    } else {
      printf("Backward reference adjustment for %s: TestFirstLastPosition failed\n", files[i]);
    }
    if (TestEqualSubstrings(new_backward_references, new_backward_references_size,
                            removed_data, removed_data_size)) {
      printf("Backward reference adjustment for %s: TestEqualSubstrings passed\n", files[i]);
    } else {
      printf("Backward reference adjustment for %s: TestEqualSubstrings failed\n", files[i]);
    }
    if (TestSortedPositions(new_backward_references, new_backward_references_size)) {
      printf("Backward reference adjustment for %s: TestSortedPositions passed\n", files[i]);
    } else {
      printf("Backward reference adjustment for %s: TestSortedPositions failed\n", files[i]);
    }
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
    BlockSplitFromDecoder new_insert_copy_length_block_splits;
    unsigned char* removed_data = NULL;
    size_t removed_data_size = 0;
    GetNewBlockSplits(input_data, input_size, 9, 100, 500,
                      &new_literals_block_splits,
                      &new_insert_copy_length_block_splits,
                      &removed_data, &removed_data_size);
    if (TestFirstLastPositions(removed_data_size, &new_literals_block_splits)) {
      printf("Literals block splits adjustment for %s: TestFirstLastPositions passed\n", files[i]);
    } else {
      printf("Literals block splits adjustment for %s: TestFirstLastPositions failed\n", files[i]);
    }
    if (TestFirstLastPositions(removed_data_size, &new_insert_copy_length_block_splits)) {
      printf("Commands block splits adjustment for %s: TestFirstLastPositions passed\n", files[i]);
    } else {
      printf("Commands block splits adjustment for %s: TestFirstLastPositions failed\n", files[i]);
    }
    if (TestIncreasingPositions(&new_literals_block_splits)) {
      printf("Literals block splits adjustment for %s: TestIncreasingPositions passed\n", files[i]);
    } else {
      printf("Literals block splits adjustment for %s: TestIncreasingPositions failed\n", files[i]);
    }
    if (TestIncreasingPositions(&new_insert_copy_length_block_splits)) {
      printf("Commands block splits adjustment for %s: TestIncreasingPositions passed\n", files[i]);
    } else {
      printf("Commands block splits adjustment for %s: TestIncreasingPositions failed\n", files[i]);
    }
    if (TestAdjacentTypes(&new_literals_block_splits)) {
      printf("Literals block splits adjustment for %s: TestAdjacentTypes passed\n", files[i]);
    } else {
      printf("Literals block splits adjustment for %s: TestAdjacentTypes failed\n", files[i]);
    }
    if (TestAdjacentTypes(&new_literals_block_splits)) {
      printf("Commands block splits adjustment for %s: TestAdjacentTypes passed\n", files[i]);
    } else {
      printf("Commands block splits adjustment for %s: TestAdjacentTypes failed\n", files[i]);
    }
    if (TestNumTypes(&new_literals_block_splits)) {
      printf("Literals block splits adjustment for %s: TestNumTypes passed\n", files[i]);
    } else {
      printf("Literals block splits adjustment for %s: TestNumTypes failed\n", files[i]);
    }
    if (TestNumTypes(&new_insert_copy_length_block_splits)) {
      printf("Commands block splits adjustment for %s: TestNumTypes passed\n", files[i]);
    } else {
      printf("Commands block splits adjustment for %s: TestNumTypes failed\n", files[i]);
    }
  }

  /* Check block splits mapping */
  if (TestSimple()) {
    printf("Block splits mapping: TestSimple passed\n");
  } else {
    printf("Block splits mapping: TestSimple failed\n");
  }
  if (TestSkipBlocksAndMergeSaveTypes()) {
    printf("Block splits mapping: TestSkipBlocksAndMergeSaveTypes passed\n");
  } else {
    printf("Block splits mapping: TestSkipBlocksAndMergeSaveTypes failed\n");
  }
  if (TestOneBlockType()) {
    printf("Block splits mapping: TestOneBlockType passed\n");
  } else {
    printf("Block splits mapping: TestOneBlockType failed\n");
  }

  /* Check block histograms */
  if (TestBlocksHistograms()) {
    printf("Passed\n");
  } else {
    printf("Not\n");
  }

  /* Check backward reference reuse */
  for (int i = 0; i < 2; ++i) {
    FILE* infile = OpenFile(files[i], "rb");

    if (infile == NULL) {
      exit(1);
    }
    unsigned char* input_data = NULL;
    size_t input_size = 0;
    ReadData(infile, &input_data, &input_size);
    fclose(infile);
    if (TestReusageRateSameFile(input_data, input_size, 9)) {
      printf("Backward reference reuse for %s: TestReusageRateSameFile passed\n", files[i]);
    } else {
      printf("Backward reference reuse for %s: TestReusageRateSameFile failed\n", files[i]);
    }
    if (TestReusageRateNewFile(input_data, input_size, 9)) {
      printf("Backward reference reuse for %s: TestReusageRateNewFile passed\n", files[i]);
    } else {
      printf("Backward reference reuse for %s: TestReusageRateNewFile failed\n", files[i]);
    }
  }

  return 0;
}
