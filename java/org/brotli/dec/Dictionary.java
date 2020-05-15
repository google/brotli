/* Copyright 2015 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

package org.brotli.dec;

import java.nio.ByteBuffer;

/**
 * Collection of static dictionary words.
 *
 * <p>Dictionary content is loaded from binary resource when {@link #getData()} is executed for the
 * first time. Consequently, it saves memory and CPU in case dictionary is not required.
 *
 * <p>One possible drawback is that multiple threads that need dictionary data may be blocked (only
 * once in each classworld). To avoid this, it is enough to call {@link #getData()} proactively.
 */
public final class Dictionary {
  static final int MIN_DICTIONARY_WORD_LENGTH = 4;
  static final int MAX_DICTIONARY_WORD_LENGTH = 31;

  private static ByteBuffer data;
  static final int[] offsets = new int[32];
  static final int[] sizeBits = new int[32];

  private static class DataLoader {
    static final boolean OK;

    static {
      boolean ok = true;
      try {
        Class.forName(Dictionary.class.getPackage().getName() + ".DictionaryData");
      } catch (Throwable ex) {
        ok = false;
      }
      OK = ok;
    }
  }

  public static void setData(ByteBuffer data, int[] sizeBits) {
    if (!data.isDirect() || !data.isReadOnly()) {
      throw new BrotliRuntimeException("data must be a direct read-only byte buffer");
    }
    // TODO: is that so?
    if (sizeBits.length > MAX_DICTIONARY_WORD_LENGTH) {
      throw new BrotliRuntimeException(
          "sizeBits length must be at most " + MAX_DICTIONARY_WORD_LENGTH);
    }
    for (int i = 0; i < MIN_DICTIONARY_WORD_LENGTH; ++i) {
      if (sizeBits[i] != 0) {
        throw new BrotliRuntimeException("first " + MIN_DICTIONARY_WORD_LENGTH + " must be 0");
      }
    }
    int[] dictionaryOffsets = Dictionary.offsets;
    int[] dictionarySizeBits = Dictionary.sizeBits;
    System.arraycopy(sizeBits, 0, dictionarySizeBits, 0, sizeBits.length);
    int pos = 0;
    int limit = data.capacity();
    for (int i = 0; i < sizeBits.length; ++i) {
      dictionaryOffsets[i] = pos;
      int bits = dictionarySizeBits[i];
      if (bits != 0) {
        if (bits >= 31) {
          throw new BrotliRuntimeException("sizeBits values must be less than 31");
        }
        pos += i << bits;
        if (pos <= 0 || pos > limit) {
          throw new BrotliRuntimeException("sizeBits is inconsistent: overflow");
        }
      }
    }
    for (int i = sizeBits.length; i < 32; ++i) {
      dictionaryOffsets[i] = pos;
    }
    if (pos != limit) {
      throw new BrotliRuntimeException("sizeBits is inconsistent: underflow");
    }
    Dictionary.data = data;
  }

  public static ByteBuffer getData() {
    if (data != null) {
      return data;
    }
    if (!DataLoader.OK) {
      throw new BrotliRuntimeException("brotli dictionary is not set");
    }
    /* Might have been set when {@link DictionaryData} was loaded.*/
    return data;
  }
}
