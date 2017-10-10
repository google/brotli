/* Copyright 2017 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

package org.brotli.wrapper.dec;

import java.io.IOException;
import java.nio.ByteBuffer;

/**
 * JNI wrapper for brotli decoder.
 */
class DecoderJNI {
  private static native ByteBuffer nativeCreate(long[] context);
  private static native void nativePush(long[] context, int length);
  private static native ByteBuffer nativePull(long[] context);
  private static native void nativeDestroy(long[] context);

  enum Status {
    ERROR,
    DONE,
    NEEDS_MORE_INPUT,
    NEEDS_MORE_OUTPUT,
    OK
  };

  static class Wrapper {
    private final long[] context = new long[2];
    private final ByteBuffer inputBuffer;
    private Status lastStatus = Status.NEEDS_MORE_INPUT;

    Wrapper(int inputBufferSize) throws IOException {
      this.context[1] = inputBufferSize;
      this.inputBuffer = nativeCreate(this.context);
      if (this.context[0] == 0) {
        throw new IOException("failed to initialize native brotli decoder");
      }
    }

    void push(int length) {
      if (length < 0) {
        throw new IllegalArgumentException("negative block length");
      }
      if (context[0] == 0) {
        throw new IllegalStateException("brotli decoder is already destroyed");
      }
      if (lastStatus != Status.NEEDS_MORE_INPUT && lastStatus != Status.OK) {
        throw new IllegalStateException("pushing input to decoder in " + lastStatus + " state");
      }
      if (lastStatus == Status.OK && length != 0) {
        throw new IllegalStateException("pushing input to decoder in OK state");
      }
      nativePush(context, length);
      parseStatus();
    }

    private void parseStatus() {
      long status = context[1];
      if (status == 1) {
        lastStatus = Status.DONE;
      } else if (status == 2) {
        lastStatus = Status.NEEDS_MORE_INPUT;
      } else if (status == 3) {
        lastStatus = Status.NEEDS_MORE_OUTPUT;
      } else if (status == 4) {
        lastStatus = Status.OK;
      } else {
        lastStatus = Status.ERROR;
      }
    }

    Status getStatus() {
      return lastStatus;
    }

    ByteBuffer getInputBuffer() {
      return inputBuffer;
    }

    ByteBuffer pull() {
      if (context[0] == 0) {
        throw new IllegalStateException("brotli decoder is already destroyed");
      }
      if (lastStatus != Status.NEEDS_MORE_OUTPUT) {
        throw new IllegalStateException("pulling output from decoder in " + lastStatus + " state");
      }
      ByteBuffer result = nativePull(context);
      parseStatus();
      return result;
    }

    /**
     * Releases native resources.
     */
    void destroy() {
      if (context[0] == 0) {
        throw new IllegalStateException("brotli decoder is already destroyed");
      }
      nativeDestroy(context);
      context[0] = 0;
    }

    @Override
    protected void finalize() throws Throwable {
      if (context[0] != 0) {
        /* TODO: log resource leak? */
        destroy();
      }
      super.finalize();
    }
  }
}
