/* Copyright 2015 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

package org.brotli.dec;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

/**
 * Tests for {@link Transform}.
 */
@RunWith(JUnit4.class)
public class TransformTest {

  @Test
  public void testTrimAll() {
    byte[] output = new byte[2];
    byte[] input = "word".getBytes(StandardCharsets.UTF_8);
    Transform transform = new Transform("[", WordTransformType.OMIT_FIRST_5, "]");
    Transform.transformDictionaryWord(output, 0, input, 0, input.length, transform);
    assertArrayEquals(output, "[]".getBytes(StandardCharsets.UTF_8));
  }

  @Test
  public void testCapitalize() {
    byte[] output = new byte[8];
    byte[] input = "qæप".getBytes(StandardCharsets.UTF_8);
    Transform transform = new Transform("[", WordTransformType.UPPERCASE_ALL, "]");
    Transform.transformDictionaryWord(output, 0, input, 0, input.length, transform);
    assertArrayEquals(output, "[QÆय]".getBytes(StandardCharsets.UTF_8));
  }

  @Test
  public void testAllTransforms() throws NoSuchAlgorithmException {
    /* This string allows to apply all transforms: head and tail cutting, capitalization and
       turning to upper case; all results will be mutually different. */
    byte[] testWord = Transform.readUniBytes("o123456789abcdef");
    byte[] output = new byte[2259];
    int offset = 0;
    for (int i = 0; i < Transform.TRANSFORMS.length; ++i) {
      offset += Transform.transformDictionaryWord(
          output, offset, testWord, 0, testWord.length, Transform.TRANSFORMS[i]);
      output[offset++] = -1;
    }
    assertEquals(output.length, offset);

    MessageDigest md = MessageDigest.getInstance("SHA-256");
    md.update(output);
    byte[] digest = md.digest();
    String sha256 = String.format("%064x", new java.math.BigInteger(1, digest));
    assertEquals("60f1c7e45d788e24938c5a3919aaf41a7d8ad474d0ced6b9e4c0079f4d1da8c4", sha256);
  }
}
