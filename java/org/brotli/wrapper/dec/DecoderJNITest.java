/* Copyright 2025 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

package org.brotli.wrapper.dec;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.fail;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import org.brotli.integration.BrotliJniTestBase;
import org.brotli.wrapper.enc.Encoder;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

/** Tests for {@link org.brotli.wrapper.dec.DecoderJNI}. */
@RunWith(JUnit4.class)
public class DecoderJNITest extends BrotliJniTestBase {

  @Test
  public void testPushLengthExceedingInputBufferIsRejected() throws IOException {
    DecoderJNI.Wrapper decoder = new DecoderJNI.Wrapper(1);
    try {
      decoder.getInputBuffer().put(0, (byte) 0);
      try {
        decoder.push(2);
        fail("push(length > inputBufferSize) must be rejected");
      } catch (IllegalArgumentException expected) {
        // Expected: oversized length would read past the input buffer.
      }
    } finally {
      decoder.destroy();
    }
  }

  @Test
  public void testPushNegativeLengthIsRejected() throws IOException {
    DecoderJNI.Wrapper decoder = new DecoderJNI.Wrapper(4);
    try {
      try {
        decoder.push(-1);
        fail("push(negative) must be rejected");
      } catch (IllegalArgumentException expected) {
        // Expected.
      }
    } finally {
      decoder.destroy();
    }
  }

  @Test
  public void testPushLengthEqualToInputBufferRoundTrips() throws IOException {
    byte[] data = new byte[64];
    for (int i = 0; i < data.length; i++) {
      data[i] = (byte) (i * 7);
    }
    byte[] compressed = Encoder.compress(data);

    DecoderJNI.Wrapper decoder = new DecoderJNI.Wrapper(compressed.length);
    ByteArrayOutputStream output = new ByteArrayOutputStream();
    try {
      decoder.getInputBuffer().put(compressed);
      decoder.push(compressed.length);
      int iterations = 0;
      while (decoder.getStatus() != DecoderJNI.Status.DONE) {
        if (++iterations > 1000) {
          fail("decoder did not finish within iteration bound");
        }
        switch (decoder.getStatus()) {
          case OK:
            decoder.push(0);
            break;

          case NEEDS_MORE_OUTPUT:
            ByteBuffer buffer = decoder.pull();
            byte[] chunk = new byte[buffer.remaining()];
            buffer.get(chunk);
            output.write(chunk, 0, chunk.length);
            break;

          case NEEDS_MORE_INPUT:
            decoder.push(0);
            if (decoder.getStatus() == DecoderJNI.Status.NEEDS_MORE_INPUT) {
              fail("unexpected truncated stream");
            }
            break;

          default:
            fail("unexpected decoder status: " + decoder.getStatus());
        }
      }
    } finally {
      decoder.destroy();
    }
    assertArrayEquals(data, output.toByteArray());
  }
}
