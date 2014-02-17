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

   Transformations on dictionary words.
*/

#ifndef BROTLI_DEC_TRANSFORM_H_
#define BROTLI_DEC_TRANSFORM_H_

#include <stdio.h>
#include <ctype.h>
#include "./types.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

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
  kUppercaseAll   = 11
};

typedef struct {
  const char* prefix;
  enum WordTransformType transform;
  const char* suffix;
} Transform;

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
  if (p[0] < 0xc0) {
    if (p[0] >= 'a' && p[0] <= 'z') {
      p[0] ^= 32;
    }
    return 1;
  }
  /* An overly simplified uppercasing model for utf-8. */
  if (p[0] < 0xe0) {
    p[1] ^= 32;
    return 2;
  }
  /* An arbitrary transform for three byte characters. */
  p[2] ^= 5;
  return 3;
}

static BROTLI_INLINE int TransformDictionaryWord(
    uint8_t* dst, const uint8_t* word, int len, int transform) {
  const char* prefix = kTransforms[transform].prefix;
  const char* suffix = kTransforms[transform].suffix;
  const int t = kTransforms[transform].transform;
  int idx = 0;
  int i = 0;
  uint8_t* uppercase;
  while (*prefix) { dst[idx++] = (uint8_t)*prefix++; }
  if (t <= kOmit9) {
    len -= t;
  }
  while (i < len) { dst[idx++] = word[i++]; }
  uppercase = &dst[idx - len];
  if (t == kUppercaseFirst) {
    ToUpperCase(uppercase, len);
  } else if (t == kUppercaseAll) {
    while (len > 0) {
      int step = ToUpperCase(uppercase, len);
      uppercase += step;
      len -= step;
    }
  }
  while (*suffix) { dst[idx++] = (uint8_t)*suffix++; }
  return idx;
}

#if defined(__cplusplus) || defined(c_plusplus)
}    /* extern "C" */
#endif

#endif  /* BROTLI_DEC_TRANSFORM_H_ */
