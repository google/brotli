/* Copyright 2015 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

package org.brotli.dec;

import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.IntBuffer;

/**
 * Bit reading helpers.
 */
class BitReader {

  /**
   * Input byte buffer, consist of a ring-buffer and a "slack" region where bytes from the start of
   * the ring-buffer are copied.
   */
  private static final int READ_SIZE = 4096;
  private static final int BUF_SIZE = READ_SIZE + 64;

  private final ByteBuffer byteBuffer =
      ByteBuffer.allocateDirect(BUF_SIZE).order(ByteOrder.LITTLE_ENDIAN);
  private final IntBuffer intBuffer = byteBuffer.asIntBuffer();
  private final byte[] shadowBuffer = new byte[BUF_SIZE];

  private InputStream input;

  /**
   * Input stream is finished.
   */
  private boolean endOfStreamReached;

  /**
   * Pre-fetched bits.
   */
  long accumulator;

  /**
   * Current bit-reading position in accumulator.
   */
  int bitOffset;

  /**
   * Number of 32-bit integers availabale for reading.
   */
  private int available;

  /* Number of bytes in unfinished "int" item. */
  private int tailBytes = 0;

  /**
   * Fills up the input buffer.
   *
   * <p> No-op if there are at least 36 bytes present after current position.
   *
   * <p> After encountering the end of the input stream, 64 additional zero bytes are copied to the
   * buffer.
   */
  // TODO: Split to check and read; move read outside of decoding loop.
  static void readMoreInput(BitReader br) {
    if (br.available > 9) {
      return;
    }
    if (br.endOfStreamReached) {
      if (br.available > 4) {
        return;
      }
      throw new BrotliRuntimeException("No more input");
    }
    int readOffset = br.intBuffer.position() << 2;
    int bytesRead = READ_SIZE - readOffset;
    System.arraycopy(br.shadowBuffer, readOffset, br.shadowBuffer, 0, bytesRead);
    try {
      while (bytesRead < READ_SIZE) {
        int len = br.input.read(br.shadowBuffer, bytesRead, READ_SIZE - bytesRead);
        if (len == -1) {
          br.endOfStreamReached = true;
          Utils.fillWithZeroes(br.shadowBuffer, bytesRead, 64);
          bytesRead += 64;
          br.tailBytes = bytesRead & 3;
          break;
        }
        bytesRead += len;
      }
    } catch (IOException e) {
      throw new BrotliRuntimeException("Failed to read input", e);
    }
    br.byteBuffer.clear();
    br.byteBuffer.put(br.shadowBuffer, 0, bytesRead & 0xFFFC);
    br.intBuffer.rewind();
    br.available = bytesRead >> 2;
  }

  static void checkHealth(BitReader br) {
    if (!br.endOfStreamReached) {
      return;
    }
    /* When end of stream is reached, we "borrow" up to 64 zeroes to bit reader.
     * If compressed stream is valid, then borrowed zeroes should remain unused. */
    int valentBytes = (br.available << 2) + ((64 - br.bitOffset) >> 3);
    int borrowedBytes = 64 - br.tailBytes;
    if (valentBytes != borrowedBytes) {
      throw new BrotliRuntimeException("Read after end");
    }
  }

  /**
   * Advances the Read buffer by 5 bytes to make room for reading next 24 bits.
   */
  static void fillBitWindow(BitReader br) {
    if (br.bitOffset >= 32) {
      br.accumulator = ((long) br.intBuffer.get() << 32) | (br.accumulator >>> 32);
      br.bitOffset -= 32;
      br.available--;
    }
  }

  /**
   * Reads the specified number of bits from Read Buffer.
   */
  static int readBits(BitReader br, int n) {
    fillBitWindow(br);
    int val = (int) (br.accumulator >>> br.bitOffset) & ((1 << n) - 1);
    br.bitOffset += n;
    return val;
  }

  /**
   * Initialize bit reader.
   *
   * <p> Initialisation turns bit reader to a ready state. Also a number of bytes is prefetched to
   * accumulator. Because of that this method may block until enough data could be read from input.
   *
   * @param br BitReader POJO
   * @param input data source
   */
  static void init(BitReader br, InputStream input) {
    if (br.input != null) {
      throw new IllegalStateException("Bit reader already has associated input stream");
    }
    br.input = input;
    br.accumulator = 0;
    br.intBuffer.position(READ_SIZE >> 2);
    br.bitOffset = 64;
    br.available = 0;
    br.endOfStreamReached = false;
    readMoreInput(br);
    /* This situation is impossible in current implementation. */
    if (br.available == 0) {
      throw new BrotliRuntimeException("Can't initialize reader");
    }
    fillBitWindow(br);
    fillBitWindow(br);
  }

  static void close(BitReader br) throws IOException {
    InputStream is = br.input;
    br.input = null;
    if (is != null) {
      is.close();
    }
  }

  static void jumpToByteBoundry(BitReader br) {
    int padding = (64 - br.bitOffset) & 7;
    if (padding != 0) {
      int paddingBits = BitReader.readBits(br, padding);
      if (paddingBits != 0) {
        throw new BrotliRuntimeException("Corrupted padding bits ");
      }
    }
  }

}
