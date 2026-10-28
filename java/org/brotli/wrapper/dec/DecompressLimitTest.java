/* Copyright 2026 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

package org.brotli.wrapper.dec;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import java.io.IOException;
import java.util.Arrays;
import org.brotli.integration.BrotliJniTestBase;
import org.brotli.wrapper.enc.Encoder;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

/** Tests for the {@link Decoder#decompress} output-size limit. */
@RunWith(JUnit4.class)
public class DecompressLimitTest extends BrotliJniTestBase {

  /** Decompression aborts once the output would exceed the limit. */
  @Test
  public void rejectsOutputExceedingLimit() throws IOException {
    byte[] original = new byte[64 * 1024];
    Arrays.fill(original, (byte) 'a');
    byte[] compressed = Encoder.compress(original);

    Decoder.Parameters params = new Decoder.Parameters().setMaxOutputSize(1024);
    try {
      Decoder.decompress(compressed, params);
      fail("expected IOException for output exceeding maximum size");
    } catch (IOException ex) {
      assertTrue("unexpected message: " + ex.getMessage(),
          ex.getMessage().contains("maximum size"));
    }
  }

  /** Output that fits the limit is returned unchanged. */
  @Test
  public void allowsOutputWithinLimit() throws IOException {
    byte[] original = new byte[8 * 1024];
    Arrays.fill(original, (byte) 'b');
    byte[] compressed = Encoder.compress(original);

    Decoder.Parameters params = new Decoder.Parameters().setMaxOutputSize(original.length);
    byte[] result = Decoder.decompress(compressed, params);
    assertArrayEquals(original, result);
  }

  /** Default parameters impose no limit. */
  @Test
  public void unlimitedByDefault() throws IOException {
    byte[] original = new byte[64 * 1024];
    Arrays.fill(original, (byte) 'c');
    byte[] compressed = Encoder.compress(original);

    byte[] result = Decoder.decompress(compressed, new Decoder.Parameters());
    assertArrayEquals(original, result);
  }
}
