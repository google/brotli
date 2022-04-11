/* Copyright 2017 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

package org.brotli.wrapper.dec;

import java.io.IOException;
import java.nio.Buffer;
import java.nio.ByteBuffer;
import java.nio.channels.ReadableByteChannel;
import java.util.ArrayList;

/**
 * Base class for InputStream / Channel implementations.
 */
public class Decoder {
  private static final ByteBuffer EMPTY_BUFFER = ByteBuffer.allocate(0);
  private final ReadableByteChannel source;
  private final DecoderJNI.Wrapper decoder;
  ByteBuffer buffer;
  boolean closed;
  boolean eager;

  /**
   * Creates a Decoder wrapper.
   *
   * @param source underlying source
   * @param inputBufferSize read buffer size
   */
  public Decoder(ReadableByteChannel source, int inputBufferSize)
      throws IOException {
    if (inputBufferSize <= 0) {
      throw new IllegalArgumentException("buffer size must be positive");
    }
    if (source == null) {
      throw new NullPointerException("source can not be null");
    }
    this.source = source;
    this.decoder = new DecoderJNI.Wrapper(inputBufferSize);
  }

  private void fail(String message) throws IOException {
    try {
      close();
    } catch (IOException ex) {
      /* Ignore */
    }
    throw new IOException(message);
  }

  void attachDictionary(ByteBuffer dictionary) throws IOException {
    if (!decoder.attachDictionary(dictionary)) {
      fail("failed to attach dictionary");
    }
  }

  public void enableEagerOutput() {
    this.eager = true;
  }

  /**
   * Continue decoding.
   *
   * @return -1 if stream is finished, or number of bytes available in read buffer (> 0)
   */
  int decode() throws IOException {
    while (true) {
      if (buffer != null) {
        if (!buffer.hasRemaining()) {
          buffer = null;
        } else {
          return buffer.remaining();
        }
      }

      switch (decoder.getStatus()) {
        case DONE:
          return -1;

        case OK:
          decoder.push(0);
          break;

        case NEEDS_MORE_INPUT:
          // In "eager" more pulling preempts pushing.
          if (eager && decoder.hasOutput()) {
            buffer = decoder.pull();
            break;
          }
          ByteBuffer inputBuffer = decoder.getInputBuffer();
          ((Buffer) inputBuffer).clear();
          int bytesRead = source.read(inputBuffer);
          if (bytesRead == -1) {
            fail("unexpected end of input");
          }
          if (bytesRead == 0) {
            // No input data is currently available.
            buffer = EMPTY_BUFFER;
            return 0;
          }
          decoder.push(bytesRead);
          break;

        case NEEDS_MORE_OUTPUT:
          buffer = decoder.pull();
          break;

        default:
          fail("corrupted input");
      }
    }
  }

  void discard(int length) {
    ((Buffer) buffer).position(buffer.position() + length);
    if (!buffer.hasRemaining()) {
      buffer = null;
    }
  }

  int consume(ByteBuffer dst) {
    ByteBuffer slice = buffer.slice();
    int limit = Math.min(slice.remaining(), dst.remaining());
    ((Buffer) slice).limit(limit);
    dst.put(slice);
    discard(limit);
    return limit;
  }

  void close() throws IOException {
    if (closed) {
      return;
    }
    closed = true;
    decoder.destroy();
    source.close();
  }

  /**
   * Decodes the given data buffer.
   * @param data compressed data buffer to be decoded
   * @return Array containing the decompressed data
   * @throws IOException when input data is corrupt or the native brotli decoder could not be initialized
   */
  public static byte[] decompress(byte[] data) throws IOException {
    return decompress(data, 0);
  }


  /**
   * Decodes the given data buffer. Allows the caller to specify the maximum allowed decompressed length.
   * Ensures that space complexity grows linearly with the {@code maxDecompressedLength}.
   * @param data compressed data buffer to be decoded
   * @param maxDecompressedLength maximum allowed length of the decoded data or 0 for any length
   * @return Array containing the decompressed data
   * @throws IOException when input data is corrupt or the native brotli decoder could not be initialized
   * @throws IllegalArgumentException when the decompressed length exceeds {@code maxDecompressedLength}
   */
  public static byte[] decompress(byte[] data, int maxDecompressedLength) throws IOException {
    DecoderJNI.Wrapper decoder = new DecoderJNI.Wrapper(data.length);
    ArrayList<byte[]> output = new ArrayList<byte[]>();
    int totalOutputSize = 0;
    try {
      decoder.getInputBuffer().put(data);
      decoder.push(data.length);
      while (decoder.getStatus() != DecoderJNI.Status.DONE) {
        switch (decoder.getStatus()) {
          case OK:
            decoder.push(0);
            break;

          case NEEDS_MORE_OUTPUT:
            ByteBuffer buffer = decoder.pull(maxDecompressedLength);
            byte[] chunk = new byte[buffer.remaining()];
            buffer.get(chunk);
            output.add(chunk);
            totalOutputSize += chunk.length;
            if (maxDecompressedLength > 0 && buffer.remaining() + totalOutputSize > maxDecompressedLength) {
              throw new IllegalArgumentException("Output length has exceeded expected length");
            }
            break;

          case NEEDS_MORE_INPUT:
            // Give decoder a chance to process the remaining of the buffered byte.
            decoder.push(0);
            // If decoder still needs input, this means that stream is truncated.
            if (decoder.getStatus() == DecoderJNI.Status.NEEDS_MORE_INPUT) {
              throw new IOException("corrupted input");
            }
            break;

          default:
            throw new IOException("corrupted input");
        }
      }
    } finally {
      decoder.destroy();
    }
    if (output.size() == 1) {
      return output.get(0);
    }
    byte[] result = new byte[totalOutputSize];
    int offset = 0;
    for (byte[] chunk : output) {
      System.arraycopy(chunk, 0, result, offset, chunk.length);
      offset += chunk.length;
    }
    return result;
  }

  /**
   * Decodes the given data buffer. Allows the caller to specify the expected decompressed length when available.
   * Ensures that space complexity grows linearly with the expected {@code decompressedLength}.
   * @param data compressed data buffer to be decoded
   * @param decompressedLength expected length of the decoded data
   * @return Array containing the decompressed data
   * @throws IOException when input data is corrupt or the native brotli decoder could not be initialized
   * @throws IllegalArgumentException when the expected {@code decompressedLength} is not
   *          equal to the actual decompressed length
   */
  public static byte[] decompressKnownLength(byte[] data, int decompressedLength) throws IOException {
    if (decompressedLength <= 0) {
      // Prevents specifying a length of 0 to circumvent length enforcement.
      throw new IllegalArgumentException("provided decompressedLength must be > 0.");
    }
    DecoderJNI.Wrapper decoder = new DecoderJNI.Wrapper(data.length);
    byte[] output = new byte[decompressedLength];
    int outputRead = 0;
    try {
      decoder.getInputBuffer().put(data);
      decoder.push(data.length);
      while (decoder.getStatus() != DecoderJNI.Status.DONE) {
        switch (decoder.getStatus()) {
        case OK:
          decoder.push(0);
          break;

        case NEEDS_MORE_OUTPUT:
          ByteBuffer buffer = decoder.pull(decompressedLength);
          int readLen = Math.min(buffer.remaining(), decompressedLength - outputRead);
          buffer.get(output, outputRead, readLen);
          outputRead += readLen;
          if (buffer.remaining() > 0 && outputRead == decompressedLength) {
            throw new IllegalArgumentException("Output length has exceeded expected length");
          }
          break;

        case NEEDS_MORE_INPUT:
          // Allow decoder to decode any outstanding data from the existing input buffer.
          decoder.push(0);
          // If decoder still needs more, input buffer was incomplete.
          if (decoder.getStatus() == DecoderJNI.Status.NEEDS_MORE_INPUT) {
            throw new IOException("corrupted input");
          }
          break;

        default:
          throw new IOException("corrupted input");
        }
      }
    } finally {
      decoder.destroy();
    }
    if (outputRead < decompressedLength) {
      throw new IllegalArgumentException("Output length is less than expected length");
    }
    return output;
  }
}
