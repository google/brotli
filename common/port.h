/* Copyright 2016 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Macros for compiler / platform specific features and build options. */

#ifndef BROTLI_COMMON_PORT_H_
#define BROTLI_COMMON_PORT_H_

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
#elif BROTLI_GCC_VERSION > 300 || BROTLI_ICC_VERSION >= 1600
#define BROTLI_MODERN_COMPILER 1
#else
#define BROTLI_MODERN_COMPILER 0
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

#if BROTLI_MODERN_COMPILER || __has_attribute(noinline)
#define BROTLI_NOINLINE __attribute__((noinline))
#else
#define BROTLI_NOINLINE
#endif

#define BROTLI_UNUSED(X) (void)(X)

#endif  /* BROTLI_COMMON_PORT_H_ */
