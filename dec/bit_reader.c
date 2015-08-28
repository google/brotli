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
  br->bit_pos_ = 0;
  br->avail_in = 0;
  br->eos_ = 0;
  br->next_in = br->buf_;
}


void BrotliWarmupBitReader(BrotliBitReader* const br) {
  size_t i;
  br->val_ = 0;
  for (i = 0; i < sizeof(br->val_); ++i) {
#if (BROTLI_64_BITS_LITTLE_ENDIAN)
    br->val_ |= ((uint64_t)*br->next_in) << (8 * i);
#else
    br->val_ |= ((uint32_t)*br->next_in) << (8 * i);
#endif
    ++br->next_in;
    --br->avail_in;
  }
}

#if defined(__cplusplus) || defined(c_plusplus)
}    /* extern "C" */
#endif
