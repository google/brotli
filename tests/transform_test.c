#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <brotli/types.h>
#include <brotli/shared_dictionary.h>
#include "../c/common/transform.h"

#define ASSERT_TRUE(x) do { \
  if (!(x)) { \
    fprintf(stderr, "Assertion failed: %s at %s:%d\n", #x, __FILE__, __LINE__); \
    exit(1); \
  } \
} while (0)

static void test_transform_normal(void) {
  const BrotliTransforms* transforms = BrotliGetTransforms();
  uint8_t dst[128];
  const uint8_t word[8] = "google";
  int out_len = BrotliTransformDictionaryWord(dst, word, 6, transforms, 0);
  ASSERT_TRUE(out_len == 6);
  ASSERT_TRUE(memcmp(dst, "google", 6) == 0);
}

static void test_transform_uppercase_bounds(void) {
  const BrotliTransforms* transforms = BrotliGetTransforms();
  uint8_t dst[128];
  /* Test truncated multibyte UTF-8 byte sequences:
     0xC3 by itself (expects 2 bytes in UTF-8) */
  const uint8_t truncated_2byte[2] = {0xC3, 0x00};
  /* 0xE2 by itself (expects 3 bytes in UTF-8) */
  const uint8_t truncated_3byte[3] = {0xE2, 0x82, 0x00};
  /* 0-length word */
  const uint8_t empty_word[1] = {0};

  /* Transform 10 is BROTLI_TRANSFORM_UPPERCASE_FIRST */
  {
    int out_len = BrotliTransformDictionaryWord(dst, truncated_2byte, 1, transforms, 10);
    ASSERT_TRUE(out_len >= 0);
  }
  {
    int out_len = BrotliTransformDictionaryWord(dst, truncated_3byte, 2, transforms, 10);
    ASSERT_TRUE(out_len >= 0);
  }
  {
    int out_len = BrotliTransformDictionaryWord(dst, empty_word, 0, transforms, 10);
    ASSERT_TRUE(out_len >= 0);
  }

  /* Test all 121 transforms against short and empty inputs to verify no buffer overreads */
  {
    uint32_t i;
    const uint8_t short_word[3] = {0xC3, 0xA9, 0x00};
    for (i = 0; i < transforms->num_transforms; ++i) {
      int out_len = BrotliTransformDictionaryWord(dst, short_word, 2, transforms, (int)i);
      ASSERT_TRUE(out_len >= 0);
      ASSERT_TRUE(out_len < 128);
    }
  }

  /* Test when skip >= len (negative remaining length guard) */
  {
    uint32_t i;
    const uint8_t single_byte[1] = {'a'};
    for (i = 0; i < transforms->num_transforms; ++i) {
      int out_len = BrotliTransformDictionaryWord(dst, single_byte, 1, transforms, (int)i);
      ASSERT_TRUE(out_len >= 0);
      ASSERT_TRUE(out_len < 128);
    }
  }
}

static void test_transform_negative_length(void) {
  const BrotliTransforms* transforms = BrotliGetTransforms();
  uint8_t dst[128];
  const uint8_t word[8] = "testing";
  /* len <= 0 should safely produce valid bounds without out-of-bounds pointer reads */
  int out_len = BrotliTransformDictionaryWord(dst, word, 0, transforms, 0);
  ASSERT_TRUE(out_len >= 0);
  out_len = BrotliTransformDictionaryWord(dst, word, -1, transforms, 0);
  ASSERT_TRUE(out_len >= 0);
}

static void test_shared_dictionary_bounds(void) {
  BrotliSharedDictionary* dict = BrotliSharedDictionaryCreateInstance(NULL, NULL, NULL);
  const uint8_t sample[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  ASSERT_TRUE(dict != NULL);
  /* Attach valid chunk */
  ASSERT_TRUE(BrotliSharedDictionaryAttach(dict, BROTLI_SHARED_DICTIONARY_RAW, sizeof(sample), sample));
  BrotliSharedDictionaryDestroyInstance(dict);
}

int main(void) {
  printf("Running transform_test...\n");
  test_transform_normal();
  test_transform_uppercase_bounds();
  test_transform_negative_length();
  test_shared_dictionary_bounds();
  printf("All transform_test cases passed successfully!\n");
  return 0;
}
