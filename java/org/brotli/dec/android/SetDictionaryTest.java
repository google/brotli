/* Copyright 2026 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

package org.brotli.dec.android;

import static java.nio.charset.StandardCharsets.US_ASCII;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import android.content.Context;
import android.content.res.AssetFileDescriptor;
import androidx.test.core.app.ApplicationProvider;
import java.io.ByteArrayInputStream;
import java.io.FileInputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.channels.FileChannel;
import org.brotli.dec.BrotliInputStream;
import org.brotli.dec.Dictionary;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;

/** Tests loading the Brotli static dictionary from an Android raw resource. */
@RunWith(RobolectricTestRunner.class)
public final class SetDictionaryTest {

  private static final int RFC_DICTIONARY_SIZE = 122784;
  private static final long RFC_DICTIONARY_CRC64 = 37084801881332636L;

  /** See {@code org.brotli.dec.SynthTest}. */
  private static final byte[] baseDictWord = {
    (byte) 0x1b, (byte) 0x03, (byte) 0x00, (byte) 0x00, (byte) 0x00, (byte) 0x00, (byte) 0x80,
    (byte) 0xe3, (byte) 0xb4, (byte) 0x0d, (byte) 0x00, (byte) 0x00, (byte) 0x07, (byte) 0x5b,
    (byte) 0x26, (byte) 0x31, (byte) 0x40, (byte) 0x02, (byte) 0x00, (byte) 0xe0, (byte) 0x4e,
    (byte) 0x1b, (byte) 0x41, (byte) 0x02
  };

  /** See {@code org.brotli.dec.SynthTest}. */
  private static final byte[] oneCommand = {
    (byte) 0x1b, (byte) 0x02, (byte) 0x00, (byte) 0x00, (byte) 0x00, (byte) 0x00, (byte) 0x80,
    (byte) 0xe3, (byte) 0xb4, (byte) 0x0d, (byte) 0x00, (byte) 0x00, (byte) 0x07, (byte) 0x5b,
    (byte) 0x26, (byte) 0x31, (byte) 0x40, (byte) 0x02, (byte) 0x00, (byte) 0xe0, (byte) 0x4e,
    (byte) 0x1b, (byte) 0x11, (byte) 0x86, (byte) 0x02
  };

  private static final int[] sizeBits = {
    0, 0, 0, 0, 10, 10, 11, 11, 10, 10, 10, 10, 10, 9, 9, 8, 7, 7, 8, 7, 7, 6, 6, 5, 5
  };

  @Test
  public void testSetDictionaryFromRawResource() throws IOException {
    byte[] buffer = new byte[16];

    // No dictionary set; decoding still succeeds when no dictionary entries are referenced.
    try (BrotliInputStream decoder = new BrotliInputStream(new ByteArrayInputStream(oneCommand))) {
      assertEquals(3, decoder.read(buffer, 0, buffer.length));
      assertEquals("aaa", new String(buffer, 0, 3, US_ASCII));
    }

    // Decoding a dictionary word must fail before the dictionary is set.
    try (BrotliInputStream decoder = new BrotliInputStream(new ByteArrayInputStream(baseDictWord))) {
      assertThrows(IOException.class, () -> decoder.read(buffer, 0, buffer.length));
    }

    // Memory-map the raw dictionary resource without copying into Java heap.
    Context context = ApplicationProvider.getApplicationContext();
    ByteBuffer dictionary;
    try (AssetFileDescriptor afd =
            context.getResources().openRawResourceFd(R.raw.brotli_dictionary);
        FileInputStream inputStream = afd.createInputStream();
        FileChannel dictionaryChannel = inputStream.getChannel()) {
      long length =
          afd.getLength() == AssetFileDescriptor.UNKNOWN_LENGTH
              ? dictionaryChannel.size() - afd.getStartOffset()
              : afd.getLength();
      assertEquals(RFC_DICTIONARY_SIZE, length);
      dictionary =
          dictionaryChannel.map(FileChannel.MapMode.READ_ONLY, afd.getStartOffset(), length);
    }
    assertTrue(dictionary.isDirect());
    assertTrue(dictionary.isReadOnly());
    Dictionary.setData(dictionary, sizeBits);

    // Verify mapped buffer remains valid and matches the RFC 7932 dictionary after closing the FD.
    assertEquals(RFC_DICTIONARY_CRC64, crc64(Dictionary.getData()));

    // Retry decoding of dictionary item.
    try (BrotliInputStream decoder = new BrotliInputStream(new ByteArrayInputStream(baseDictWord))) {
      assertEquals(4, decoder.read(buffer, 0, buffer.length));
      assertEquals("time", new String(buffer, 0, 4, US_ASCII));
    }
  }

  private static long crc64(ByteBuffer data) {
    long crc = -1;
    for (int i = 0; i < data.capacity(); ++i) {
      long c = (crc ^ (long) (data.get(i) & 0xFF)) & 0xFF;
      for (int k = 0; k < 8; k++) {
        c = (c >>> 1) ^ (-(c & 1L) & -3932672073523589310L);
      }
      crc = c ^ (crc >>> 8);
    }
    return ~crc;
  }
}
