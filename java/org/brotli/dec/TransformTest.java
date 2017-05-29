/* Copyright 2015 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

package org.brotli.dec;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;

import java.nio.ByteBuffer;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

/**
 * Tests for {@link Transform}.
 */
@RunWith(JUnit4.class)
public class TransformTest {

  private static long crc64(byte[] data) {
    long crc = -1;
    for (int i = 0; i < data.length; ++i) {
      long c = (crc ^ (long) (data[i] & 0xFF)) & 0xFF;
      for (int k = 0; k < 8; k++) {
        c = (c >>> 1) ^ (-(c & 1L) & -3932672073523589310L);
      }
      crc = c ^ (crc >>> 8);
    }
    return ~crc;
  }

  @Test
  public void testTrimAll() {
    byte[] output = new byte[2];
    byte[] input = {119, 111, 114, 100}; // "word"
    Transform transform = new Transform("[", WordTransformType.OMIT_FIRST_5, "]");
    Transform.transformDictionaryWord(
        output, 0, ByteBuffer.wrap(input), 0, input.length, transform);
    byte[] expectedOutput = {91, 93}; // "[]"
    assertArrayEquals(expectedOutput, output);
  }

  @Test
  public void testCapitalize() {
    byte[] output = new byte[8];
    byte[] input = {113, -61, -90, -32, -92, -86}; // "qæप"
    Transform transform = new Transform("[", WordTransformType.UPPERCASE_ALL, "]");
    Transform.transformDictionaryWord(
      output, 0, ByteBuffer.wrap(input), 0, input.length, transform);
    byte[] expectedOutput = {91, 81, -61, -122, -32, -92, -81, 93}; // "[QÆय]"
    assertArrayEquals(expectedOutput, output);
  }

  @Test
  public void testAllTransforms() {
    /* This string allows to apply all transforms: head and tail cutting, capitalization and
       turning to upper case; all results will be mutually different. */
    // "o123456789abcdef"
    byte[] testWord = {111, 49, 50, 51, 52, 53, 54, 55, 56, 57, 97, 98, 99, 100, 101, 102};
    byte[] output = new byte[2259];
    int offset = 0;
    for (int i = 0; i < Transform.TRANSFORMS.length; ++i) {
      offset += Transform.transformDictionaryWord(
          output, offset, ByteBuffer.wrap(testWord), 0, testWord.length, Transform.TRANSFORMS[i]);
      output[offset++] = -1;
    }
    assertEquals(output.length, offset);
    assertEquals(8929191060211225186L, crc64(output));
  }
}
