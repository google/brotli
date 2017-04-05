/* Copyright 2017 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

package org.brotli.dec;

/**
 * Byte-to-int conversion magic.
 */
final class IntReader {

  private byte[] byteBuffer;
  private int[] intBuffer;

  static void init(IntReader ir, byte[] byteBuffer, int[] intBuffer) {
    ir.byteBuffer = byteBuffer;
    ir.intBuffer = intBuffer;
  }

  /**
   * Translates bytes to ints.
   *
   * NB: intLen == 4 * byteSize!
   * NB: intLen should be less or equal to intBuffer length.
   */
  static void convert(IntReader ir, int intLen) {
    for (int i = 0; i < intLen; ++i) {
      ir.intBuffer[i] = ((ir.byteBuffer[i * 4] & 0xFF))
          | ((ir.byteBuffer[(i * 4) + 1] & 0xFF) << 8)
          | ((ir.byteBuffer[(i * 4) + 2] & 0xFF) << 16)
          | ((ir.byteBuffer[(i * 4) + 3] & 0xFF) << 24);
    }
  }
}
