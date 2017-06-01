package org.brotli.wrapper.enc;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import org.brotli.integration.BundleHelper;
import org.brotli.wrapper.common.BrotliCommon;
import org.brotli.wrapper.dec.BrotliInputStream;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.util.List;
import junit.framework.TestCase;
import junit.framework.TestSuite;
import org.junit.runner.RunWith;
import org.junit.runners.AllTests;

/** Tests for compression / decompression aided with LZ77 dictionary. */
@RunWith(AllTests.class)
public class UseCustomDictionaryTest {

  // TODO: remove when Bazel get JNI support.
  static {
    System.load(new java.io.File(new java.io.File(System.getProperty("java.library.path")),
        "liblibjni.so").getAbsolutePath());
  }

  static InputStream getBundle() throws IOException {
    return new FileInputStream(System.getProperty("TEST_BUNDLE"));
  }

  /** Creates a test suite. */
  public static TestSuite suite() throws IOException {
    TestSuite suite = new TestSuite();
    InputStream bundle = getBundle();
    try {
      List<String> entries = BundleHelper.listEntries(bundle);
      for (String entry : entries) {
        suite.addTest(new UseCustomDictionaryTestCase(entry));
      }
    } finally {
      bundle.close();
    }
    return suite;
  }

  /** Test case with a unique name. */
  static class UseCustomDictionaryTestCase extends TestCase {
    final String entryName;
    UseCustomDictionaryTestCase(String entryName) {
      super("UseCustomDictionaryTest." + entryName);
      this.entryName = entryName;
    }

    @Override
    protected void runTest() throws Throwable {
      UseCustomDictionaryTest.run(entryName);
    }
  }

  private static void run(String entryName) throws Throwable {
    InputStream bundle = getBundle();
    byte[] original;
    try {
      original = BundleHelper.readEntry(bundle, entryName);
    } finally {
      bundle.close();
    }

    if (original == null) {
      throw new RuntimeException("Can't read bundle entry: " + entryName);
    }

    ByteBuffer dictionary = BrotliCommon.makeNative(original);

    ByteArrayOutputStream dst = new ByteArrayOutputStream();
    OutputStream encoder = new BrotliOutputStream(dst,
        new Encoder.Parameters().setQuality(11).setWindow(23), 1 << 23, dictionary);
    try {
      encoder.write(original);
    } finally {
      encoder.close();
    }

    byte[] compressed = dst.toByteArray();

    // Just copy self from LZ77 dictionary -> ultimate compression ratio.
    assertTrue(compressed.length < 80 + original.length / 65536);

    InputStream decoder = new BrotliInputStream(new ByteArrayInputStream(compressed),
        1 << 23, dictionary);
    try {
      long originalCrc = BundleHelper.fingerprintStream(new ByteArrayInputStream(original));
      long crc = BundleHelper.fingerprintStream(decoder);
      assertEquals(originalCrc, crc);
    } finally {
      decoder.close();
    }
  }
}
