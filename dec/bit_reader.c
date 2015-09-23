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

void BrotliInitBitReader(BrotliBitReader* const br, BrotliInput input) {
  BROTLI_DCHECK(br != NULL);

  br->input_ = input;
  br->val_ = 0;
  br->bit_pos_ = sizeof(br->val_) << 3;
  br->avail_in = 0;
  br->eos_ = 0;
  br->next_in = br->buf_;
}

int BrotliWarmupBitReader(BrotliBitReader* const br) {
  if (br->bit_pos_ == (sizeof(br->val_) << 3)) {
    if (!br->avail_in) {
      return 0;
    }
    br->bit_pos_ -= 8;
    br->val_ = *br->next_in;
    br->val_ <<= br->bit_pos_;
    br->next_in++;
    br->avail_in--;
  }
  return 1;
}

#if defined(__cplusplus) || defined(c_plusplus)
}    /* extern "C" */
#endif
