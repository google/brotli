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

  private static ByteBuffer data = ByteBuffer.allocateDirect(0);
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

  private static final int DICTIONARY_DEBUG = Utils.isDebugMode();

  /** Initialize static dictionary. */
  public static void setData(ByteBuffer newData, int[] newSizeBits) {
    if (DICTIONARY_DEBUG != 0) {
      if ((Utils.isDirect(newData) == 0) || (Utils.isReadOnly(newData) == 0)) {
        throw new BrotliRuntimeException("newData must be a direct read-only byte buffer");
      }
      // TODO: is that so?
      if (newSizeBits.length > MAX_DICTIONARY_WORD_LENGTH) {
        throw new BrotliRuntimeException(
            "sizeBits length must be at most " + String.valueOf(MAX_DICTIONARY_WORD_LENGTH));
      }
      for (int i = 0; i < MIN_DICTIONARY_WORD_LENGTH; ++i) {
        if (newSizeBits[i] != 0) {
          throw new BrotliRuntimeException(
              "first " + String.valueOf(MIN_DICTIONARY_WORD_LENGTH) + " must be 0");
        }
      }
    }
    final int[] dictionaryOffsets = Dictionary.offsets;
    final int[] dictionarySizeBits = Dictionary.sizeBits;
    for (int i = 0; i < newSizeBits.length; ++i) {
      dictionarySizeBits[i] = newSizeBits[i];
    }
    int pos = 0;
    for (int i = 0; i < newSizeBits.length; ++i) {
      dictionaryOffsets[i] = pos;
      final int bits = dictionarySizeBits[i];
      if (bits != 0) {
        pos += i << (bits & 31);
        if (DICTIONARY_DEBUG != 0) {
          if (bits >= 31) {
            throw new BrotliRuntimeException("newSizeBits values must be less than 31");
          }
          if (pos <= 0 || pos > newData.capacity()) {
            throw new BrotliRuntimeException("newSizeBits is inconsistent: overflow");
          }
        }
      }
    }
    for (int i = newSizeBits.length; i < 32; ++i) {
      dictionaryOffsets[i] = pos;
    }
    if (DICTIONARY_DEBUG != 0) {
      if (pos != newData.capacity()) {
        throw new BrotliRuntimeException("newSizeBits is inconsistent: underflow");
      }
    }
    Dictionary.data = newData;
  }

  /** Access static dictionary. */
  public static ByteBuffer getData() {
    if (data.capacity() != 0) {
      return data;
    }
    if (!DataLoader.OK) {
      throw new BrotliRuntimeException("brotli dictionary is not set");
    }
    /* Might have been set when {@link DictionaryData} was loaded.*/
    return data;
  }

  /** Non-instantiable. */
  private Dictionary() {}
}
