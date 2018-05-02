/* Copyright 2016 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Macros for compiler / platform specific API declarations. */

#ifndef BROTLI_COMMON_PORT_H_
#define BROTLI_COMMON_PORT_H_

/* NB: borrowed from github.com.nemequ/hedley. */
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L) && \
    !defined(__STDC_NO_VLA__) && !defined(__cplusplus) &&         \
    !defined(__PGI) && !defined(__PGIC__) && !defined(__TINYC__)
#define BROTLI_ARRAY_PARAM(name) (name)
#else
#define BROTLI_ARRAY_PARAM(name)
#endif

#if defined(BROTLI_SHARED_COMPILATION) && defined(_WIN32)
#if defined(BROTLICOMMON_SHARED_COMPILATION)
#define BROTLI_COMMON_API __declspec(dllexport)
#else
#define BROTLI_COMMON_API __declspec(dllimport)
#endif  /* BROTLICOMMON_SHARED_COMPILATION */
#if defined(BROTLIDEC_SHARED_COMPILATION)
#define BROTLI_DEC_API __declspec(dllexport)
#else
#define BROTLI_DEC_API __declspec(dllimport)
#endif  /* BROTLIDEC_SHARED_COMPILATION */
#if defined(BROTLIENC_SHARED_COMPILATION)
#define BROTLI_ENC_API __declspec(dllexport)
#else
#define BROTLI_ENC_API __declspec(dllimport)
#endif  /* BROTLIENC_SHARED_COMPILATION */
#else  /* BROTLI_SHARED_COMPILATION && _WIN32 */
#define BROTLI_COMMON_API
#define BROTLI_DEC_API
#define BROTLI_ENC_API
#endif

#endif  /* BROTLI_COMMON_PORT_H_ */
