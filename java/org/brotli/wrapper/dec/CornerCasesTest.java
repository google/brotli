/* Copyright 2021 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

package org.brotli.wrapper.dec;

import static org.junit.Assert.assertEquals;

import org.brotli.integration.BrotliJniTestBase;
import org.brotli.wrapper.enc.Encoder;
import java.io.IOException;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

/** Tests for {@link org.brotli.wrapper.enc.Encoder}. */
@RunWith(JUnit4.class)
public class CornerCasesTest extends BrotliJniTestBase {
  @Test
  public void testPowerOfTwoInput() throws IOException {
    // 24 == max window bits to ensure ring-buffer size is not greater than input.
    int len = 1 << 24;
    byte[] data = new byte[len];
    for (int i = 0; i < len; ++i) {
      data[i] = (byte) Integer.bitCount(i);
    }
    byte[] encoded = Encoder.compress(data);
    byte[] decoded = Decoder.decompress(encoded);
    assertEquals(len, decoded.length);
  }
}
