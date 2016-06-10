/* Copyright 2015 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Macros for compiler / platform specific features and build options.

   Build options are:
    * BROTLI_BUILD_32_BIT disables 64-bit optimizations
    * BROTLI_BUILD_64_BIT forces to use 64-bit optimizations
    * BROTLI_BUILD_BIG_ENDIAN forces to use big-endian optimizations
    * BROTLI_BUILD_ENDIAN_NEUTRAL disables endian-aware optimizations
    * BROTLI_BUILD_LITTLE_ENDIAN forces to use little-endian optimizations
    * BROTLI_BUILD_MODERN_COMPILER forces to use modern compilers built-ins,
      features and attributes
    * BROTLI_BUILD_PORTABLE disables dangerous optimizations, like unaligned
      read and overlapping memcpy; this reduces decompression speed by 5%
    * BROTLI_DEBUG dumps file name and line number when decoder detects stream
      or memory error
    * BROTLI_ENABLE_LOG enables asserts and dumps various state information
 */

#ifndef BROTLI_DEC_PORT_H_
#define BROTLI_DEC_PORT_H_

#if defined(BROTLI_ENABLE_LOG) || defined(BROTLI_DEBUG)
#include <assert.h>
#include <stdio.h>
#endif

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

#if defined(__arm__) || defined(__thumb__) || \
    defined(_M_ARM) || defined(_M_ARMT)
#define BROTLI_TARGET_ARM
#if (defined(__ARM_ARCH) && (__ARM_ARCH >= 7)) || \
    (defined(M_ARM) && (M_ARM >= 7))
#define BROTLI_TARGET_ARMV7
#endif  /* ARMv7 */
#if defined(__aarch64__)
#define BROTLI_TARGET_ARMV8
#endif  /* ARMv8 */
#endif  /* ARM */

#if defined(__i386) || defined(_M_IX86)
#define BROTLI_TARGET_X86
#endif

#if defined(__x86_64__) || defined(_M_X64)
#define BROTLI_TARGET_X64
#endif

#if defined(__PPC64__)
#define BROTLI_TARGET_POWERPC64
#endif

#if defined(__GNUC__) && defined(__GNUC_MINOR__)
#define BROTLI_GCC_VERSION (__GNUC__ * 100 + __GNUC_MINOR__)
#else
#define BROTLI_GCC_VERSION 0
#endif

#if defined(__ICC)
#define BROTLI_ICC_VERSION __ICC
#else
#define BROTLI_ICC_VERSION 0
#endif

#if defined(BROTLI_BUILD_MODERN_COMPILER)
#define BROTLI_MODERN_COMPILER 1
#elif (BROTLI_GCC_VERSION > 300) || (BROTLI_ICC_VERSION >= 1600)
#define BROTLI_MODERN_COMPILER 1
#else
#define BROTLI_MODERN_COMPILER 0
#endif

#ifdef BROTLI_BUILD_PORTABLE
#define BROTLI_ALIGNED_READ (!!1)
#elif defined(BROTLI_TARGET_X86) || defined(BROTLI_TARGET_X64) || \
     defined(BROTLI_TARGET_ARMV7) || defined(BROTLI_TARGET_ARMV8)
/* Allow unaligned read only for whitelisted CPUs. */
#define BROTLI_ALIGNED_READ (!!0)
#else
#define BROTLI_ALIGNED_READ (!!1)
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
#if BROTLI_MODERN_COMPILER || __has_builtin(__builtin_expect)
#define PREDICT_TRUE(x) (__builtin_expect(!!(x), 1))
#define PREDICT_FALSE(x) (__builtin_expect(x, 0))
#else
#define PREDICT_FALSE(x) (x)
#define PREDICT_TRUE(x) (x)
#endif

/* IS_CONSTANT macros returns true for compile-time constant expressions. */
#if BROTLI_MODERN_COMPILER || __has_builtin(__builtin_constant_p)
#define IS_CONSTANT(x) (!!__builtin_constant_p(x))
#else
#define IS_CONSTANT(x) (!!0)
#endif

#if BROTLI_MODERN_COMPILER || __has_attribute(always_inline)
#define ATTRIBUTE_ALWAYS_INLINE __attribute__ ((always_inline))
#else
#define ATTRIBUTE_ALWAYS_INLINE
#endif

#if defined(_WIN32) || defined(__CYGWIN__)
#define ATTRIBUTE_VISIBILITY_HIDDEN
#elif BROTLI_MODERN_COMPILER || __has_attribute(visibility)
#define ATTRIBUTE_VISIBILITY_HIDDEN __attribute__ ((visibility ("hidden")))
#else
#define ATTRIBUTE_VISIBILITY_HIDDEN
#endif

#ifndef BROTLI_INTERNAL
#define BROTLI_INTERNAL ATTRIBUTE_VISIBILITY_HIDDEN
#endif

#ifndef _MSC_VER
#if defined(__cplusplus) || !defined(__STRICT_ANSI__) || \
    __STDC_VERSION__ >= 199901L
#define BROTLI_INLINE inline ATTRIBUTE_ALWAYS_INLINE
#else
#define BROTLI_INLINE
#endif
#else  /* _MSC_VER */
#define BROTLI_INLINE __forceinline
#endif  /* _MSC_VER */

#ifdef BROTLI_ENABLE_LOG
#define BROTLI_DCHECK(x) assert(x)
#define BROTLI_LOG(x) printf x
#else
#define BROTLI_DCHECK(x)
#define BROTLI_LOG(x)
#endif

#if defined(BROTLI_DEBUG) || defined(BROTLI_ENABLE_LOG)
static inline void BrotliDump(const char* f, int l, const char* fn) {
  fprintf(stderr, "%s:%d (%s)\n", f, l, fn);
  fflush(stderr);
}
#define BROTLI_DUMP() BrotliDump(__FILE__, __LINE__, __FUNCTION__)
#else
#define BROTLI_DUMP() (void)(0)
#endif

#if defined(BROTLI_BUILD_64_BIT)
#define BROTLI_64_BITS 1
#elif defined(BROTLI_BUILD_32_BIT)
#define BROTLI_64_BITS 0
#elif defined(BROTLI_TARGET_X64) || defined(BROTLI_TARGET_ARMV8) || \
    defined(BROTLI_TARGET_POWERPC64)
#define BROTLI_64_BITS 1
#else
#define BROTLI_64_BITS 0
#endif

#if defined(BROTLI_BUILD_BIG_ENDIAN)
#define BROTLI_LITTLE_ENDIAN 0
#define BROTLI_BIG_ENDIAN 1
#elif defined(BROTLI_BUILD_LITTLE_ENDIAN)
#define BROTLI_LITTLE_ENDIAN 1
#define BROTLI_BIG_ENDIAN 0
#elif defined(BROTLI_BUILD_ENDIAN_NEUTRAL)
#define BROTLI_LITTLE_ENDIAN 0
#define BROTLI_BIG_ENDIAN 0
#elif defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#define BROTLI_LITTLE_ENDIAN 1
#define BROTLI_BIG_ENDIAN 0
#elif defined(_WIN32)
/* Win32 can currently always be assumed to be little endian */
#define BROTLI_LITTLE_ENDIAN 1
#define BROTLI_BIG_ENDIAN 0
#else
#if (defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__))
#define BROTLI_BIG_ENDIAN 1
#else
#define BROTLI_BIG_ENDIAN 0
#endif
#define BROTLI_LITTLE_ENDIAN 0
#endif

#if BROTLI_MODERN_COMPILER || __has_attribute(noinline)
#define BROTLI_NOINLINE __attribute__((noinline))
#else
#define BROTLI_NOINLINE
#endif

#define BROTLI_REPEAT(N, X) {     \
  if ((N & 1) != 0) {X;}          \
  if ((N & 2) != 0) {X; X;}       \
  if ((N & 4) != 0) {X; X; X; X;} \
}

#if BROTLI_MODERN_COMPILER || defined(__llvm__)
#if defined(BROTLI_TARGET_ARMV7)
static BROTLI_INLINE unsigned BrotliRBit(unsigned input) {
  unsigned output;
  __asm__("rbit %0, %1\n" : "=r"(output) : "r"(input));
  return output;
}
#define BROTLI_RBIT(x) BrotliRBit(x)
#endif  /* armv7 */
#endif  /* gcc || clang */

#if defined(BROTLI_TARGET_ARM)
#define BROTLI_HAS_UBFX (!!1)
#else
#define BROTLI_HAS_UBFX (!!0)
#endif

#define BROTLI_ALLOC(S, L) S->alloc_func(S->memory_manager_opaque, L)

#define BROTLI_FREE(S, X) {                  \
  S->free_func(S->memory_manager_opaque, X); \
  X = NULL;                                  \
}

#define BROTLI_UNUSED(X) (void)(X)

#endif  /* BROTLI_DEC_PORT_H_ */
