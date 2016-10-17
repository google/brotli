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
enum WordTransformType {
  IDENTITY(0, 0),
  OMIT_LAST_1(0, 1),
  OMIT_LAST_2(0, 2),
  OMIT_LAST_3(0, 3),
  OMIT_LAST_4(0, 4),
  OMIT_LAST_5(0, 5),
  OMIT_LAST_6(0, 6),
  OMIT_LAST_7(0, 7),
  OMIT_LAST_8(0, 8),
  OMIT_LAST_9(0, 9),
  UPPERCASE_FIRST(0, 0),
  UPPERCASE_ALL(0, 0),
  OMIT_FIRST_1(1, 0),
  OMIT_FIRST_2(2, 0),
  OMIT_FIRST_3(3, 0),
  OMIT_FIRST_4(4, 0),
  OMIT_FIRST_5(5, 0),
  OMIT_FIRST_6(6, 0),
  OMIT_FIRST_7(7, 0),
  /*
   * brotli specification doesn't use OMIT_FIRST_8(8, 0) transform.
   * Probably, it would be used in future format extensions.
   */
  OMIT_FIRST_9(9, 0);

  final int omitFirst;
  final int omitLast;

  WordTransformType(int omitFirst, int omitLast) {
    this.omitFirst = omitFirst;
    this.omitLast = omitLast;
  }
}
