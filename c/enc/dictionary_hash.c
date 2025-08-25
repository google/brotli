/* Copyright 2015 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Hash table on the 4-byte prefixes of static dictionary words. */

#include "dictionary_hash.h"

#include "../common/platform.h"  /* IWYU pragma: keep */
#include "static_init.h"

#if (BROTLI_STATIC_INIT != BROTLI_STATIC_INIT_NONE)
#include "../common/dictionary.h"
#include "hash_base.h"
#endif

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#if (BROTLI_STATIC_INIT != BROTLI_STATIC_INIT_NONE)
BROTLI_BOOL BrotliEncoderInitDictionaryHash(
    const BrotliDictionary* dict, uint16_t* words, uint8_t* lengths) {
  size_t global_idx = 0;
  size_t len;
  size_t i;
  static const uint8_t frozen_idx[1688] = {0, 0, 8, 164, 32, 56, 31, 191, 36, 4,
128, 81, 68, 132, 145, 129, 0, 0, 0, 28, 0, 8, 1, 1, 64, 3, 1, 0, 0, 0, 0, 0, 4,
64, 1, 2, 128, 0, 132, 49, 0, 0, 0, 0, 0, 0, 0, 0, 17, 0, 0, 0, 1, 0, 36, 152,
0, 0, 0, 0, 128, 8, 0, 0, 128, 0, 0, 8, 0, 0, 64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8,
0, 0, 0, 1, 0, 64, 133, 0, 32, 0, 0, 128, 1, 0, 0, 0, 0, 4, 4, 4, 32, 16, 130,
0, 128, 8, 0, 0, 0, 0, 0, 64, 0, 64, 0, 160, 0, 148, 53, 0, 0, 0, 0, 0, 128, 0,
130, 0, 0, 0, 8, 0, 0, 0, 0, 0, 48, 0, 0, 0, 0, 0, 0, 32, 1, 32, 129, 0, 12, 0,
1, 0, 0, 0, 0, 0, 0, 0, 16, 0, 0, 0, 16, 32, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 8,
0, 0, 2, 0, 0, 0, 0, 0, 32, 0, 0, 0, 2, 66, 128, 0, 0, 16, 0, 0, 0, 0, 64, 1, 6,
128, 8, 0, 192, 24, 32, 0, 0, 8, 4, 128, 128, 2, 160, 0, 160, 0, 64, 0, 0, 2, 0,
0, 0, 0, 0, 0, 0, 0, 0, 32, 1, 0, 0, 64, 0, 0, 0, 0, 0, 0, 32, 0, 66, 0, 2, 0,
4, 0, 8, 0, 2, 0, 0, 33, 8, 0, 0, 0, 8, 0, 128, 162, 4, 128, 0, 2, 33, 0, 160,
0, 8, 0, 64, 0, 160, 0, 129, 4, 0, 0, 32, 0, 0, 32, 0, 2, 0, 0, 0, 0, 0, 0, 128,
0, 0, 0, 0, 0, 64, 10, 0, 0, 0, 0, 32, 64, 0, 0, 0, 0, 0, 16, 0, 16, 16, 0, 0,
80, 2, 0, 0, 0, 0, 8, 0, 0, 16, 0, 8, 0, 0, 0, 8, 64, 128, 0, 0, 0, 8, 208, 0,
0, 0, 0, 0, 0, 0, 32, 0, 0, 0, 0, 0, 0, 32, 0, 8, 0, 128, 0, 0, 0, 1, 0, 0, 0,
16, 8, 1, 136, 0, 0, 36, 0, 64, 9, 0, 1, 32, 8, 0, 64, 64, 131, 16, 224, 32, 4,
0, 4, 5, 160, 0, 131, 0, 4, 96, 0, 0, 184, 192, 0, 177, 205, 96, 0, 0, 0, 0, 2,
0, 32, 0, 0, 0, 0, 0, 0, 0, 0, 64, 0, 0, 128, 0, 0, 8, 0, 0, 0, 0, 1, 4, 0, 1,
0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 4, 0, 0, 64, 69, 0, 0, 8, 2, 66, 32, 64, 0, 0, 0,
0, 0, 1, 0, 128, 17, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 12, 0, 16, 0, 0, 4, 128, 64,
0, 0, 0, 0, 0, 0, 0, 0, 224, 0, 8, 0, 0, 130, 16, 64, 128, 2, 64, 0, 0, 0, 128,
2, 192, 64, 0, 65, 0, 0, 0, 16, 0, 0, 0, 32, 4, 2, 2, 76, 0, 0, 0, 4, 72, 52,
131, 44, 76, 0, 0, 0, 0, 64, 1, 16, 148, 4, 0, 16, 10, 64, 0, 2, 0, 1, 0, 128,
64, 68, 0, 0, 0, 0, 0, 64, 144, 0, 8, 0, 2, 0, 0, 0, 0, 0, 0, 3, 64, 0, 0, 0, 0,
1, 128, 0, 0, 32, 66, 0, 0, 0, 40, 0, 18, 0, 0, 0, 0, 0, 33, 0, 0, 32, 0, 0, 32,
0, 128, 4, 64, 145, 140, 0, 0, 0, 128, 0, 2, 0, 0, 20, 0, 80, 38, 0, 0, 32, 0,
32, 64, 4, 4, 0, 4, 0, 0, 0, 129, 4, 0, 0, 144, 17, 32, 130, 16, 132, 24, 134,
0, 0, 64, 2, 5, 50, 8, 194, 33, 1, 68, 117, 1, 8, 32, 161, 54, 0, 130, 34, 0, 0,
0, 64, 128, 0, 0, 2, 0, 0, 0, 0, 32, 1, 0, 0, 0, 3, 14, 0, 0, 0, 0, 0, 16, 4, 0,
0, 0, 0, 0, 0, 0, 0, 96, 1, 24, 18, 0, 1, 128, 24, 0, 64, 0, 4, 0, 16, 128, 0,
64, 0, 0, 0, 64, 0, 8, 0, 0, 0, 0, 0, 66, 128, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 16, 0, 64, 2, 0, 0, 0, 0, 6, 0, 8, 8, 2, 0, 64};

  memset(lengths, 0, BROTLI_ENC_NUM_HASH_BUCKETS);

  for (len = BROTLI_MAX_DICTIONARY_WORD_LENGTH;
       len >= BROTLI_MIN_DICTIONARY_WORD_LENGTH; --len) {
    size_t length_lt_8 = len < 8 ? 1 : 0;
    size_t n = 1u << dict->size_bits_by_length[len];
    const uint8_t* dict_words = dict->data + dict->offsets_by_length[len];
    for (i = 0; i < n; ++i) {
      size_t j = n - 1 - i;
      const uint8_t* word = dict_words + len * j;
      const uint32_t key = Hash14(word);
      size_t idx = (key << 1) + length_lt_8;
      if ((lengths[idx] & 0x80) == 0) {
        BROTLI_BOOL is_final = TO_BROTLI_BOOL(frozen_idx[global_idx / 8] &
                                              (1u << (global_idx % 8)));
        words[idx] = (uint16_t)j;
        lengths[idx] = (uint8_t)(len + (is_final ? 0x80 : 0));
      }
      global_idx++;
    }
  }
  for (i = 0; i < BROTLI_ENC_NUM_HASH_BUCKETS; ++i) {
    lengths[i] &= 0x7F;
  }

  return BROTLI_TRUE;
}

BROTLI_MODEL("small")
uint16_t kStaticDictionaryHashWords[BROTLI_ENC_NUM_HASH_BUCKETS];
BROTLI_MODEL("small")
uint8_t kStaticDictionaryHashLengths[BROTLI_ENC_NUM_HASH_BUCKETS];

#else  /* BROTLI_STATIC_INIT */

/* Embed kStaticDictionaryHashWords and kStaticDictionaryHashLengths. */
#include "dictionary_hash_inc.h"

#endif  /* BROTLI_STATIC_INIT */

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif
