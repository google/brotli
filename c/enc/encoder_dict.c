/* Copyright 2017 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

#ifdef __VMS
#include "encoder_dict.h"
#else
#include "./encoder_dict.h"
#endif

#include "../common/dictionary.h"
#include "../common/transform.h"
#ifdef __VMS
#include "dictionary_hash.h"
#else
#include "./dictionary_hash.h"
#endif
#ifdef __VMS
#include "hash.h"
#else
#include "./hash.h"
#endif

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

void BrotliInitEncoderDictionary(BrotliEncoderDictionary* dict) {
  dict->words = BrotliGetDictionary();
  dict->num_transforms = (uint32_t)BrotliGetTransforms()->num_transforms;

  dict->hash_table_words = kStaticDictionaryHashWords;
  dict->hash_table_lengths = kStaticDictionaryHashLengths;
  dict->buckets = kStaticDictionaryBuckets;
  dict->dict_words = kStaticDictionaryWords;

  dict->cutoffTransformsCount = kCutoffTransformsCount;
  dict->cutoffTransforms = kCutoffTransforms;
}

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif
