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
typedef signed   char int8_t;
typedef unsigned char uint8_t;
typedef signed   short int16_t;
typedef unsigned short uint16_t;
typedef signed   int int32_t;
typedef unsigned int uint32_t;
typedef unsigned long long int uint64_t;
typedef long long int int64_t;
#else
#include <stdint.h>
#endif  /* defined(_MSC_VER) && (_MSC_VER < 1600) */

#endif  /* BROTLI_DEC_TYPES_H_ */
