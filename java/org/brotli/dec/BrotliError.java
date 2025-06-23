/* Copyright 2025 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

package org.brotli.dec;

/**
 * Possible errors from decoder.
 */
public class BrotliError {
  public static final int BROTLI_OK = 0;
  public static final int BROTLI_OK_DONE = BROTLI_OK + 1;
  public static final int BROTLI_OK_NEED_MORE_OUTPUT = BROTLI_OK + 2;

  // It is important that actual error codes are LESS than -1!
  public static final int BROTLI_ERROR = -1;
  public static final int BROTLI_ERROR_CORRUPTED_CODE_LENGTH_TABLE = BROTLI_ERROR - 1;
  public static final int BROTLI_ERROR_CORRUPTED_CONTEXT_MAP = BROTLI_ERROR - 2;
  public static final int BROTLI_ERROR_CORRUPTED_HUFFMAN_CODE_HISTOGRAM = BROTLI_ERROR - 3;
  public static final int BROTLI_ERROR_CORRUPTED_PADDING_BITS = BROTLI_ERROR - 4;
  public static final int BROTLI_ERROR_CORRUPTED_RESERVED_BIT = BROTLI_ERROR - 5;
  public static final int BROTLI_ERROR_DUPLICATE_SIMPLE_HUFFMAN_SYMBOL = BROTLI_ERROR - 6;
  public static final int BROTLI_ERROR_EXUBERANT_NIBBLE = BROTLI_ERROR - 7;
  public static final int BROTLI_ERROR_INVALID_BACKWARD_REFERENCE = BROTLI_ERROR - 8;
  public static final int BROTLI_ERROR_INVALID_METABLOCK_LENGTH = BROTLI_ERROR - 9;
  public static final int BROTLI_ERROR_INVALID_WINDOW_BITS = BROTLI_ERROR - 10;
  public static final int BROTLI_ERROR_NEGATIVE_DISTANCE = BROTLI_ERROR - 11;
  public static final int BROTLI_ERROR_READ_AFTER_END = BROTLI_ERROR - 12;
  public static final int BROTLI_ERROR_READ_FAILED = BROTLI_ERROR - 13;
  public static final int BROTLI_ERROR_SYMBOL_OUT_OF_RANGE = BROTLI_ERROR - 14;
  public static final int BROTLI_ERROR_TRUNCATED_INPUT = BROTLI_ERROR - 15;
  public static final int BROTLI_ERROR_UNUSED_BYTES_AFTER_END = BROTLI_ERROR - 16;
  public static final int BROTLI_ERROR_UNUSED_HUFFMAN_SPACE = BROTLI_ERROR - 17;

  public static final int BROTLI_PANIC = -21;
  public static final int BROTLI_PANIC_ALREADY_CLOSED = BROTLI_PANIC - 1;
  public static final int BROTLI_PANIC_MAX_DISTANCE_TOO_SMALL = BROTLI_PANIC - 2;
  public static final int BROTLI_PANIC_STATE_NOT_FRESH = BROTLI_PANIC - 3;
  public static final int BROTLI_PANIC_STATE_NOT_INITIALIZED = BROTLI_PANIC - 4;
  public static final int BROTLI_PANIC_STATE_NOT_UNINITIALIZED = BROTLI_PANIC - 5;
  public static final int BROTLI_PANIC_TOO_MANY_DICTIONARY_CHUNKS = BROTLI_PANIC - 6;
  public static final int BROTLI_PANIC_UNEXPECTED_STATE = BROTLI_PANIC - 7;
  public static final int BROTLI_PANIC_UNREACHABLE = BROTLI_PANIC - 8;
  public static final int BROTLI_PANIC_UNALIGNED_COPY_BYTES = BROTLI_PANIC - 9;
}
