package org.brotli.dec;

import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

public class Decoder {
  private static long decodeBytes(InputStream input, OutputStream output, byte[] buffer)
      throws IOException {
    long totalOut = 0;
    int readBytes;
    BrotliInputStream in = new BrotliInputStream(input);
    in.enableLargeWindow();
    try {
      while ((readBytes = in.read(buffer)) >= 0) {
        output.write(buffer, 0, readBytes);
        totalOut += readBytes;
      }
    } finally {
      in.close();
    }
    return totalOut;
  }

  public static void main(String... args) throws IOException {
    if (args.length != 2) {
      System.out.println("Usage: decoder <compressed_in> <decompressed_out>");
      return;
    }

    byte[] buffer = new byte[1024 * 1024];
    long start;
    long bytesDecoded;
    long end;
    InputStream in = null;
    OutputStream out = null;
    try {
      in = new FileInputStream(args[0]);
      out = new FileOutputStream(args[1]);
      start = System.nanoTime();
      bytesDecoded = decodeBytes(in, out, buffer);
      end = System.nanoTime();
    } finally {
      if (in != null) {
        in.close();  // Hopefully, does not throw exception.
      }
      if (out != null) {
        out.close();
      }
    }

    double timeDelta = (end - start) / 1000000000.0;
    if (timeDelta <= 0) {
      return;
    }
    double mbDecoded = bytesDecoded / (1024.0 * 1024.0);
    System.out.println(mbDecoded / timeDelta + " MiB/s");
  }
}
