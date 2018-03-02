#ifndef BROTLI_COMMON_SHARED_DICTIONARY_H_
#define BROTLI_COMMON_SHARED_DICTIONARY_H_

#include <brotli/port.h>
#include <brotli/types.h>

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#define SHARED_BROTLI_MIN_DICTIONARY_WORD_LENGTH 4
#define SHARED_BROTLI_MAX_DICTIONARY_WORD_LENGTH 31
#define SHARED_BROTLI_NUM_DICTIONARY_CONTEXTS 64
#define SHARED_BROTLI_MAX_COMPOUND_DICTS 15

typedef struct BrotliSharedDictionaryStruct BrotliSharedDictionary;

typedef enum BrotliSharedDictionaryType {
  /* A generic raw file as prefix (compound) dictionary. */
  BROTLI_SHARED_DICTIONARY_RAW = 0,
  /* A file in the shared dictionary format, can replace words and/or contain
     multiple compound dictionaries. */
  BROTLI_SHARED_DICTIONARY_SERIALIZED = 1
} BrotliSharedDictionaryType;

/**
 * Creates an instance of shared dictionary.
 *
 * Fresh instance has default word dictionary and transforms
 * and no LZ77 prefix dictionary.
 *
 * @p alloc_func and @p free_func @b MUST be both zero or both non-zero. In the
 * case they are both zero, default memory allocators are used. @p opaque is
 * passed to @p alloc_func and @p free_func when they are called. @p free_func
 * has to return without doing anything when asked to free a NULL pointer.
 *
 * @param alloc_func custom memory allocation function
 * @param free_func custom memory free function
 * @param opaque custom memory manager handle
 * @returns @c 0 if instance can not be allocated or initialized
 * @returns pointer to initialized ::BrotliDecoderState otherwise
 */
BROTLI_COMMON_API BrotliSharedDictionary* BrotliSharedDictionaryCreateInstance(
    brotli_alloc_func alloc_func, brotli_free_func free_func, void* opaque);

/* Deinitializes and frees ::BrotliSharedDictionary instance. */
BROTLI_COMMON_API void BrotliSharedDictionaryDestroyInstance(
    BrotliSharedDictionary* dict);

/* Attaches one dictionary to another, to combine compound dictionaries. */
BROTLI_COMMON_API BROTLI_BOOL BrotliSharedDictionaryAttach(
    BrotliSharedDictionary* dict, BrotliSharedDictionaryType type,
    const uint8_t* data, size_t size);

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif

#endif  /* BROTLI_COMMON_SHARED_DICTIONARY_H_ */
