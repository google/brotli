/* Copyright 2017 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

package org.brotli.dec;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.IntBuffer;

/**
 * Byte-to-int conversion magic.
 */
final class IntReader {

  static final int CAPACITY = 1024 + 16;

  private final ByteBuffer byteBuffer =
      ByteBuffer.allocateDirect(CAPACITY << 2).order(ByteOrder.LITTLE_ENDIAN);
  private final IntBuffer intBuffer = byteBuffer.asIntBuffer();

  /**
   * Reinitialize reader with new data chunk.
   *
   * NB: intLen == 4 * byteSize!
   * NB: intLen should be less or equal to {@link CAPACITY}
   */
  static void reload(IntReader ir, byte[] data, int offset, int intLen) {
    ir.byteBuffer.clear();
    ir.byteBuffer.put(data, offset, intLen << 2);
    ir.intBuffer.rewind();
  }

  static int position(IntReader ir) {
    return ir.intBuffer.position();
  }

  static void setPosition(IntReader ir, int position) {
    ir.intBuffer.position(position);
  }

  static int read(IntReader ir) {
    // Advances position by 1.
    return ir.intBuffer.get();
  }
}
