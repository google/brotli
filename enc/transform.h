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
  kOmitLast1      = 1,
  kOmitLast2      = 2,
  kOmitLast3      = 3,
  kOmitLast4      = 4,
  kOmitLast5      = 5,
  kOmitLast6      = 6,
  kOmitLast7      = 7,
  kOmitLast8      = 8,
  kOmitLast9      = 9,
  kUppercaseFirst = 10,
  kUppercaseAll   = 11,
  kOmitFirst1     = 12,
  kOmitFirst2     = 13,
  kOmitFirst3     = 14,
  kOmitFirst4     = 15,
  kOmitFirst5     = 16,
  kOmitFirst6     = 17,
  kOmitFirst7     = 18,
  kOmitFirst8     = 19,
  kOmitFirst9     = 20,
};

struct Transform {
  const char* prefix;
  WordTransformType word_transform;
  const char* suffix;
};

static const Transform kTransforms[] = {
     {         "", kIdentity,       ""           },
     {         "", kIdentity,       " "          },
     {        " ", kIdentity,       " "          },
     {         "", kOmitFirst1,     ""           },
     {         "", kUppercaseFirst, " "          },
     {         "", kIdentity,       " the "      },
     {        " ", kIdentity,       ""           },
     {       "s ", kIdentity,       " "          },
     {         "", kIdentity,       " of "       },
     {         "", kUppercaseFirst, ""           },
     {         "", kIdentity,       " and "      },
     {         "", kOmitFirst2,     ""           },
     {         "", kOmitLast1,      ""           },
     {       ", ", kIdentity,       " "          },
     {         "", kIdentity,       ", "         },
     {        " ", kUppercaseFirst, " "          },
     {         "", kIdentity,       " in "       },
     {         "", kIdentity,       " to "       },
     {       "e ", kIdentity,       " "          },
     {         "", kIdentity,       "\""         },
     {         "", kIdentity,       "."          },
     {         "", kIdentity,       "\">"        },
     {         "", kIdentity,       "\n"         },
     {         "", kOmitLast3,      ""           },
     {         "", kIdentity,       "]"          },
     {         "", kIdentity,       " for "      },
     {         "", kOmitFirst3,     ""           },
     {         "", kOmitLast2,      ""           },
     {         "", kIdentity,       " a "        },
     {         "", kIdentity,       " that "     },
     {        " ", kUppercaseFirst, ""           },
     {         "", kIdentity,       ". "         },
     {        ".", kIdentity,       ""           },
     {        " ", kIdentity,       ", "         },
     {         "", kOmitFirst4,     ""           },
     {         "", kIdentity,       " with "     },
     {         "", kIdentity,       "'"          },
     {         "", kIdentity,       " from "     },
     {         "", kIdentity,       " by "       },
     {         "", kOmitFirst5,     ""           },
     {         "", kOmitFirst6,     ""           },
     {    " the ", kIdentity,       ""           },
     {         "", kOmitLast4,      ""           },
     {         "", kIdentity,       ". The "     },
     {         "", kUppercaseAll,   ""           },
     {         "", kIdentity,       " on "       },
     {         "", kIdentity,       " as "       },
     {         "", kIdentity,       " is "       },
     {         "", kOmitLast7,      ""           },
     {         "", kOmitLast1,      "ing "       },
     {         "", kIdentity,       "\n\t"       },
     {         "", kIdentity,       ":"          },
     {        " ", kIdentity,       ". "         },
     {         "", kIdentity,       "ed "        },
     {         "", kOmitFirst9,     ""           },
     {         "", kOmitFirst7,     ""           },
     {         "", kOmitLast6,      ""           },
     {         "", kIdentity,       "("          },
     {         "", kUppercaseFirst, ", "         },
     {         "", kOmitLast8,      ""           },
     {         "", kIdentity,       " at "       },
     {         "", kIdentity,       "ly "        },
     {    " the ", kIdentity,       " of "       },
     {         "", kOmitLast5,      ""           },
     {         "", kOmitLast9,      ""           },
     {        " ", kUppercaseFirst, ", "         },
     {         "", kUppercaseFirst, "\""         },
     {        ".", kIdentity,       "("          },
     {         "", kUppercaseAll,   " "          },
     {         "", kUppercaseFirst, "\">"        },
     {         "", kIdentity,       "=\""        },
     {        " ", kIdentity,       "."          },
     {    ".com/", kIdentity,       ""           },
     {    " the ", kIdentity,       " of the "   },
     {         "", kUppercaseFirst, "'"          },
     {         "", kIdentity,       ". This "    },
     {         "", kIdentity,       ","          },
     {        ".", kIdentity,       " "          },
     {         "", kUppercaseFirst, "("          },
     {         "", kUppercaseFirst, "."          },
     {         "", kIdentity,       " not "      },
     {        " ", kIdentity,       "=\""        },
     {         "", kIdentity,       "er "        },
     {        " ", kUppercaseAll,   " "          },
     {         "", kIdentity,       "al "        },
     {        " ", kUppercaseAll,   ""           },
     {         "", kIdentity,       "='"         },
     {         "", kUppercaseAll,   "\""         },
     {         "", kUppercaseFirst, ". "         },
     {        " ", kIdentity,       "("          },
     {         "", kIdentity,       "ful "       },
     {        " ", kUppercaseFirst, ". "         },
     {         "", kIdentity,       "ive "       },
     {         "", kIdentity,       "less "      },
     {         "", kUppercaseAll,   "'"          },
     {         "", kIdentity,       "est "       },
     {        " ", kUppercaseFirst, "."          },
     {         "", kUppercaseAll,   "\">"        },
     {        " ", kIdentity,       "='"         },
     {         "", kUppercaseFirst, ","          },
     {         "", kIdentity,       "ize "       },
     {         "", kUppercaseAll,   "."          },
     { "\xc2\xa0", kIdentity,       ""           },
     {        " ", kIdentity,       ","          },
     {         "", kUppercaseFirst, "=\""        },
     {         "", kUppercaseAll,   "=\""        },
     {         "", kIdentity,       "ous "       },
     {         "", kUppercaseAll,   ", "         },
     {         "", kUppercaseFirst, "='"         },
     {        " ", kUppercaseFirst, ","          },
     {        " ", kUppercaseAll,   "=\""        },
     {        " ", kUppercaseAll,   ", "         },
     {         "", kUppercaseAll,   ","          },
     {         "", kUppercaseAll,   "("          },
     {         "", kUppercaseAll,   ". "         },
     {        " ", kUppercaseAll,   "."          },
     {         "", kUppercaseAll,   "='"         },
     {        " ", kUppercaseAll,   ". "         },
     {        " ", kUppercaseFirst, "=\""        },
     {        " ", kUppercaseAll,   "='"         },
     {        " ", kUppercaseFirst, "='"         },
};

static const int kNumTransforms = sizeof(kTransforms) / sizeof(kTransforms[0]);

static const int kOmitFirstNTransforms[10] = {
  0, 3, 11, 26, 34, 39, 40, 55, 0, 54
};

static const int kOmitLastNTransforms[10] = {
  0, 12, 27, 23, 42, 63, 56, 48, 59, 64,
};

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
  if (t.word_transform <= kOmitLast9) {
    len -= t.word_transform;
  }
  if (len > 0) {
    if (t.word_transform >= kOmitFirst1) {
      const int skip = t.word_transform - (kOmitFirst1 - 1);
      if (len > skip) {
        ret += std::string(word + skip, word + len);
      }
    } else {
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
