// Copyright 2013 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Macros for endianness, branch prediction and unaligned loads and stores.

#ifndef BROTLI_ENC_PORT_H_
#define BROTLI_ENC_PORT_H_

#if defined OS_LINUX || defined OS_CYGWIN
#include <endian.h>
#elif defined OS_FREEBSD
#include <machine/endian.h>
#elif defined OS_MACOSX
#include <machine/endian.h>
/* Let's try and follow the Linux convention */
#define __BYTE_ORDER  BYTE_ORDER
#define __LITTLE_ENDIAN LITTLE_ENDIAN
#define __BIG_ENDIAN BIG_ENDIAN
#endif

// define the macros IS_LITTLE_ENDIAN or IS_BIG_ENDIAN
// using the above endian definitions from endian.h if
// endian.h was included
#ifdef __BYTE_ORDER
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define IS_LITTLE_ENDIAN
#endif

#if __BYTE_ORDER == __BIG_ENDIAN
#define IS_BIG_ENDIAN
#endif

#else

#if defined(__LITTLE_ENDIAN__)
#define IS_LITTLE_ENDIAN
#elif defined(__BIG_ENDIAN__)
#define IS_BIG_ENDIAN
#endif
#endif  // __BYTE_ORDER

#if defined(COMPILER_GCC3)
#define PREDICT_FALSE(x) (__builtin_expect(x, 0))
#define PREDICT_TRUE(x) (__builtin_expect(!!(x), 1))
#else
#define PREDICT_FALSE(x) x
#define PREDICT_TRUE(x) x
#endif

// Portable handling of unaligned loads, stores, and copies.
// On some platforms, like ARM, the copy functions can be more efficient
// then a load and a store.

#if defined(ARCH_PIII) || defined(ARCH_ATHLON) || \
  defined(ARCH_K8) || defined(_ARCH_PPC)

// x86 and x86-64 can perform unaligned loads/stores directly;
// modern PowerPC hardware can also do unaligned integer loads and stores;
// but note: the FPU still sends unaligned loads and stores to a trap handler!

#define BROTLI_UNALIGNED_LOAD32(_p) (*reinterpret_cast<const uint32_t *>(_p))
#define BROTLI_UNALIGNED_LOAD64(_p) (*reinterpret_cast<const uint64_t *>(_p))

#define BROTLI_UNALIGNED_STORE32(_p, _val) \
  (*reinterpret_cast<uint32_t *>(_p) = (_val))
#define BROTLI_UNALIGNED_STORE64(_p, _val) \
  (*reinterpret_cast<uint64_t *>(_p) = (_val))

#elif defined(__arm__) && \
  !defined(__ARM_ARCH_5__) && \
  !defined(__ARM_ARCH_5T__) && \
  !defined(__ARM_ARCH_5TE__) && \
  !defined(__ARM_ARCH_5TEJ__) && \
  !defined(__ARM_ARCH_6__) && \
  !defined(__ARM_ARCH_6J__) && \
  !defined(__ARM_ARCH_6K__) && \
  !defined(__ARM_ARCH_6Z__) && \
  !defined(__ARM_ARCH_6ZK__) && \
  !defined(__ARM_ARCH_6T2__)

// ARMv7 and newer support native unaligned accesses, but only of 16-bit
// and 32-bit values (not 64-bit); older versions either raise a fatal signal,
// do an unaligned read and rotate the words around a bit, or do the reads very
// slowly (trip through kernel mode).

#define BROTLI_UNALIGNED_LOAD32(_p) (*reinterpret_cast<const uint32_t *>(_p))
#define BROTLI_UNALIGNED_STORE32(_p, _val) \
  (*reinterpret_cast<uint32_t *>(_p) = (_val))

inline uint64_t BROTLI_UNALIGNED_LOAD64(const void *p) {
  uint64_t t;
  memcpy(&t, p, sizeof t);
  return t;
}

inline void BROTLI_UNALIGNED_STORE64(void *p, uint64_t v) {
  memcpy(p, &v, sizeof v);
}

#else

// These functions are provided for architectures that don't support
// unaligned loads and stores.

inline uint32_t BROTLI_UNALIGNED_LOAD32(const void *p) {
  uint32_t t;
  memcpy(&t, p, sizeof t);
  return t;
}

inline uint64_t BROTLI_UNALIGNED_LOAD64(const void *p) {
  uint64_t t;
  memcpy(&t, p, sizeof t);
  return t;
}

inline void BROTLI_UNALIGNED_STORE32(void *p, uint32_t v) {
  memcpy(p, &v, sizeof v);
}

inline void BROTLI_UNALIGNED_STORE64(void *p, uint64_t v) {
  memcpy(p, &v, sizeof v);
}

#endif

#endif  // BROTLI_ENC_PORT_H_
