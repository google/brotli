/* Copyright 2015 Google Inc. All Rights Reserved.

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

/* Macros for branch prediction. */

#ifndef BROTLI_DEC_PORT_H_
#define BROTLI_DEC_PORT_H_

#include<assert.h>

/* Compatibility with non-clang compilers. */
#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

/* Define "PREDICT_TRUE" and "PREDICT_FALSE" macros for capable compilers.

To apply compiler hint, enclose the branching condition into macros, like this:

  if (PREDICT_TRUE(zero == 0)) {
    // main execution path
  } else {
    // compiler should place this code outside of main execution path
  }

OR:

  if (PREDICT_FALSE(something_rare_or_unexpected_happens)) {
    // compiler should place this code outside of main execution path
  }

*/
#if (__GNUC__ > 2) || (__GNUC__ == 2 && __GNUC_MINOR__ > 95) || \
    (defined(__llvm__) && __has_builtin(__builtin_expect))
#define PREDICT_TRUE(x) (__builtin_expect(!!(x), 1))
#define PREDICT_FALSE(x) (__builtin_expect(x, 0))
#else
#define PREDICT_FALSE(x) (x)
#define PREDICT_TRUE(x) (x)
#endif

#ifdef BROTLI_DECODE_DEBUG
#define BROTLI_DCHECK(x) assert(x)
#else
#define BROTLI_DCHECK(x)
#endif

#endif  /* BROTLI_DEC_PORT_H_ */
