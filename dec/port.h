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

#ifndef __has_attribute
#define __has_attribute(x) 0
#endif

#ifndef __has_feature
#define __has_feature(x) 0
#endif

#define BROTLI_ASAN_BUILD __has_feature(address_sanitizer)

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

/* IS_CONSTANT macros returns true for compile-time constant expressions. */
#if (__GNUC__ > 3) || (__GNUC__ == 3 && __GNUC_MINOR__ > 0) || \
    (defined(__llvm__) && __has_builtin(__builtin_constant_p))
#define IS_CONSTANT(x) __builtin_constant_p(x)
#else
#define IS_CONSTANT(x) 0
#endif

#if (__GNUC__ > 3) || (__GNUC__ == 3 && __GNUC_MINOR__ > 0) || \
    (defined(__llvm__) && __has_attribute(always_inline))
#define ATTRIBUTE_ALWAYS_INLINE __attribute__ ((always_inline))
#else
#define ATTRIBUTE_ALWAYS_INLINE
#endif

#ifndef _MSC_VER
#if defined(__cplusplus) || !defined(__STRICT_ANSI__) \
    || __STDC_VERSION__ >= 199901L
#define BROTLI_INLINE inline ATTRIBUTE_ALWAYS_INLINE
#else
#define BROTLI_INLINE
#endif
#else  /* _MSC_VER */
#define BROTLI_INLINE __forceinline
#endif  /* _MSC_VER */

#ifdef BROTLI_DECODE_DEBUG
#define BROTLI_DCHECK(x) assert(x)
#else
#define BROTLI_DCHECK(x)
#endif

#if (defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__) || \
     defined(__PPC64__))
#define BROTLI_64_BITS 1
#else
#define BROTLI_64_BITS 0
#endif

#if (defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__))
#define BROTLI_LITTLE_ENDIAN 1
#elif defined(_WIN32)
/* Win32 can currently always be assumed to be little endian */
#define BROTLI_LITTLE_ENDIAN 1
#else
#define BROTLI_LITTLE_ENDIAN 0
#endif

#if (BROTLI_64_BITS && BROTLI_LITTLE_ENDIAN)
#define BROTLI_64_BITS_LITTLE_ENDIAN 1
#else
#define BROTLI_64_BITS_LITTLE_ENDIAN 0
#endif

#if (__GNUC__ > 3) || (__GNUC__ == 3 && __GNUC_MINOR__ >= 1) || \
    (defined(__llvm__) && __has_attribute(noinline))
#define BROTLI_NOINLINE __attribute__ ((noinline))
#else
#define BROTLI_NOINLINE
#endif

#if BROTLI_ASAN_BUILD
#define BROTLI_NO_ASAN __attribute__((no_sanitize("address"))) BROTLI_NOINLINE
#else
#define BROTLI_NO_ASAN
#endif

#define BROTLI_REPEAT(N, X) { \
  if ((N & 1) != 0) {X;} \
  if ((N & 2) != 0) {X; X;} \
  if ((N & 4) != 0) {X; X; X; X;} \
}

#if (__GNUC__ > 2) || defined(__llvm__)
#if (defined(__ARM_ARCH) && (__ARM_ARCH >= 7))
static BROTLI_INLINE unsigned BrotliRBit(unsigned input) {
  unsigned output;
  __asm__("rbit %0, %1\n" : "=r"(output) : "r"(input));
  return output;
}
#define BROTLI_RBIT(x) BrotliRBit(x)
#endif  /* armv7 */
#endif  /* gcc || clang */

#endif  /* BROTLI_DEC_PORT_H_ */
