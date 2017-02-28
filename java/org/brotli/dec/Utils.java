/* Copyright 2015 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

package org.brotli.dec;

/**
 * A set of utility methods.
 */
final class Utils {

  private static final byte[] BYTE_ZEROES = new byte[1024];

  private static final int[] INT_ZEROES = new int[1024];

  /**
   * Fills byte array with zeroes.
   *
   * <p> Current implementation uses {@link System#arraycopy}, so it should be used for length not
   * less than 16.
   *
   * @param dest array to fill with zeroes
   * @param offset the first byte to fill
   * @param length number of bytes to change
   */
  static void fillWithZeroes(byte[] dest, int offset, int length) {
    int cursor = 0;
    while (cursor < length) {
      int step = Math.min(cursor + 1024, length) - cursor;
      System.arraycopy(BYTE_ZEROES, 0, dest, offset + cursor, step);
      cursor += step;
    }
  }

  /**
   * Fills int array with zeroes.
   *
   * <p> Current implementation uses {@link System#arraycopy}, so it should be used for length not
   * less than 16.
   *
   * @param dest array to fill with zeroes
   * @param offset the first item to fill
   * @param length number of item to change
   */
  static void fillWithZeroes(int[] dest, int offset, int length) {
    int cursor = 0;
    while (cursor < length) {
      int step = Math.min(cursor + 1024, length) - cursor;
      System.arraycopy(INT_ZEROES, 0, dest, offset + cursor, step);
      cursor += step;
    }
  }
}
