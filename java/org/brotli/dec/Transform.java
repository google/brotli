/* Copyright 2015 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

package org.brotli.dec;

import static org.brotli.dec.WordTransformType.IDENTITY;
import static org.brotli.dec.WordTransformType.OMIT_FIRST_1;
import static org.brotli.dec.WordTransformType.OMIT_FIRST_2;
import static org.brotli.dec.WordTransformType.OMIT_FIRST_3;
import static org.brotli.dec.WordTransformType.OMIT_FIRST_4;
import static org.brotli.dec.WordTransformType.OMIT_FIRST_5;
import static org.brotli.dec.WordTransformType.OMIT_FIRST_6;
import static org.brotli.dec.WordTransformType.OMIT_FIRST_7;
import static org.brotli.dec.WordTransformType.OMIT_FIRST_9;
import static org.brotli.dec.WordTransformType.OMIT_LAST_1;
import static org.brotli.dec.WordTransformType.OMIT_LAST_2;
import static org.brotli.dec.WordTransformType.OMIT_LAST_3;
import static org.brotli.dec.WordTransformType.OMIT_LAST_4;
import static org.brotli.dec.WordTransformType.OMIT_LAST_5;
import static org.brotli.dec.WordTransformType.OMIT_LAST_6;
import static org.brotli.dec.WordTransformType.OMIT_LAST_7;
import static org.brotli.dec.WordTransformType.OMIT_LAST_8;
import static org.brotli.dec.WordTransformType.OMIT_LAST_9;
import static org.brotli.dec.WordTransformType.UPPERCASE_ALL;
import static org.brotli.dec.WordTransformType.UPPERCASE_FIRST;

/**
 * Transformations on dictionary words.
 */
final class Transform {

  private final byte[] prefix;
  private final WordTransformType type;
  private final byte[] suffix;

  Transform(String prefix, WordTransformType type, String suffix) {
    this.prefix = readUniBytes(prefix);
    this.type = type;
    this.suffix = readUniBytes(suffix);
  }

  static byte[] readUniBytes(String uniBytes) {
    byte[] result = new byte[uniBytes.length()];
    for (int i = 0; i < result.length; ++i) {
      result[i] = (byte) uniBytes.charAt(i);
    }
    return result;
  }

  static final Transform[] TRANSFORMS = {
      new Transform("", IDENTITY, ""),
      new Transform("", IDENTITY, " "),
      new Transform(" ", IDENTITY, " "),
      new Transform("", OMIT_FIRST_1, ""),
      new Transform("", UPPERCASE_FIRST, " "),
      new Transform("", IDENTITY, " the "),
      new Transform(" ", IDENTITY, ""),
      new Transform("s ", IDENTITY, " "),
      new Transform("", IDENTITY, " of "),
      new Transform("", UPPERCASE_FIRST, ""),
      new Transform("", IDENTITY, " and "),
      new Transform("", OMIT_FIRST_2, ""),
      new Transform("", OMIT_LAST_1, ""),
      new Transform(", ", IDENTITY, " "),
      new Transform("", IDENTITY, ", "),
      new Transform(" ", UPPERCASE_FIRST, " "),
      new Transform("", IDENTITY, " in "),
      new Transform("", IDENTITY, " to "),
      new Transform("e ", IDENTITY, " "),
      new Transform("", IDENTITY, "\""),
      new Transform("", IDENTITY, "."),
      new Transform("", IDENTITY, "\">"),
      new Transform("", IDENTITY, "\n"),
      new Transform("", OMIT_LAST_3, ""),
      new Transform("", IDENTITY, "]"),
      new Transform("", IDENTITY, " for "),
      new Transform("", OMIT_FIRST_3, ""),
      new Transform("", OMIT_LAST_2, ""),
      new Transform("", IDENTITY, " a "),
      new Transform("", IDENTITY, " that "),
      new Transform(" ", UPPERCASE_FIRST, ""),
      new Transform("", IDENTITY, ". "),
      new Transform(".", IDENTITY, ""),
      new Transform(" ", IDENTITY, ", "),
      new Transform("", OMIT_FIRST_4, ""),
      new Transform("", IDENTITY, " with "),
      new Transform("", IDENTITY, "'"),
      new Transform("", IDENTITY, " from "),
      new Transform("", IDENTITY, " by "),
      new Transform("", OMIT_FIRST_5, ""),
      new Transform("", OMIT_FIRST_6, ""),
      new Transform(" the ", IDENTITY, ""),
      new Transform("", OMIT_LAST_4, ""),
      new Transform("", IDENTITY, ". The "),
      new Transform("", UPPERCASE_ALL, ""),
      new Transform("", IDENTITY, " on "),
      new Transform("", IDENTITY, " as "),
      new Transform("", IDENTITY, " is "),
      new Transform("", OMIT_LAST_7, ""),
      new Transform("", OMIT_LAST_1, "ing "),
      new Transform("", IDENTITY, "\n\t"),
      new Transform("", IDENTITY, ":"),
      new Transform(" ", IDENTITY, ". "),
      new Transform("", IDENTITY, "ed "),
      new Transform("", OMIT_FIRST_9, ""),
      new Transform("", OMIT_FIRST_7, ""),
      new Transform("", OMIT_LAST_6, ""),
      new Transform("", IDENTITY, "("),
      new Transform("", UPPERCASE_FIRST, ", "),
      new Transform("", OMIT_LAST_8, ""),
      new Transform("", IDENTITY, " at "),
      new Transform("", IDENTITY, "ly "),
      new Transform(" the ", IDENTITY, " of "),
      new Transform("", OMIT_LAST_5, ""),
      new Transform("", OMIT_LAST_9, ""),
      new Transform(" ", UPPERCASE_FIRST, ", "),
      new Transform("", UPPERCASE_FIRST, "\""),
      new Transform(".", IDENTITY, "("),
      new Transform("", UPPERCASE_ALL, " "),
      new Transform("", UPPERCASE_FIRST, "\">"),
      new Transform("", IDENTITY, "=\""),
      new Transform(" ", IDENTITY, "."),
      new Transform(".com/", IDENTITY, ""),
      new Transform(" the ", IDENTITY, " of the "),
      new Transform("", UPPERCASE_FIRST, "'"),
      new Transform("", IDENTITY, ". This "),
      new Transform("", IDENTITY, ","),
      new Transform(".", IDENTITY, " "),
      new Transform("", UPPERCASE_FIRST, "("),
      new Transform("", UPPERCASE_FIRST, "."),
      new Transform("", IDENTITY, " not "),
      new Transform(" ", IDENTITY, "=\""),
      new Transform("", IDENTITY, "er "),
      new Transform(" ", UPPERCASE_ALL, " "),
      new Transform("", IDENTITY, "al "),
      new Transform(" ", UPPERCASE_ALL, ""),
      new Transform("", IDENTITY, "='"),
      new Transform("", UPPERCASE_ALL, "\""),
      new Transform("", UPPERCASE_FIRST, ". "),
      new Transform(" ", IDENTITY, "("),
      new Transform("", IDENTITY, "ful "),
      new Transform(" ", UPPERCASE_FIRST, ". "),
      new Transform("", IDENTITY, "ive "),
      new Transform("", IDENTITY, "less "),
      new Transform("", UPPERCASE_ALL, "'"),
      new Transform("", IDENTITY, "est "),
      new Transform(" ", UPPERCASE_FIRST, "."),
      new Transform("", UPPERCASE_ALL, "\">"),
      new Transform(" ", IDENTITY, "='"),
      new Transform("", UPPERCASE_FIRST, ","),
      new Transform("", IDENTITY, "ize "),
      new Transform("", UPPERCASE_ALL, "."),
      new Transform("\u00c2\u00a0", IDENTITY, ""),
      new Transform(" ", IDENTITY, ","),
      new Transform("", UPPERCASE_FIRST, "=\""),
      new Transform("", UPPERCASE_ALL, "=\""),
      new Transform("", IDENTITY, "ous "),
      new Transform("", UPPERCASE_ALL, ", "),
      new Transform("", UPPERCASE_FIRST, "='"),
      new Transform(" ", UPPERCASE_FIRST, ","),
      new Transform(" ", UPPERCASE_ALL, "=\""),
      new Transform(" ", UPPERCASE_ALL, ", "),
      new Transform("", UPPERCASE_ALL, ","),
      new Transform("", UPPERCASE_ALL, "("),
      new Transform("", UPPERCASE_ALL, ". "),
      new Transform(" ", UPPERCASE_ALL, "."),
      new Transform("", UPPERCASE_ALL, "='"),
      new Transform(" ", UPPERCASE_ALL, ". "),
      new Transform(" ", UPPERCASE_FIRST, "=\""),
      new Transform(" ", UPPERCASE_ALL, "='"),
      new Transform(" ", UPPERCASE_FIRST, "='")
  };

  static int transformDictionaryWord(byte[] dst, int dstOffset, byte[] word, int wordOffset,
      int len, Transform transform) {
    int offset = dstOffset;

    // Copy prefix.
    byte[] string = transform.prefix;
    int tmp = string.length;
    int i = 0;
    // In most cases tmp < 10 -> no benefits from System.arrayCopy
    while (i < tmp) {
      dst[offset++] = string[i++];
    }

    // Copy trimmed word.
    WordTransformType op = transform.type;
    tmp = op.omitFirst;
    if (tmp > len) {
      tmp = len;
    }
    wordOffset += tmp;
    len -= tmp;
    len -= op.omitLast;
    i = len;
    while (i > 0) {
      dst[offset++] = word[wordOffset++];
      i--;
    }

    if (op == UPPERCASE_ALL || op == UPPERCASE_FIRST) {
      int uppercaseOffset = offset - len;
      if (op == UPPERCASE_FIRST) {
        len = 1;
      }
      while (len > 0) {
        tmp = dst[uppercaseOffset] & 0xFF;
        if (tmp < 0xc0) {
          if (tmp >= 'a' && tmp <= 'z') {
            dst[uppercaseOffset] ^= (byte) 32;
          }
          uppercaseOffset += 1;
          len -= 1;
        } else if (tmp < 0xe0) {
          dst[uppercaseOffset + 1] ^= (byte) 32;
          uppercaseOffset += 2;
          len -= 2;
        } else {
          dst[uppercaseOffset + 2] ^= (byte) 5;
          uppercaseOffset += 3;
          len -= 3;
        }
      }
    }

    // Copy suffix.
    string = transform.suffix;
    tmp = string.length;
    i = 0;
    while (i < tmp) {
      dst[offset++] = string[i++];
    }

    return offset - dstOffset;
  }
}
