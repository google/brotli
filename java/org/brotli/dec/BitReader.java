/* Copyright 2015 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

package org.brotli.dec;

import java.io.IOException;
import java.io.InputStream;

/**
 * Bit reading helpers.
 */
final class BitReader {

  /**
   * Input byte buffer, consist of a ring-buffer and a "slack" region where bytes from the start of
   * the ring-buffer are copied.
   */
  private static final int CAPACITY = 1024;
  private static final int SLACK = 16;
  private static final int INT_BUFFER_SIZE = CAPACITY + SLACK;
  private static final int BYTE_READ_SIZE = CAPACITY << 2;
  private static final int BYTE_BUFFER_SIZE = INT_BUFFER_SIZE << 2;

  private final byte[] byteBuffer = new byte[BYTE_BUFFER_SIZE];
  private final int[] intBuffer = new int[INT_BUFFER_SIZE];
  private final IntReader intReader = new IntReader();

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
   * Offset of next item in intBuffer.
   */
  private int intOffset;

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
    if (br.intOffset <= CAPACITY - 9) {
      return;
    }
    if (br.endOfStreamReached) {
      if (intAvailable(br) >= -2) {
        return;
      }
      throw new BrotliRuntimeException("No more input");
    }
    int readOffset = br.intOffset << 2;
    int bytesRead = BYTE_READ_SIZE - readOffset;
    System.arraycopy(br.byteBuffer, readOffset, br.byteBuffer, 0, bytesRead);
    br.intOffset = 0;
    try {
      while (bytesRead < BYTE_READ_SIZE) {
        int len = br.input.read(br.byteBuffer, bytesRead, BYTE_READ_SIZE - bytesRead);
        if (len == -1) {
          br.endOfStreamReached = true;
          br.tailBytes = bytesRead;
          bytesRead += 3;
          break;
        }
        bytesRead += len;
      }
    } catch (IOException e) {
      throw new BrotliRuntimeException("Failed to read input", e);
    }
    IntReader.convert(br.intReader, bytesRead >> 2);
  }

  static void checkHealth(BitReader br, boolean endOfStream) {
    if (!br.endOfStreamReached) {
      return;
    }
    int byteOffset = (br.intOffset << 2) + ((br.bitOffset + 7) >> 3) - 8;
    if (byteOffset > br.tailBytes) {
      throw new BrotliRuntimeException("Read after end");
    }
    if (endOfStream && (byteOffset != br.tailBytes)) {
      throw new BrotliRuntimeException("Unused bytes after end");
    }
  }

  /**
   * Advances the Read buffer by 5 bytes to make room for reading next 24 bits.
   */
  static void fillBitWindow(BitReader br) {
    if (br.bitOffset >= 32) {
      br.accumulator = ((long) br.intBuffer[br.intOffset++] << 32) | (br.accumulator >>> 32);
      br.bitOffset -= 32;
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
    IntReader.init(br.intReader, br.byteBuffer, br.intBuffer);
    br.input = input;
    br.accumulator = 0;
    br.bitOffset = 64;
    br.intOffset = CAPACITY;
    br.endOfStreamReached = false;
    prepare(br);
  }

  private static void prepare(BitReader br) {
    readMoreInput(br);
    checkHealth(br, false);
    fillBitWindow(br);
    fillBitWindow(br);
  }

  static void reload(BitReader br) {
    if (br.bitOffset == 64) {
      prepare(br);
    }
  }

  static void close(BitReader br) throws IOException {
    InputStream is = br.input;
    br.input = null;
    if (is != null) {
      is.close();
    }
  }

  static void jumpToByteBoundary(BitReader br) {
    int padding = (64 - br.bitOffset) & 7;
    if (padding != 0) {
      int paddingBits = BitReader.readBits(br, padding);
      if (paddingBits != 0) {
        throw new BrotliRuntimeException("Corrupted padding bits");
      }
    }
  }

  static int intAvailable(BitReader br) {
    int limit = CAPACITY;
    if (br.endOfStreamReached) {
      limit = (br.tailBytes + 3) >> 2;
    }
    return limit - br.intOffset;
  }

  static void copyBytes(BitReader br, byte[] data, int offset, int length) {
    if ((br.bitOffset & 7) != 0) {
      throw new BrotliRuntimeException("Unaligned copyBytes");
    }

    // Drain accumulator.
    while ((br.bitOffset != 64) && (length != 0)) {
      data[offset++] = (byte) (br.accumulator >>> br.bitOffset);
      br.bitOffset += 8;
      length--;
    }
    if (length == 0) {
      return;
    }

    // Get data from shadow buffer with "sizeof(int)" granularity.
    int copyInts = Math.min(intAvailable(br), length >> 2);
    if (copyInts > 0) {
      int readOffset = br.intOffset << 2;
      System.arraycopy(br.byteBuffer, readOffset, data, offset, copyInts << 2);
      offset += copyInts << 2;
      length -= copyInts << 2;
      br.intOffset += copyInts;
    }
    if (length == 0) {
      return;
    }

    // Read tail bytes.
    if (intAvailable(br) > 0) {
      // length = 1..3
      fillBitWindow(br);
      while (length != 0) {
        data[offset++] = (byte) (br.accumulator >>> br.bitOffset);
        br.bitOffset += 8;
        length--;
      }
      checkHealth(br, false);
      return;
    }

    // Now it is possible to copy bytes directly.
    try {
      while (length > 0) {
        int len = br.input.read(data, offset, length);
        if (len == -1) {
          throw new BrotliRuntimeException("Unexpected end of input");
        }
        offset += len;
        length -= len;
      }
    } catch (IOException e) {
      throw new BrotliRuntimeException("Failed to read input", e);
    }
  }
}
