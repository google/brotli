/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Common types */

#ifndef BROTLI_ENC_TYPES_H_
#define BROTLI_ENC_TYPES_H_

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

#define MAKE_UINT64_T(high, low) ((((uint64_t)(high)) << 32) | low)

#endif  /* BROTLI_ENC_TYPES_H_ */
