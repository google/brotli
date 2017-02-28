/* Copyright 2015 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

package org.brotli.dec;

import static org.junit.Assert.assertEquals;

import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

/**
 * Tests for {@link Dictionary}.
 */
@RunWith(JUnit4.class)
public class DictionaryTest {

  @Test
  public void testGetData() throws NoSuchAlgorithmException {
    MessageDigest md = MessageDigest.getInstance("SHA-256");
    md.update(Dictionary.getData());
    byte[] digest = md.digest();
    String sha256 = String.format("%064x", new java.math.BigInteger(1, digest));
    assertEquals("20e42eb1b511c21806d4d227d07e5dd06877d8ce7b3a817f378f313653f35c70", sha256);
  }
}
