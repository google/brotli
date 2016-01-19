/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

// Macros for endianness, branch prediction and unaligned loads and stores.

#ifndef BROTLI_ENC_PORT_H_
#define BROTLI_ENC_PORT_H_

#include <assert.h>
#include <string.h>
#include "./types.h"

#if defined OS_LINUX || defined OS_CYGWIN
#include <endian.h>
#elif defined OS_FREEBSD
#include <machine/endian.h>
#elif defined OS_MACOSX
#include <machine/endian.h>
/* Let's try and follow the Linux convention */
#define __BYTE_ORDER  BYTE_ORDER
#define __LITTLE_ENDIAN LITTLE_ENDIAN
#endif

// define the macro IS_LITTLE_ENDIAN
// using the above endian definitions from endian.h if
// endian.h was included
#ifdef __BYTE_ORDER
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define IS_LITTLE_ENDIAN
#endif

#else

#if defined(__LITTLE_ENDIAN__)
#define IS_LITTLE_ENDIAN
#endif
#endif  // __BYTE_ORDER

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#define IS_LITTLE_ENDIAN
#endif

// Enable little-endian optimization for x64 architecture on Windows.
#if (defined(_WIN32) || defined(_WIN64)) && defined(_M_X64)
#define IS_LITTLE_ENDIAN
#endif

/* Compatibility with non-clang compilers. */
#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

#if (__GNUC__ > 2) || (__GNUC__ == 2 && __GNUC_MINOR__ > 95) || \
    (defined(__llvm__) && __has_builtin(__builtin_expect))
#define PREDICT_FALSE(x) (__builtin_expect(x, 0))
#define PREDICT_TRUE(x) (__builtin_expect(!!(x), 1))
#else
#define PREDICT_FALSE(x) (x)
#define PREDICT_TRUE(x) (x)
#endif

// Portable handling of unaligned loads, stores, and copies.
// On some platforms, like ARM, the copy functions can be more efficient
// then a load and a store.

#if defined(ARCH_PIII) || \
  defined(ARCH_ATHLON) || defined(ARCH_K8) || defined(_ARCH_PPC)

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
