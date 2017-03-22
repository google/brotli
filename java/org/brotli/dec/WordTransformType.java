/* Copyright 2015 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

package org.brotli.dec;

/**
 * Enumeration of all possible word transformations.
 *
 * <p>There are two simple types of transforms: omit X first/last symbols, two character-case
 * transforms and the identity transform.
 */
final class WordTransformType {
  static final int IDENTITY = 0;
  static final int OMIT_LAST_1 = 1;
  static final int OMIT_LAST_2 = 2;
  static final int OMIT_LAST_3 = 3;
  static final int OMIT_LAST_4 = 4;
  static final int OMIT_LAST_5 = 5;
  static final int OMIT_LAST_6 = 6;
  static final int OMIT_LAST_7 = 7;
  static final int OMIT_LAST_8 = 8;
  static final int OMIT_LAST_9 = 9;
  static final int UPPERCASE_FIRST = 10;
  static final int UPPERCASE_ALL = 11;
  static final int OMIT_FIRST_1 = 12;
  static final int OMIT_FIRST_2 = 13;
  static final int OMIT_FIRST_3 = 14;
  static final int OMIT_FIRST_4 = 15;
  static final int OMIT_FIRST_5 = 16;
  static final int OMIT_FIRST_6 = 17;
  static final int OMIT_FIRST_7 = 18;
  static final int OMIT_FIRST_8 = 19;
  static final int OMIT_FIRST_9 = 20;

  static int getOmitFirst(int type) {
    return type >= OMIT_FIRST_1 ? (type - OMIT_FIRST_1 + 1) : 0;
  }

  static int getOmitLast(int type) {
    return type <= OMIT_LAST_9 ? (type - OMIT_LAST_1 + 1) : 0;
  }
}
