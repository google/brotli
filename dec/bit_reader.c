/* Copyright 2013 Google Inc. All Rights Reserved.

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

/* Bit reading helpers */

#include <stdlib.h>

#include "./bit_reader.h"
#include "./port.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

void BrotliInitBitReader(BrotliBitReader* const br) {
  br->val_ = 0;
  br->bit_pos_ = sizeof(br->val_) << 3;
}

int BrotliWarmupBitReader(BrotliBitReader* const br) {
  size_t aligned_read_mask = (sizeof(br->val_) >> 1) - 1;
  /* Fixing alignment after unaligned BrotliFillWindow would result accumulator
     overflow. If unalignment is caused by BrotliSafeReadBits, then there is
     enough space in accumulator to fix aligment. */
  if (!BROTLI_ALIGNED_READ) {
    aligned_read_mask = 0;
  }
  if (BrotliGetAvailableBits(br) == 0) {
    if (!BrotliPullByte(br)) {
      return 0;
    }
  }

  while ((((size_t)br->next_in) & aligned_read_mask) != 0) {
    if (!BrotliPullByte(br)) {
      /* If we consumed all the input, we don't care about the alignment. */
      return 1;
    }
  }
  return 1;
}

#if defined(__cplusplus) || defined(c_plusplus)
}    /* extern "C" */
#endif
