/* Copyright 2018 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

package org.brotli.dec;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import java.io.ByteArrayInputStream;
import java.io.FileInputStream;
import java.io.FilterInputStream;
import java.io.IOException;
import java.io.InputStream;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

/**
 * Tests for {@link Decode}.
 */
@RunWith(JUnit4.class)
public class EagerStreamTest {

  static class ProxyStream extends FilterInputStream {
    int readBytes;

    ProxyStream(InputStream is) {
      super(is);
    }

    @Override
    public int read(byte[] b, int off, int len) throws IOException {
      int result = super.read(b, off, len);
      if (result > 0) {
        readBytes += result;
      }
      return result;
    }
  }

  @Test
  public void testEagerStream() throws IOException {
    int length = 6626;
    byte[] data = new byte[length];
    FileInputStream in = new FileInputStream(System.getProperty("TEST_DATA"));
    assertEquals(length, in.read(data));

    ProxyStream ps = new ProxyStream(new ByteArrayInputStream(data));
    BrotliInputStream reader = new BrotliInputStream(ps, 1);
    byte[] buffer = new byte[1];
    reader.read(buffer);
    reader.close();
    int normalReadBytes = ps.readBytes;

    ps = new ProxyStream(new ByteArrayInputStream(data));
    reader = new BrotliInputStream(ps, 1);
    reader.setEager(true);
    reader.read(buffer);
    reader.close();
    int eagerReadBytes = ps.readBytes;

    // Did not continue decoding - suspended as soon as enough data was decoded.
    assertTrue(eagerReadBytes < normalReadBytes);
  }
}
