/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Common types */

#ifndef BROTLI_COMMON_TYPES_H_
#define BROTLI_COMMON_TYPES_H_

#include <stddef.h>  /* for size_t */

#if defined(_MSC_VER) && (_MSC_VER < 1600)
typedef __int8 int8_t;
typedef unsigned __int8 uint8_t;
typedef __int16 int16_t;
typedef unsigned __int16 uint16_t;
typedef __int32 int32_t;
typedef unsigned __int32 uint32_t;
typedef unsigned __int64 uint64_t;
typedef __int64 int64_t;
#else
#include <stdint.h>
#endif  /* defined(_MSC_VER) && (_MSC_VER < 1600) */

#if (!defined(_MSC_VER) || (_MSC_VER >= 1800)) && \
  (defined(__cplusplus) || (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L))
#include <stdbool.h>
#define BROTLI_BOOL bool
#define BROTLI_TRUE true
#define BROTLI_FALSE false
#define TO_BROTLI_BOOL(X) (!!(X))
#else
typedef enum {
  BROTLI_FALSE = 0,
  BROTLI_TRUE = !BROTLI_FALSE
} BROTLI_BOOL;
#define TO_BROTLI_BOOL(X) (!!(X) ? BROTLI_TRUE : BROTLI_FALSE)
#endif

#define MAKE_UINT64_T(high, low) ((((uint64_t)(high)) << 32) | low)

#define BROTLI_UINT32_MAX (~((uint32_t)0))
#define BROTLI_SIZE_MAX (~((size_t)0))

/* Allocating function pointer. Function MUST return 0 in the case of failure.
   Otherwise it MUST return a valid pointer to a memory region of at least
   size length. Neither items nor size are allowed to be 0.
   opaque argument is a pointer provided by client and could be used to bind
   function to specific object (memory pool). */
typedef void* (*brotli_alloc_func)(void* opaque, size_t size);

/* Deallocating function pointer. Function SHOULD be no-op in the case the
   address is 0. */
typedef void (*brotli_free_func)(void* opaque, void* address);

#endif  /* BROTLI_COMMON_TYPES_H_ */
