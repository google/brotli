/* Copyright 2026 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

package org.brotli.wrapper.enc;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import java.io.IOException;
import java.nio.ByteBuffer;
import org.brotli.integration.BrotliJniTestBase;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

/** Tests for {@link org.brotli.wrapper.enc.EncoderJNI}. */
@RunWith(JUnit4.class)
public class EncoderJNITest extends BrotliJniTestBase {

  @Test
  public void testPushLengthExceedingInputBufferIsRejected() throws IOException {
    EncoderJNI.Wrapper encoder = new EncoderJNI.Wrapper(1, 5, 22, Encoder.Mode.GENERIC);
    try {
      encoder.getInputBuffer().put(0, (byte) 0);
      try {
        encoder.push(EncoderJNI.Operation.PROCESS, 2);
        fail("push(length > inputBufferSize) must be rejected");
      } catch (IllegalArgumentException expected) {
        // Expected: oversized length would read past the input buffer.
      }
    } finally {
      encoder.destroy();
    }
  }

  @Test
  public void testPushNegativeLengthIsRejected() throws IOException {
    EncoderJNI.Wrapper encoder = new EncoderJNI.Wrapper(4, 5, 22, Encoder.Mode.GENERIC);
    try {
      try {
        encoder.push(EncoderJNI.Operation.PROCESS, -1);
        fail("push(negative) must be rejected");
      } catch (IllegalArgumentException expected) {
        // Expected.
      }
    } finally {
      encoder.destroy();
    }
  }

  @Test
  public void testPushLengthEqualToInputBufferRoundTrips() throws IOException {
    EncoderJNI.Wrapper encoder = new EncoderJNI.Wrapper(64, 5, 22, Encoder.Mode.GENERIC);
    try {
      ByteBuffer input = encoder.getInputBuffer();
      for (int i = 0; i < 64; i++) {
        input.put(i, (byte) (i * 7));
      }
      encoder.push(EncoderJNI.Operation.FINISH, 64);
      assertTrue(encoder.isSuccess());
      ByteBuffer output = encoder.pull();
      assertTrue(output.limit() > 0);
      assertTrue(encoder.isFinished());
    } finally {
      encoder.destroy();
    }
  }
}
