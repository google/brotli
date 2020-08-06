/* Copyright 2020 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/


#include "../compress_similar_files/compress_similar_files.c"
#include "helper.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>


bool TestEqualSubstrings(BackwardReferenceFromDecoder* backward_references,
                         size_t backward_references_size,
                         unsigned char* input_data, size_t input_size) {
  /* Check that strings are equal */
  for (int i = 0; i < backward_references_size; ++i) {
    if (backward_references[i].distance <= backward_references[i].max_distance &&
        backward_references[i].distance <= backward_references[i].position) {
      for (int j = 0; j < backward_references[i].copy_len; ++j) {
        if (backward_references[i].position + j >= input_size) {
          return false;
        }
        if (backward_references[i].position + j - backward_references[i].distance < 0) {
          return false;
        }
        if (input_data[backward_references[i].position + j] != input_data[backward_references[i].position + j - backward_references[i].distance]) {
          return false;
        }
      }
    }
  }
  return true;
}

bool TestSortedPositions(BackwardReferenceFromDecoder* backward_references,
                         size_t backward_references_size) {
 for (int i = 1; i < backward_references_size; ++i) {
   if (backward_references[i - 1].position >= backward_references[i].position) {
     return false;
   }
 }
 return true;
}

bool TestFirstLastPosition(BackwardReferenceFromDecoder* backward_references,
                         size_t backward_references_size,
                         unsigned char* input_data, size_t input_size) {
  /* Check that the first position is > 0 */
  if (backward_references_size > 0 &&
      backward_references[0].position < 0) {
    printf("backward_references[0].position=%d\n", backward_references[0].position);
    return false;
  }
  /* Check that the last position is < input_size */
  if (backward_references_size > 0 &&
      backward_references[backward_references_size - 1].position >= input_size) {
    printf("backward_references[back_refs_size - 1].position=%d, %zu\n", backward_references[backward_references_size - 1].position, input_size);
    return false;
  }
  return true;
}
