/* Copyright 2020 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

#include "../compress_similar_files/compress_similar_files.c"
#include "helper.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>


bool TestFirstLastPositions(size_t input_size,
                            const BlockSplitFromDecoder* block_splits) {
 if (block_splits->num_blocks > 0 &&
     block_splits->positions_begin[0] != 0) {
       return false;
 }
 if (block_splits->num_blocks > 0 &&
     block_splits->positions_end[block_splits->num_blocks - 1]
       != input_size) {
       return false;
 }
 return true;
}

bool TestIncreasingPositions(const BlockSplitFromDecoder* block_splits) {
 for (int i = 0; i < block_splits->num_blocks; ++i) {
   if (block_splits->positions_begin[i] >=
       block_splits->positions_end[i]) {
     return false;
   }
 }
 return true;
}

bool TestAdjacentTypes(const BlockSplitFromDecoder* block_splits) {
 for (int i = 1; i < block_splits->num_blocks; ++i) {
   if (block_splits->types[i] ==
       block_splits->types[i - 1]) {
     return false;
   }
 }
 return true;
}

bool TestNumTypes(const BlockSplitFromDecoder* block_splits) {
 if (CountUniqueElements(block_splits->types, block_splits->num_blocks)
            != block_splits->num_types) {
   return false;
 }
 return true;
}
