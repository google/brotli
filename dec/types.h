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

/* Common types */

#ifndef BROTLI_DEC_TYPES_H_
#define BROTLI_DEC_TYPES_H_

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

/* Allocating function pointer. Function MUST return 0 in the case of failure.
   Otherwise it MUST return a valid pointer to a memory region of at least
   size length. Neither items nor size are allowed to be 0.
   opaque argument is a pointer provided by client and could be used to bind
   function to specific object (memory pool). */
typedef void* (*brotli_alloc_func) (void* opaque, size_t size);

/* Deallocating function pointer. Function SHOULD be no-op in the case the
   address is 0. */
typedef void  (*brotli_free_func)  (void* opaque, void* address);

#endif  /* BROTLI_DEC_TYPES_H_ */
