package org.brotli.wrapper.dec;

import org.brotli.integration.BrotliJniTestBase;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

import java.io.IOException;
import java.nio.charset.StandardCharsets;

import static org.junit.Assert.*;

@RunWith(JUnit4.class)
public class LengthLimitedTest extends BrotliJniTestBase {

  private static final byte[] COMPRESSED_DATA = {-117, 2, -128, 66, 82, 79, 84, 76, 73, 3};
  private static final int ORIGINAL_DATA_LENGTH = 6; // "BROTLI" length

  @Test
  public void decompressKnownLength() throws IOException {
    byte[] decompressedData = Decoder.decompressKnownLength(COMPRESSED_DATA, ORIGINAL_DATA_LENGTH);
    assertEquals("BROTLI", new String(decompressedData, StandardCharsets.UTF_8));
  }

  @Test(expected = IllegalArgumentException.class)
  public void decompressKnownLengthDataTooBig() throws IOException {
    Decoder.decompressKnownLength(COMPRESSED_DATA, ORIGINAL_DATA_LENGTH - 1);
  }

  @Test(expected = IllegalArgumentException.class)
  public void decompressKnownLengthDataTooSmall() throws IOException {
    Decoder.decompressKnownLength(COMPRESSED_DATA, ORIGINAL_DATA_LENGTH + 1);
  }

  @Test(expected = IllegalArgumentException.class)
  public void decompressKnownLengthZeroLength() throws IOException {
    Decoder.decompressKnownLength(COMPRESSED_DATA, 0);
  }

  @Test
  public void decompressMaxLengthExactLength() throws IOException {
    byte[] decompressedData = Decoder.decompress(COMPRESSED_DATA, ORIGINAL_DATA_LENGTH);
    assertEquals("BROTLI", new String(decompressedData, StandardCharsets.UTF_8));
  }

  @Test
  public void decompressMaxLengthAllowedLength() throws IOException {
    byte[] decompressedData = Decoder.decompress(COMPRESSED_DATA, ORIGINAL_DATA_LENGTH + 5);
    assertEquals("BROTLI", new String(decompressedData, StandardCharsets.UTF_8));
  }

  @Test(expected = IllegalArgumentException.class)
  public void decompressMaxLengthDisallowedLength() throws IOException {
    Decoder.decompress(COMPRESSED_DATA, ORIGINAL_DATA_LENGTH - 2);
  }

  @Test
  public void decompressMaxLengthAnyLength() throws IOException {
    byte[] decompressedData = Decoder.decompress(COMPRESSED_DATA, 0);
    assertEquals("BROTLI", new String(decompressedData, StandardCharsets.UTF_8));
  }

  @Test(expected = IllegalArgumentException.class)
  public void decompressMaxLengthNegativeLength() throws IOException {
    Decoder.decompress(COMPRESSED_DATA, -1);
  }
}
