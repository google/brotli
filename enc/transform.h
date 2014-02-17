// Copyright 2010 Google Inc. All Rights Reserved.
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
// Transformations on dictionary words.

#ifndef BROTLI_ENC_TRANSFORM_H_
#define BROTLI_ENC_TRANSFORM_H_

#include <string>

#include "./dictionary.h"

namespace brotli {

enum WordTransformType {
  kIdentity       = 0,
  kOmit1          = 1,
  kOmit2          = 2,
  kOmit3          = 3,
  kOmit4          = 4,
  kOmit5          = 5,
  kOmit6          = 6,
  kOmit7          = 7,
  kOmit8          = 8,
  kOmit9          = 9,
  kUppercaseFirst = 10,
  kUppercaseAll   = 11,
};

struct Transform {
  const char* prefix;
  WordTransformType word_transform;
  const char* suffix;
};

static const Transform kTransforms[] = {
  { "",   kIdentity, ""    },
  { "",   kIdentity, " ",  },
  { "",   kIdentity, "\">" },
  { "",   kUppercaseFirst, ""    },
  { "",   kIdentity, "\""  },
  { "",   kIdentity, ".",  },
  { "",   kIdentity, "=\"" },
  { "",   kUppercaseFirst, " ",  },
  { " ",  kIdentity, "=\"" },
  { " ",  kIdentity, " ",  },
  { "",   kIdentity, ":", },
  { " ",  kIdentity, ""    },
  { "",   kIdentity, "\n"    },
  { "",   kIdentity, "(",  },
  { "",   kUppercaseAll, ""    },
  { ".",  kIdentity, "(",  },
  { "",   kIdentity, "'"   },
  { "",   kUppercaseFirst, "\""  },
  { " ",  kUppercaseFirst, " ",  },
  { "",   kOmit3, ""  },
  { "",   kOmit4, ""  },
  { ".",  kIdentity, ""    },
  { "",   kOmit1, ""  },
  { "",   kOmit2, ""  },
  { "",   kUppercaseFirst, "\">" },
  { "",   kOmit5, ""  },
  { "",   kUppercaseAll, " ",  },
  { " ",  kUppercaseFirst, ""    },
  { "",   kIdentity, ", ", },
  { "",   kUppercaseFirst, "(",  },
  { "",   kIdentity, "\n\t"    },
  { "",   kUppercaseFirst, "'"   },
  { ".",  kIdentity, " ",  },
  { " ",  kUppercaseAll, " ",  },
  { "",   kIdentity, "='"  },
  { "",   kUppercaseFirst, ".",  },
  { " ",  kIdentity, ".",  },
  { " ",  kIdentity, ", ", },
  { " ",  kUppercaseAll, ""    },
  { "",   kOmit6, ""  },
  { "",   kOmit9, ""  },
  { "",   kUppercaseAll, "\""  },
  { "",   kIdentity, " the "    },
  { "",   kIdentity, " in "    },
  { "",   kIdentity, " of "    },
  { "",   kIdentity, " to "    },
  { "",   kIdentity, " and "    },
  { "",   kIdentity, " is "    },
  { "",   kIdentity, " on "    },
  { "",   kIdentity, " by "    },
  { "",   kIdentity, " for "    },
  { "",   kIdentity, " with "    },
  { "",   kIdentity, " from "    },
  { "",   kIdentity, " as "    },
  { "",   kIdentity, " at "    },
  { "",   kIdentity, "er " },
  { " ",  kIdentity, "='"  },
  { "",   kIdentity, " a "    },
  { "",   kOmit7, ""  },
  { "",   kOmit8, ""  },
  { " ",  kIdentity, "(",  },
  { " ",  kIdentity, ". ", },
  { "",   kIdentity, ". ", },
  { "",   kIdentity, ",",  },
  { "",   kOmit1, "ing " },
  { "",   kIdentity, "ed " },
  { "",   kUppercaseFirst, ", ", },
  { "",   kUppercaseAll, ".",  },
  { "",   kUppercaseAll, "=\"" },
  { "",   kUppercaseAll, ", ", },
  { "",   kUppercaseAll, "\">" },
  { " ",  kUppercaseFirst, ".",  },
  { " ",  kUppercaseAll, "=\"" },
  { " ",  kUppercaseFirst, ", ", },
  { "",   kUppercaseAll, "'"   },
  { "",   kUppercaseFirst, "=\"" },
  { " ",  kIdentity, ",",  },
  { "",   kIdentity, " that "    },
  { "",   kUppercaseFirst, "='"  },
  { "",   kUppercaseFirst, ". ", },
  { "",   kUppercaseFirst, ",",  },
  { "",   kIdentity, ". The "    },
  { "\xc2\xa0",  kIdentity, "" },
  { " ",  kUppercaseFirst, ". ", },
  { "",   kUppercaseAll, ",",  },
  { "",   kUppercaseAll, "(",  },
  { " ",  kUppercaseAll, "='"  },
  { "",   kIdentity, "]"  },
  { "",   kUppercaseAll, "='"  },
  { " ",  kUppercaseAll, ".",  },
  { "",   kUppercaseAll, ". ", },
  { " ",  kUppercaseFirst, "=\"" },
  { " ",  kUppercaseAll, ". ", },
  { " ",  kUppercaseFirst, ",",  },
  { " ",  kUppercaseAll, ", ", },
  { "",   kIdentity, "ize " },
  { " ",  kUppercaseFirst, "='"  },
  { "",   kIdentity, "est " },
  { "",   kIdentity, ". This " },
};

static const int kNumTransforms = sizeof(kTransforms) / sizeof(kTransforms[0]);

static int ToUpperCase(uint8_t *p, int len) {
  if (len == 1 || p[0] < 0xc0) {
    if (p[0] >= 'a' && p[0] <= 'z') {
      p[0] ^= 32;
    }
    return 1;
  }
  if (p[0] < 0xe0) {
    p[1] ^= 32;
    return 2;
  }
  if (len == 2) {
    return 2;
  }
  p[2] ^= 5;
  return 3;
}

inline std::string ApplyTransform(
    const Transform& t, const uint8_t* word, int len) {
  std::string ret(t.prefix);
  if (t.word_transform <= kOmit9) {
    len -= t.word_transform;
  }
  if (len > 0) {
    ret += std::string(word, word + len);
    uint8_t *uppercase = reinterpret_cast<uint8_t*>(&ret[ret.size() - len]);
    if (t.word_transform == kUppercaseFirst) {
      ToUpperCase(uppercase, len);
    } else if (t.word_transform == kUppercaseAll) {
      while (len > 0) {
        int step = ToUpperCase(uppercase, len);
        uppercase += step;
        len -= step;
      }
    }
  }
  ret += std::string(t.suffix);
  return ret;
}

inline std::string GetTransformedDictionaryWord(int len_code, int word_id) {
  int num_words = 1 << kBrotliDictionarySizeBitsByLength[len_code];
  int offset = kBrotliDictionaryOffsetsByLength[len_code];
  int t = word_id / num_words;
  int word_idx = word_id % num_words;
  offset += len_code * word_idx;
  const uint8_t* word = &kBrotliDictionary[offset];
  return ApplyTransform(kTransforms[t], word, len_code);
}

}  // namespace brotli

#endif  // BROTLI_ENC_TRANSFORM_H_
