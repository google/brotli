/* Copyright 2016 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

package org.brotli.integration;

import org.brotli.dec.BrotliInputStream;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FilterInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.math.BigInteger;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * Decompress files and (optionally) checks their checksums.
 *
 * <p> File are read from ZIP archive passed as an array of bytes. Multiple checkers negotiate about
 * task distribution via shared AtomicInteger counter.
 * <p> All entries are expected to be valid brotli compressed streams and output CRC64 checksum
 * is expected to match the checksum hex-encoded in the first part of entry name.
 */
public class BundleChecker implements Runnable {
  final AtomicInteger nextJob;
  final InputStream input;
  final boolean sanityCheck;

  /**
   * @param sanityCheck do not calculate checksum and ignore {@link IOException}.
   */
  public BundleChecker(InputStream input, AtomicInteger nextJob, boolean sanityCheck) {
    this.input = input;
    this.nextJob = nextJob;
    this.sanityCheck = sanityCheck;
  }

  /** ECMA CRC64 polynomial. */
  static final long CRC_64_POLY = new BigInteger("C96C5795D7870F42", 16).longValue();

  /**
   * Rolls CRC64 calculation.
   *
   * <p> {@code CRC64(data) = -1 ^ updateCrc64((... updateCrc64(-1, firstBlock), ...), lastBlock);}
   * <p> This simple and reliable checksum is chosen to make is easy to calculate the same value
   * across the variety of languages (C++, Java, Go, ...).
   */
  private static long updateCrc64(long crc, byte[] data, int offset, int length) {
    for (int i = offset; i < offset + length; ++i) {
      long c = (crc ^ (long) (data[i] & 0xFF)) & 0xFF;
      for (int k = 0; k < 8; k++) {
        c = ((c & 1) == 1) ? CRC_64_POLY ^ (c >>> 1) : c >>> 1;
      }
      crc = c ^ (crc >>> 8);
    }
    return crc;
  }

  private long decompressAndCalculateCrc(ZipInputStream input) throws IOException {
    /* Do not allow entry readers to close the whole ZipInputStream. */
    FilterInputStream entryStream = new FilterInputStream(input) {
      @Override
      public void close() {}
    };

    long crc = -1;
    byte[] buffer = new byte[65536];
    BrotliInputStream decompressedStream = new BrotliInputStream(entryStream);
    while (true) {
      int len = decompressedStream.read(buffer);
      if (len <= 0) {
        break;
      }
      crc = updateCrc64(crc, buffer, 0, len);
    }
    decompressedStream.close();
    return crc ^ -1;
  }

  @Override
  public void run() {
    String entryName = "";
    ZipInputStream zis = new ZipInputStream(input);
    try {
      int entryIndex = 0;
      ZipEntry entry = null;
      int jobIndex = nextJob.getAndIncrement();
      while ((entry = zis.getNextEntry()) != null) {
        if (entry.isDirectory()) {
          continue;
        }
        if (entryIndex++ != jobIndex) {
          zis.closeEntry();
          continue;
        }
        entryName = entry.getName();
        int dotIndex = entryName.indexOf('.');
        String entryCrcString = (dotIndex == -1) ? entryName : entryName.substring(0, dotIndex);
        long entryCrc = new BigInteger(entryCrcString, 16).longValue();
        try {
          if (entryCrc != decompressAndCalculateCrc(zis) && !sanityCheck) {
            throw new RuntimeException("CRC mismatch");
          }
        } catch (IOException iox) {
          if (!sanityCheck) {
            throw new RuntimeException("Decompression failed", iox);
          }
        }
        zis.closeEntry();
        entryName = "";
        jobIndex = nextJob.getAndIncrement();
      }
      zis.close();
      input.close();
    } catch (Throwable ex) {
      throw new RuntimeException(entryName, ex);
    }
  }

  public static void main(String[] args) throws FileNotFoundException {
    int argsOffset = 0;
    boolean sanityCheck = false;
    if (args.length != 0) {
      if (args[0].equals("-s")) {
        sanityCheck = true;
        argsOffset = 1;
      }
    }
    if (args.length == argsOffset) {
      throw new RuntimeException("Usage: BundleChecker [-s] <fileX.zip> ...");
    }
    for (int i = argsOffset; i < args.length; ++i) {
      new BundleChecker(new FileInputStream(args[i]), new AtomicInteger(0), sanityCheck).run();
    }
  }
}
