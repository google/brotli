/* Copyright 2025 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

package org.brotli.dec;

/** Possible errors from decoder. */
public final class BrotliError {
  /** Success; anything greater is also success. */
  public static final int BROTLI_OK = 0;
  /** Success; decoder has finished decompressing the input. */
  public static final int BROTLI_OK_DONE = BROTLI_OK + 1;
  /** Success; decoder has more output to produce. */
  public static final int BROTLI_OK_NEED_MORE_OUTPUT = BROTLI_OK + 2;

  /** Error code threshold; actual error codes are LESS than -1! */
  public static final int BROTLI_ERROR = -1;
  /** Stream error: corrupted code length table. */
  public static final int BROTLI_ERROR_CORRUPTED_CODE_LENGTH_TABLE = BROTLI_ERROR - 1;
  /** Stream error: corrupted context map. */
  public static final int BROTLI_ERROR_CORRUPTED_CONTEXT_MAP = BROTLI_ERROR - 2;
  /** Stream error: corrupted Huffman code histogram. */
  public static final int BROTLI_ERROR_CORRUPTED_HUFFMAN_CODE_HISTOGRAM = BROTLI_ERROR - 3;
  /** Stream error: corrupted padding bits. */
  public static final int BROTLI_ERROR_CORRUPTED_PADDING_BITS = BROTLI_ERROR - 4;
  /** Stream error: corrupted reserved bit. */
  public static final int BROTLI_ERROR_CORRUPTED_RESERVED_BIT = BROTLI_ERROR - 5;
  /** Stream error: duplicate simple Huffman symbol. */
  public static final int BROTLI_ERROR_DUPLICATE_SIMPLE_HUFFMAN_SYMBOL = BROTLI_ERROR - 6;
  /** Stream error: exuberant nibble. */
  public static final int BROTLI_ERROR_EXUBERANT_NIBBLE = BROTLI_ERROR - 7;
  /** Stream error: invalid backward reference. */
  public static final int BROTLI_ERROR_INVALID_BACKWARD_REFERENCE = BROTLI_ERROR - 8;
  /** Stream error: invalid metablock length. */
  public static final int BROTLI_ERROR_INVALID_METABLOCK_LENGTH = BROTLI_ERROR - 9;
  /** Stream error: invalid window bits. */
  public static final int BROTLI_ERROR_INVALID_WINDOW_BITS = BROTLI_ERROR - 10;
  /** Stream error: negative distance. */
  public static final int BROTLI_ERROR_NEGATIVE_DISTANCE = BROTLI_ERROR - 11;
  /** Stream error: read after end of input buffer. */
  public static final int BROTLI_ERROR_READ_AFTER_END = BROTLI_ERROR - 12;
  /** IO error: read failed. */
  public static final int BROTLI_ERROR_READ_FAILED = BROTLI_ERROR - 13;
  /** IO error: symbol out of range. */
  public static final int BROTLI_ERROR_SYMBOL_OUT_OF_RANGE = BROTLI_ERROR - 14;
  /** Stream error: truncated input. */
  public static final int BROTLI_ERROR_TRUNCATED_INPUT = BROTLI_ERROR - 15;
  /** Stream error: unused bytes after end of stream. */
  public static final int BROTLI_ERROR_UNUSED_BYTES_AFTER_END = BROTLI_ERROR - 16;
  /** Stream error: unused Huffman space. */
  public static final int BROTLI_ERROR_UNUSED_HUFFMAN_SPACE = BROTLI_ERROR - 17;

  /** Exception code threshold. */
  public static final int BROTLI_PANIC = -21;
  /** Exception: stream is already closed. */
  public static final int BROTLI_PANIC_ALREADY_CLOSED = BROTLI_PANIC - 1;
  /** Exception: max distance is too small. */
  public static final int BROTLI_PANIC_MAX_DISTANCE_TOO_SMALL = BROTLI_PANIC - 2;
  /** Exception: state is not fresh. */
  public static final int BROTLI_PANIC_STATE_NOT_FRESH = BROTLI_PANIC - 3;
  /** Exception: state is not initialized. */
  public static final int BROTLI_PANIC_STATE_NOT_INITIALIZED = BROTLI_PANIC - 4;
  /** Exception: state is not uninitialized. */
  public static final int BROTLI_PANIC_STATE_NOT_UNINITIALIZED = BROTLI_PANIC - 5;
  /** Exception: too many dictionary chunks. */
  public static final int BROTLI_PANIC_TOO_MANY_DICTIONARY_CHUNKS = BROTLI_PANIC - 6;
  /** Exception: unexpected state. */
  public static final int BROTLI_PANIC_UNEXPECTED_STATE = BROTLI_PANIC - 7;
  /** Exception: unreachable code. */
  public static final int BROTLI_PANIC_UNREACHABLE = BROTLI_PANIC - 8;
  /** Exception: unaligned copy bytes. */
  public static final int BROTLI_PANIC_UNALIGNED_COPY_BYTES = BROTLI_PANIC - 9;

  /** Non-instantiable. */
  private BrotliError() {}
}
