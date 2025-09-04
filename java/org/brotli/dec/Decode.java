/* Copyright 2015 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

package org.brotli.dec;

import static org.brotli.dec.BrotliError.BROTLI_ERROR_CORRUPTED_CODE_LENGTH_TABLE;
import static org.brotli.dec.BrotliError.BROTLI_ERROR_CORRUPTED_CONTEXT_MAP;
import static org.brotli.dec.BrotliError.BROTLI_ERROR_CORRUPTED_HUFFMAN_CODE_HISTOGRAM;
import static org.brotli.dec.BrotliError.BROTLI_ERROR_CORRUPTED_RESERVED_BIT;
import static org.brotli.dec.BrotliError.BROTLI_ERROR_DUPLICATE_SIMPLE_HUFFMAN_SYMBOL;
import static org.brotli.dec.BrotliError.BROTLI_ERROR_EXUBERANT_NIBBLE;
import static org.brotli.dec.BrotliError.BROTLI_ERROR_INVALID_BACKWARD_REFERENCE;
import static org.brotli.dec.BrotliError.BROTLI_ERROR_INVALID_METABLOCK_LENGTH;
import static org.brotli.dec.BrotliError.BROTLI_ERROR_INVALID_WINDOW_BITS;
import static org.brotli.dec.BrotliError.BROTLI_ERROR_NEGATIVE_DISTANCE;
import static org.brotli.dec.BrotliError.BROTLI_ERROR_SYMBOL_OUT_OF_RANGE;
import static org.brotli.dec.BrotliError.BROTLI_ERROR_UNUSED_HUFFMAN_SPACE;
import static org.brotli.dec.BrotliError.BROTLI_OK;
import static org.brotli.dec.BrotliError.BROTLI_OK_DONE;
import static org.brotli.dec.BrotliError.BROTLI_OK_NEED_MORE_OUTPUT;
import static org.brotli.dec.BrotliError.BROTLI_PANIC_ALREADY_CLOSED;
import static org.brotli.dec.BrotliError.BROTLI_PANIC_MAX_DISTANCE_TOO_SMALL;
import static org.brotli.dec.BrotliError.BROTLI_PANIC_STATE_NOT_FRESH;
import static org.brotli.dec.BrotliError.BROTLI_PANIC_STATE_NOT_INITIALIZED;
import static org.brotli.dec.BrotliError.BROTLI_PANIC_STATE_NOT_UNINITIALIZED;
import static org.brotli.dec.BrotliError.BROTLI_PANIC_TOO_MANY_DICTIONARY_CHUNKS;
import static org.brotli.dec.BrotliError.BROTLI_PANIC_UNEXPECTED_STATE;
import static org.brotli.dec.BrotliError.BROTLI_PANIC_UNREACHABLE;

import java.nio.ByteBuffer;

/**
 * API for Brotli decompression.
 */
final class Decode {

  static final int MIN_LARGE_WINDOW_BITS = 10;
  /* Maximum was chosen to be 30 to allow efficient decoder implementation.
   * Format allows bigger window, but Java does not support 2G+ arrays. */
  static final int MAX_LARGE_WINDOW_BITS = 30;

  //----------------------------------------------------------------------------
  // RunningState
  //----------------------------------------------------------------------------
  // NB: negative values are used for errors.
  private static final int UNINITIALIZED = 0;
  private static final int INITIALIZED = 1;
  private static final int BLOCK_START = 2;
  private static final int COMPRESSED_BLOCK_START = 3;
  private static final int MAIN_LOOP = 4;
  private static final int READ_METADATA = 5;
  private static final int COPY_UNCOMPRESSED = 6;
  private static final int INSERT_LOOP = 7;
  private static final int COPY_LOOP = 8;
  private static final int USE_DICTIONARY = 9;
  private static final int FINISHED = 10;
  private static final int CLOSED = 11;
  private static final int INIT_WRITE = 12;
  private static final int WRITE = 13;
  private static final int COPY_FROM_COMPOUND_DICTIONARY = 14;

  private static final int DEFAULT_CODE_LENGTH = 8;
  private static final int CODE_LENGTH_REPEAT_CODE = 16;
  private static final int NUM_LITERAL_CODES = 256;
  private static final int NUM_COMMAND_CODES = 704;
  private static final int NUM_BLOCK_LENGTH_CODES = 26;
  private static final int LITERAL_CONTEXT_BITS = 6;
  private static final int DISTANCE_CONTEXT_BITS = 2;

  private static final int CD_BLOCK_MAP_BITS = 8;
  private static final int HUFFMAN_TABLE_BITS = 8;
  private static final int HUFFMAN_TABLE_MASK = 0xFF;

  /**
   * Maximum possible Huffman table size for an alphabet size of (index * 32),
   * max code length 15 and root table bits 8.
   * The biggest alphabet is "command" - 704 symbols. Though "distance" alphabet could theoretically
   * outreach that limit (for 62 extra bit distances), practically it is limited by
   * MAX_ALLOWED_DISTANCE and never gets bigger than 544 symbols.
   */
  static final int[] MAX_HUFFMAN_TABLE_SIZE = {
      256, 402, 436, 468, 500, 534, 566, 598, 630, 662, 694, 726, 758, 790, 822,
      854, 886, 920, 952, 984, 1016, 1048, 1080
  };

  private static final int HUFFMAN_TABLE_SIZE_26 = 396;
  private static final int HUFFMAN_TABLE_SIZE_258 = 632;

  private static final int CODE_LENGTH_CODES = 18;
  private static final int[] CODE_LENGTH_CODE_ORDER = {
      1, 2, 3, 4, 0, 5, 17, 6, 16, 7, 8, 9, 10, 11, 12, 13, 14, 15,
  };

  private static final int NUM_DISTANCE_SHORT_CODES = 16;
  private static final int[] DISTANCE_SHORT_CODE_INDEX_OFFSET = {
    0, 3, 2, 1, 0, 0, 0, 0, 0, 0, 3, 3, 3, 3, 3, 3
  };

  private static final int[] DISTANCE_SHORT_CODE_VALUE_OFFSET = {
      0, 0, 0, 0, -1, 1, -2, 2, -3, 3, -1, 1, -2, 2, -3, 3
  };

  /**
   * Static Huffman code for the code length code lengths.
   */
  private static final int[] FIXED_TABLE = {
      0x020000, 0x020004, 0x020003, 0x030002, 0x020000, 0x020004, 0x020003, 0x040001,
      0x020000, 0x020004, 0x020003, 0x030002, 0x020000, 0x020004, 0x020003, 0x040005
  };

  // TODO(eustas): generalize.
  static final int MAX_TRANSFORMED_WORD_LENGTH = 5 + 24 + 8;

  private static final int MAX_DISTANCE_BITS = 24;
  private static final int MAX_LARGE_WINDOW_DISTANCE_BITS = 62;

  /**
   * Safe distance limit.
   *
   * Limit ((1 << 31) - 4) allows safe distance calculation without overflows,
   * given the distance alphabet size is limited to corresponding size.
   */
  private static final int MAX_ALLOWED_DISTANCE = 0x7FFFFFFC;

  //----------------------------------------------------------------------------
  // Prefix code LUT.
  //----------------------------------------------------------------------------
  static final int[] BLOCK_LENGTH_OFFSET = {
      1, 5, 9, 13, 17, 25, 33, 41, 49, 65, 81, 97, 113, 145, 177, 209, 241, 305, 369, 497,
      753, 1265, 2289, 4337, 8433, 16625
  };

  static final int[] BLOCK_LENGTH_N_BITS = {
      2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 7, 8, 9, 10, 11, 12, 13, 24
  };

  static final short[] INSERT_LENGTH_N_BITS = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x02, 0x02, 0x03, 0x03,
      0x04, 0x04, 0x05, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0C, 0x0E, 0x18
  };

  static final short[] COPY_LENGTH_N_BITS = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x02, 0x02,
      0x03, 0x03, 0x04, 0x04, 0x05, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x18
  };

  // Each command is represented with 4x16-bit values:
  //  * [insertLenExtraBits, copyLenExtraBits]
  //  * insertLenOffset
  //  * copyLenOffset
  //  * distanceContext
  static final short[] CMD_LOOKUP = new short[NUM_COMMAND_CODES * 4];

  static {
    unpackCommandLookupTable(CMD_LOOKUP);
  }

  private static int log2floor(int i) {
    // REQUIRED: i > 0
    int result = -1;
    int step = 16;
    int v = i;
    while (step > 0) {
      int next = v >> step;
      if (next != 0) {
        result += step;
        v = next;
      }
      step = step >> 1;
    }
    return result + v;
  }

  private static int calculateDistanceAlphabetSize(int npostfix, int ndirect, int maxndistbits) {
    return NUM_DISTANCE_SHORT_CODES + ndirect + 2 * (maxndistbits << npostfix);
  }

  // TODO(eustas): add a correctness test for this function when
  //               large-window and dictionary are implemented.
  private static int calculateDistanceAlphabetLimit(State s, int maxDistance, int npostfix, int ndirect) {
    if (maxDistance < ndirect + (2 << npostfix)) {
      return Utils.makeError(s, BROTLI_PANIC_MAX_DISTANCE_TOO_SMALL);
    }
    final int offset = ((maxDistance - ndirect) >> npostfix) + 4;
    final int ndistbits = log2floor(offset) - 1;
    final int group = ((ndistbits - 1) << 1) | ((offset >> ndistbits) & 1);
    return ((group - 1) << npostfix) + (1 << npostfix) + ndirect + NUM_DISTANCE_SHORT_CODES;
  }

  private static void unpackCommandLookupTable(short[] cmdLookup) {
    final int[] insertLengthOffsets = new int[24];
    final int[] copyLengthOffsets = new int[24];
    copyLengthOffsets[0] = 2;
    for (int i = 0; i < 23; ++i) {
      insertLengthOffsets[i + 1] = insertLengthOffsets[i] + (1 << (int) INSERT_LENGTH_N_BITS[i]);
      copyLengthOffsets[i + 1] = copyLengthOffsets[i] + (1 << (int) COPY_LENGTH_N_BITS[i]);
    }

    for (int cmdCode = 0; cmdCode < NUM_COMMAND_CODES; ++cmdCode) {
      int rangeIdx = cmdCode >> 6;
      /* -4 turns any regular distance code to negative. */
      int distanceContextOffset = -4;
      if (rangeIdx >= 2) {
        rangeIdx -= 2;
        distanceContextOffset = 0;
      }
      final int insertCode = (((0x29850 >> (rangeIdx * 2)) & 0x3) << 3) | ((cmdCode >> 3) & 7);
      final int copyCode = (((0x26244 >> (rangeIdx * 2)) & 0x3) << 3) | (cmdCode & 7);
      final int copyLengthOffset = copyLengthOffsets[copyCode];
      final int distanceContext = distanceContextOffset + Utils.min(copyLengthOffset, 5) - 2;
      final int index = cmdCode * 4;
      cmdLookup[index + 0] =
          (short)
              ((int) INSERT_LENGTH_N_BITS[insertCode] | ((int) COPY_LENGTH_N_BITS[copyCode] << 8));
      cmdLookup[index + 1] = (short) insertLengthOffsets[insertCode];
      cmdLookup[index + 2] = (short) copyLengthOffsets[copyCode];
      cmdLookup[index + 3] = (short) distanceContext;
    }
  }

  /**
   * Reads brotli stream header and parses "window bits".
   *
   * @param s initialized state, before any read is performed.
   * @return -1 if header is invalid
   */
  private static int decodeWindowBits(State s) {
    /* Change the meaning of flag. Before that step it means "decoder must be capable of reading
     * "large-window" brotli stream. After this step it means that "large-window" feature
     * is actually detected. Despite the window size could be same as before (lgwin = 10..24),
     * encoded distances are allowed to be much greater, thus bigger dictionary could be used. */
    final int largeWindowEnabled = s.isLargeWindow;
    s.isLargeWindow = 0;

    BitReader.fillBitWindow(s);
    if (BitReader.readFewBits(s, 1) == 0) {
      return 16;
    }
    int n = BitReader.readFewBits(s, 3);
    if (n != 0) {
      return 17 + n;
    }
    n = BitReader.readFewBits(s, 3);
    if (n != 0) {
      if (n == 1) {
        if (largeWindowEnabled == 0) {
          /* Reserved value in regular brotli stream. */
          return -1;
        }
        s.isLargeWindow = 1;
        /* Check "reserved" bit for future (post-large-window) extensions. */
        if (BitReader.readFewBits(s, 1) == 1) {
          return -1;
        }
        n = BitReader.readFewBits(s, 6);
        if (n < MIN_LARGE_WINDOW_BITS || n > MAX_LARGE_WINDOW_BITS) {
          /* Encoded window bits value is too small or too big. */
          return -1;
        }
        return n;
      }
      return 8 + n;
    }
    return 17;
  }

  /**
   * Switch decoder to "eager" mode.
   *
   * In "eager" mode decoder returns as soon as there is enough data to fill output buffer.
   *
   * @param s initialized state, before any read is performed.
   */
  static int enableEagerOutput(State s) {
    if (s.runningState != INITIALIZED) {
      return Utils.makeError(s, BROTLI_PANIC_STATE_NOT_FRESH);
    }
    s.isEager = 1;
    return BROTLI_OK;
  }

  static int enableLargeWindow(State s) {
    if (s.runningState != INITIALIZED) {
      return Utils.makeError(s, BROTLI_PANIC_STATE_NOT_FRESH);
    }
    s.isLargeWindow = 1;
    return BROTLI_OK;
  }

  // TODO(eustas): do we need byte views?
  static int attachDictionaryChunk(State s, byte[] data) {
    if (s.runningState != INITIALIZED) {
      return Utils.makeError(s, BROTLI_PANIC_STATE_NOT_FRESH);
    }
    if (s.cdNumChunks == 0) {
      s.cdChunks = new byte[16][];
      s.cdChunkOffsets = new int[16];
      s.cdBlockBits = -1;
    }
    if (s.cdNumChunks == 15) {
      return Utils.makeError(s, BROTLI_PANIC_TOO_MANY_DICTIONARY_CHUNKS);
    }
    s.cdChunks[s.cdNumChunks] = data;
    s.cdNumChunks++;
    s.cdTotalSize += data.length;
    s.cdChunkOffsets[s.cdNumChunks] = s.cdTotalSize;
    return BROTLI_OK;
  }

  /**
   * Associate input with decoder state.
   *
   * @param s uninitialized state without associated input
   */
  static int initState(State s) {
    if (s.runningState != UNINITIALIZED) {
      return Utils.makeError(s, BROTLI_PANIC_STATE_NOT_UNINITIALIZED);
    }
    /* 6 trees + 1 extra "offset" slot to simplify table decoding logic. */
    s.blockTrees = new int[7 + 3 * (HUFFMAN_TABLE_SIZE_258 + HUFFMAN_TABLE_SIZE_26)];
    s.blockTrees[0] = 7;
    s.distRbIdx = 3;
    int result = calculateDistanceAlphabetLimit(s, MAX_ALLOWED_DISTANCE, 3, 15 << 3);
    if (result < BROTLI_OK) {
      return result;
    }
    final int maxDistanceAlphabetLimit = result;
    s.distExtraBits = new byte[maxDistanceAlphabetLimit];
    s.distOffset = new int[maxDistanceAlphabetLimit];
    result = BitReader.initBitReader(s);
    if (result < BROTLI_OK) {
      return result;
    }
    s.runningState = INITIALIZED;
    return BROTLI_OK;
  }

  static int close(State s) {
    if (s.runningState == UNINITIALIZED) {
      return Utils.makeError(s, BROTLI_PANIC_STATE_NOT_INITIALIZED);
    }
    if (s.runningState > 0) {
      s.runningState = CLOSED;
    }
    return BROTLI_OK;
  }

  /**
   * Decodes a number in the range [0..255], by reading 1 - 11 bits.
   */
  private static int decodeVarLenUnsignedByte(State s) {
    BitReader.fillBitWindow(s);
    if (BitReader.readFewBits(s, 1) != 0) {
      final int n = BitReader.readFewBits(s, 3);
      if (n == 0) {
        return 1;
      }
      return BitReader.readFewBits(s, n) + (1 << n);
    }
    return 0;
  }

  private static int decodeMetaBlockLength(State s) {
    BitReader.fillBitWindow(s);
    s.inputEnd = BitReader.readFewBits(s, 1);
    s.metaBlockLength = 0;
    s.isUncompressed = 0;
    s.isMetadata = 0;
    if ((s.inputEnd != 0) && BitReader.readFewBits(s, 1) != 0) {
      return BROTLI_OK;
    }
    final int sizeNibbles = BitReader.readFewBits(s, 2) + 4;
    if (sizeNibbles == 7) {
      s.isMetadata = 1;
      if (BitReader.readFewBits(s, 1) != 0) {
        return Utils.makeError(s, BROTLI_ERROR_CORRUPTED_RESERVED_BIT);
      }
      final int sizeBytes = BitReader.readFewBits(s, 2);
      if (sizeBytes == 0) {
        return BROTLI_OK;
      }
      for (int i = 0; i < sizeBytes; ++i) {
        BitReader.fillBitWindow(s);
        final int bits = BitReader.readFewBits(s, 8);
        if (bits == 0 && i + 1 == sizeBytes && sizeBytes > 1) {
          return Utils.makeError(s, BROTLI_ERROR_EXUBERANT_NIBBLE);
        }
        s.metaBlockLength += bits << (i * 8);
      }
    } else {
      for (int i = 0; i < sizeNibbles; ++i) {
        BitReader.fillBitWindow(s);
        final int bits = BitReader.readFewBits(s, 4);
        if (bits == 0 && i + 1 == sizeNibbles && sizeNibbles > 4) {
          return Utils.makeError(s, BROTLI_ERROR_EXUBERANT_NIBBLE);
        }
        s.metaBlockLength += bits << (i * 4);
      }
    }
    s.metaBlockLength++;
    if (s.inputEnd == 0) {
      s.isUncompressed = BitReader.readFewBits(s, 1);
    }
    return BROTLI_OK;
  }

  /**
   * Decodes the next Huffman code from bit-stream.
   */
  private static int readSymbol(int[] tableGroup, int tableIdx, State s) {
    int offset = tableGroup[tableIdx];
    final int v = BitReader.peekBits(s);
    offset += v & HUFFMAN_TABLE_MASK;
    final int bits = tableGroup[offset] >> 16;
    final int sym = tableGroup[offset] & 0xFFFF;
    if (bits <= HUFFMAN_TABLE_BITS) {
      s.bitOffset += bits;
      return sym;
    }
    offset += sym;
    final int mask = (1 << bits) - 1;
    offset += Utils.shr32(v & mask, HUFFMAN_TABLE_BITS);
    s.bitOffset += ((tableGroup[offset] >> 16) + HUFFMAN_TABLE_BITS);
    return tableGroup[offset] & 0xFFFF;
  }

  private static int readBlockLength(int[] tableGroup, int tableIdx, State s) {
    BitReader.fillBitWindow(s);
    final int code = readSymbol(tableGroup, tableIdx, s);
    final int n = BLOCK_LENGTH_N_BITS[code];
    BitReader.fillBitWindow(s);
    return BLOCK_LENGTH_OFFSET[code] + BitReader.readBits(s, n);
  }

  private static void moveToFront(int[] v, int index) {
    int i = index;
    final int value = v[i];
    while (i > 0) {
      v[i] = v[i - 1];
      i--;
    }
    v[0] = value;
  }

  private static void inverseMoveToFrontTransform(byte[] v, int vLen) {
    final int[] mtf = new int[256];
    for (int i = 0; i < 256; ++i) {
      mtf[i] = i;
    }
    for (int i = 0; i < vLen; ++i) {
      final int index = (int) v[i] & 0xFF;
      v[i] = (byte) mtf[index];
      if (index != 0) {
        moveToFront(mtf, index);
      }
    }
  }

  private static int readHuffmanCodeLengths(
      int[] codeLengthCodeLengths, int numSymbols, int[] codeLengths, State s) {
    int symbol = 0;
    int prevCodeLen = DEFAULT_CODE_LENGTH;
    int repeat = 0;
    int repeatCodeLen = 0;
    int space = 32768;
    final int[] table = new int[32 + 1];  /* Speculative single entry table group. */
    final int tableIdx = table.length - 1;
    Huffman.buildHuffmanTable(table, tableIdx, 5, codeLengthCodeLengths, CODE_LENGTH_CODES);

    while (symbol < numSymbols && space > 0) {
      if (s.halfOffset > BitReader.HALF_WATERLINE) {
        final int result = BitReader.readMoreInput(s);
        if (result < BROTLI_OK) {
          return result;
        }
      }
      BitReader.fillBitWindow(s);
      final int p = BitReader.peekBits(s) & 31;
      s.bitOffset += table[p] >> 16;
      final int codeLen = table[p] & 0xFFFF;
      if (codeLen < CODE_LENGTH_REPEAT_CODE) {
        repeat = 0;
        codeLengths[symbol++] = codeLen;
        if (codeLen != 0) {
          prevCodeLen = codeLen;
          space -= 32768 >> codeLen;
        }
      } else {
        final int extraBits = codeLen - 14;
        int newLen = 0;
        if (codeLen == CODE_LENGTH_REPEAT_CODE) {
          newLen = prevCodeLen;
        }
        if (repeatCodeLen != newLen) {
          repeat = 0;
          repeatCodeLen = newLen;
        }
        final int oldRepeat = repeat;
        if (repeat > 0) {
          repeat -= 2;
          repeat = repeat << extraBits;
        }
        BitReader.fillBitWindow(s);
        repeat += BitReader.readFewBits(s, extraBits) + 3;
        final int repeatDelta = repeat - oldRepeat;
        if (symbol + repeatDelta > numSymbols) {
          return Utils.makeError(s, BROTLI_ERROR_CORRUPTED_CODE_LENGTH_TABLE);
        }
        for (int i = 0; i < repeatDelta; ++i) {
          codeLengths[symbol++] = repeatCodeLen;
        }
        if (repeatCodeLen != 0) {
          space -= repeatDelta << (15 - repeatCodeLen);
        }
      }
    }
    if (space != 0) {
      return Utils.makeError(s, BROTLI_ERROR_UNUSED_HUFFMAN_SPACE);
    }
    // TODO(eustas): Pass max_symbol to Huffman table builder instead?
    Utils.fillIntsWithZeroes(codeLengths, symbol, numSymbols);
    return BROTLI_OK;
  }

  private static int checkDupes(State s, int[] symbols, int length) {
    for (int i = 0; i < length - 1; ++i) {
      for (int j = i + 1; j < length; ++j) {
        if (symbols[i] == symbols[j]) {
          return Utils.makeError(s, BROTLI_ERROR_DUPLICATE_SIMPLE_HUFFMAN_SYMBOL);
        }
      }
    }
    return BROTLI_OK;
  }

  /**
   * Reads up to 4 symbols directly and applies predefined histograms.
   */
  private static int readSimpleHuffmanCode(int alphabetSizeMax, int alphabetSizeLimit,
      int[] tableGroup, int tableIdx, State s) {
    // TODO(eustas): Avoid allocation?
    final int[] codeLengths = new int[alphabetSizeLimit];
    final int[] symbols = new int[4];

    final int maxBits = 1 + log2floor(alphabetSizeMax - 1);

    final int numSymbols = BitReader.readFewBits(s, 2) + 1;
    for (int i = 0; i < numSymbols; ++i) {
      BitReader.fillBitWindow(s);
      final int symbol = BitReader.readFewBits(s, maxBits);
      if (symbol >= alphabetSizeLimit) {
        return Utils.makeError(s, BROTLI_ERROR_SYMBOL_OUT_OF_RANGE);
      }
      symbols[i] = symbol;
    }
    final int result = checkDupes(s, symbols, numSymbols);
    if (result < BROTLI_OK) {
      return result;
    }

    int histogramId = numSymbols;
    if (numSymbols == 4) {
      histogramId += BitReader.readFewBits(s, 1);
    }

    switch (histogramId) {
      case 1:
        codeLengths[symbols[0]] = 1;
        break;

      case 2:
        codeLengths[symbols[0]] = 1;
        codeLengths[symbols[1]] = 1;
        break;

      case 3:
        codeLengths[symbols[0]] = 1;
        codeLengths[symbols[1]] = 2;
        codeLengths[symbols[2]] = 2;
        break;

      case 4:  // uniform 4-symbol histogram
        codeLengths[symbols[0]] = 2;
        codeLengths[symbols[1]] = 2;
        codeLengths[symbols[2]] = 2;
        codeLengths[symbols[3]] = 2;
        break;

      case 5:  // prioritized 4-symbol histogram
        codeLengths[symbols[0]] = 1;
        codeLengths[symbols[1]] = 2;
        codeLengths[symbols[2]] = 3;
        codeLengths[symbols[3]] = 3;
        break;

      default:
        break;
    }

    // TODO(eustas): Use specialized version?
    return Huffman.buildHuffmanTable(
        tableGroup, tableIdx, HUFFMAN_TABLE_BITS, codeLengths, alphabetSizeLimit);
  }

  // Decode Huffman-coded code lengths.
  private static int readComplexHuffmanCode(int alphabetSizeLimit, int skip,
      int[] tableGroup, int tableIdx, State s) {
    // TODO(eustas): Avoid allocation?
    final int[] codeLengths = new int[alphabetSizeLimit];
    final int[] codeLengthCodeLengths = new int[CODE_LENGTH_CODES];
    int space = 32;
    int numCodes = 0;
    for (int i = skip; i < CODE_LENGTH_CODES; ++i) {
      final int codeLenIdx = CODE_LENGTH_CODE_ORDER[i];
      BitReader.fillBitWindow(s);
      final int p = BitReader.peekBits(s) & 15;
      // TODO(eustas): Demultiplex FIXED_TABLE.
      s.bitOffset += FIXED_TABLE[p] >> 16;
      final int v = FIXED_TABLE[p] & 0xFFFF;
      codeLengthCodeLengths[codeLenIdx] = v;
      if (v != 0) {
        space -= (32 >> v);
        numCodes++;
        if (space <= 0) {
          break;
        }
      }
    }
    if (space != 0 && numCodes != 1) {
      return Utils.makeError(s, BROTLI_ERROR_CORRUPTED_HUFFMAN_CODE_HISTOGRAM);
    }

    final int result = readHuffmanCodeLengths(codeLengthCodeLengths, alphabetSizeLimit, codeLengths, s);
    if (result < BROTLI_OK) {
      return result;
    }

    return Huffman.buildHuffmanTable(
        tableGroup, tableIdx, HUFFMAN_TABLE_BITS, codeLengths, alphabetSizeLimit);
  }

  /**
   * Decodes Huffman table from bit-stream.
   *
   * @return number of slots used by resulting Huffman table
   */
  private static int readHuffmanCode(int alphabetSizeMax, int alphabetSizeLimit,
      int[] tableGroup, int tableIdx, State s) {
    if (s.halfOffset > BitReader.HALF_WATERLINE) {
      final int result = BitReader.readMoreInput(s);
      if (result < BROTLI_OK) {
        return result;
      }
    }
    BitReader.fillBitWindow(s);
    final int simpleCodeOrSkip = BitReader.readFewBits(s, 2);
    if (simpleCodeOrSkip == 1) {
      return readSimpleHuffmanCode(alphabetSizeMax, alphabetSizeLimit, tableGroup, tableIdx, s);
    }
    return readComplexHuffmanCode(alphabetSizeLimit, simpleCodeOrSkip, tableGroup, tableIdx, s);
  }

  private static int decodeContextMap(int contextMapSize, byte[] contextMap, State s) {
    int result;
    if (s.halfOffset > BitReader.HALF_WATERLINE) {
      result = BitReader.readMoreInput(s);
      if (result < BROTLI_OK) {
        return result;
      }
    }
    final int numTrees = decodeVarLenUnsignedByte(s) + 1;

    if (numTrees == 1) {
      Utils.fillBytesWithZeroes(contextMap, 0, contextMapSize);
      return numTrees;
    }

    BitReader.fillBitWindow(s);
    final int useRleForZeros = BitReader.readFewBits(s, 1);
    int maxRunLengthPrefix = 0;
    if (useRleForZeros != 0) {
      maxRunLengthPrefix = BitReader.readFewBits(s, 4) + 1;
    }
    final int alphabetSize = numTrees + maxRunLengthPrefix;
    final int tableSize = MAX_HUFFMAN_TABLE_SIZE[(alphabetSize + 31) >> 5];
    /* Speculative single entry table group. */
    final int[] table = new int[tableSize + 1];
    final int tableIdx = table.length - 1;
    result = readHuffmanCode(alphabetSize, alphabetSize, table, tableIdx, s);
    if (result < BROTLI_OK) {
      return result;
    }
    int i = 0;
    while (i < contextMapSize) {
      if (s.halfOffset > BitReader.HALF_WATERLINE) {
        result = BitReader.readMoreInput(s);
        if (result < BROTLI_OK) {
          return result;
        }
      }
      BitReader.fillBitWindow(s);
      final int code = readSymbol(table, tableIdx, s);
      if (code == 0) {
        contextMap[i] = 0;
        i++;
      } else if (code <= maxRunLengthPrefix) {
        BitReader.fillBitWindow(s);
        int reps = (1 << code) + BitReader.readFewBits(s, code);
        while (reps != 0) {
          if (i >= contextMapSize) {
            return Utils.makeError(s, BROTLI_ERROR_CORRUPTED_CONTEXT_MAP);
          }
          contextMap[i] = 0;
          i++;
          reps--;
        }
      } else {
        contextMap[i] = (byte) (code - maxRunLengthPrefix);
        i++;
      }
    }
    BitReader.fillBitWindow(s);
    if (BitReader.readFewBits(s, 1) == 1) {
      inverseMoveToFrontTransform(contextMap, contextMapSize);
    }
    return numTrees;
  }

  private static int decodeBlockTypeAndLength(State s, int treeType, int numBlockTypes) {
    final int[] ringBuffers = s.rings;
    final int offset = 4 + treeType * 2;
    BitReader.fillBitWindow(s);
    int blockType = readSymbol(s.blockTrees, 2 * treeType, s);
    final int result = readBlockLength(s.blockTrees, 2 * treeType + 1, s);

    if (blockType == 1) {
      blockType = ringBuffers[offset + 1] + 1;
    } else if (blockType == 0) {
      blockType = ringBuffers[offset];
    } else {
      blockType -= 2;
    }
    if (blockType >= numBlockTypes) {
      blockType -= numBlockTypes;
    }
    ringBuffers[offset] = ringBuffers[offset + 1];
    ringBuffers[offset + 1] = blockType;
    return result;
  }

  private static void decodeLiteralBlockSwitch(State s) {
    s.literalBlockLength = decodeBlockTypeAndLength(s, 0, s.numLiteralBlockTypes);
    final int literalBlockType = s.rings[5];
    s.contextMapSlice = literalBlockType << LITERAL_CONTEXT_BITS;
    s.literalTreeIdx = (int) s.contextMap[s.contextMapSlice] & 0xFF;
    final int contextMode = (int) s.contextModes[literalBlockType];
    s.contextLookupOffset1 = contextMode << 9;
    s.contextLookupOffset2 = s.contextLookupOffset1 + 256;
  }

  private static void decodeCommandBlockSwitch(State s) {
    s.commandBlockLength = decodeBlockTypeAndLength(s, 1, s.numCommandBlockTypes);
    s.commandTreeIdx = s.rings[7];
  }

  private static void decodeDistanceBlockSwitch(State s) {
    s.distanceBlockLength = decodeBlockTypeAndLength(s, 2, s.numDistanceBlockTypes);
    s.distContextMapSlice = s.rings[9] << DISTANCE_CONTEXT_BITS;
  }

  private static void maybeReallocateRingBuffer(State s) {
    int newSize = s.maxRingBufferSize;
    if (newSize > s.expectedTotalSize) {
      /* TODO(eustas): Handle 2GB+ cases more gracefully. */
      final int minimalNewSize = s.expectedTotalSize;
      while ((newSize >> 1) > minimalNewSize) {
        newSize = newSize >> 1;
      }
      if ((s.inputEnd == 0) && newSize < 16384 && s.maxRingBufferSize >= 16384) {
        newSize = 16384;
      }
    }
    if (newSize <= s.ringBufferSize) {
      return;
    }
    final int ringBufferSizeWithSlack = newSize + MAX_TRANSFORMED_WORD_LENGTH;
    final byte[] newBuffer = new byte[ringBufferSizeWithSlack];
    final byte[] oldBuffer = s.ringBuffer;
    if (oldBuffer.length != 0) {
      Utils.copyBytes(newBuffer, 0, oldBuffer, 0, s.ringBufferSize);
    }
    s.ringBuffer = newBuffer;
    s.ringBufferSize = newSize;
  }

  private static int readNextMetablockHeader(State s) {
    if (s.inputEnd != 0) {
      s.nextRunningState = FINISHED;
      s.runningState = INIT_WRITE;
      return BROTLI_OK;
    }
    // TODO(eustas): Reset? Do we need this?
    s.literalTreeGroup = new int[0];
    s.commandTreeGroup = new int[0];
    s.distanceTreeGroup = new int[0];

    int result;
    if (s.halfOffset > BitReader.HALF_WATERLINE) {
      result = BitReader.readMoreInput(s);
      if (result < BROTLI_OK) {
        return result;
      }
    }
    result = decodeMetaBlockLength(s);
    if (result < BROTLI_OK) {
      return result;
    }
    if ((s.metaBlockLength == 0) && (s.isMetadata == 0)) {
      return BROTLI_OK;
    }
    if ((s.isUncompressed != 0) || (s.isMetadata != 0)) {
      result = BitReader.jumpToByteBoundary(s);
      if (result < BROTLI_OK) {
        return result;
      }
      if (s.isMetadata == 0) {
        s.runningState = COPY_UNCOMPRESSED;
      } else {
        s.runningState = READ_METADATA;
      }
    } else {
      s.runningState = COMPRESSED_BLOCK_START;
    }

    if (s.isMetadata != 0) {
      return BROTLI_OK;
    }
    s.expectedTotalSize += s.metaBlockLength;
    if (s.expectedTotalSize > 1 << 30) {
      s.expectedTotalSize = 1 << 30;
    }
    if (s.ringBufferSize < s.maxRingBufferSize) {
      maybeReallocateRingBuffer(s);
    }
    return BROTLI_OK;
  }

  private static int readMetablockPartition(State s, int treeType, int numBlockTypes) {
    int offset = s.blockTrees[2 * treeType];
    if (numBlockTypes <= 1) {
      s.blockTrees[2 * treeType + 1] = offset;
      s.blockTrees[2 * treeType + 2] = offset;
      return 1 << 28;
    }

    final int blockTypeAlphabetSize = numBlockTypes + 2;
    int result = readHuffmanCode(
        blockTypeAlphabetSize, blockTypeAlphabetSize, s.blockTrees, 2 * treeType, s);
    if (result < BROTLI_OK) {
      return result;
    }
    offset += result;
    s.blockTrees[2 * treeType + 1] = offset;

    final int blockLengthAlphabetSize = NUM_BLOCK_LENGTH_CODES;
    result = readHuffmanCode(
        blockLengthAlphabetSize, blockLengthAlphabetSize, s.blockTrees, 2 * treeType + 1, s);
    if (result < BROTLI_OK) {
      return result;
    }
    offset += result;
    s.blockTrees[2 * treeType + 2] = offset;

    return readBlockLength(s.blockTrees, 2 * treeType + 1, s);
  }

  private static void calculateDistanceLut(State s, int alphabetSizeLimit) {
    final byte[] distExtraBits = s.distExtraBits;
    final int[] distOffset = s.distOffset;
    final int npostfix = s.distancePostfixBits;
    final int ndirect = s.numDirectDistanceCodes;
    final int postfix = 1 << npostfix;
    int bits = 1;
    int half = 0;

    /* Skip short codes. */
    int i = NUM_DISTANCE_SHORT_CODES;

    /* Fill direct codes. */
    for (int j = 0; j < ndirect; ++j) {
      distExtraBits[i] = 0;
      distOffset[i] = j + 1;
      ++i;
    }

    /* Fill regular distance codes. */
    while (i < alphabetSizeLimit) {
      final int base = ndirect + ((((2 + half) << bits) - 4) << npostfix) + 1;
      /* Always fill the complete group. */
      for (int j = 0; j < postfix; ++j) {
        distExtraBits[i] = (byte) bits;
        distOffset[i] = base + j;
        ++i;
      }
      bits = bits + half;
      half = half ^ 1;
    }
  }

  private static int readMetablockHuffmanCodesAndContextMaps(State s) {
    s.numLiteralBlockTypes = decodeVarLenUnsignedByte(s) + 1;
    int result = readMetablockPartition(s, 0, s.numLiteralBlockTypes);
    if (result < BROTLI_OK) {
      return result;
    }
    s.literalBlockLength = result;
    s.numCommandBlockTypes = decodeVarLenUnsignedByte(s) + 1;
    result = readMetablockPartition(s, 1, s.numCommandBlockTypes);
    if (result < BROTLI_OK) {
      return result;
    }
    s.commandBlockLength = result;
    s.numDistanceBlockTypes = decodeVarLenUnsignedByte(s) + 1;
    result = readMetablockPartition(s, 2, s.numDistanceBlockTypes);
    if (result < BROTLI_OK) {
      return result;
    }
    s.distanceBlockLength = result;

    if (s.halfOffset > BitReader.HALF_WATERLINE) {
      result = BitReader.readMoreInput(s);
      if (result < BROTLI_OK) {
        return result;
      }
    }
    BitReader.fillBitWindow(s);
    s.distancePostfixBits = BitReader.readFewBits(s, 2);
    s.numDirectDistanceCodes = BitReader.readFewBits(s, 4) << s.distancePostfixBits;
    // TODO(eustas): Reuse?
    s.contextModes = new byte[s.numLiteralBlockTypes];
    int i = 0;
    while (i < s.numLiteralBlockTypes) {
      /* Ensure that less than 256 bits read between readMoreInput. */
      final int limit = Utils.min(i + 96, s.numLiteralBlockTypes);
      while (i < limit) {
        BitReader.fillBitWindow(s);
        s.contextModes[i] = (byte) BitReader.readFewBits(s, 2);
        i++;
      }
      if (s.halfOffset > BitReader.HALF_WATERLINE) {
        result = BitReader.readMoreInput(s);
        if (result < BROTLI_OK) {
          return result;
        }
      }
    }

    // TODO(eustas): Reuse?
    final int contextMapLength = s.numLiteralBlockTypes << LITERAL_CONTEXT_BITS;
    s.contextMap = new byte[contextMapLength];
    result = decodeContextMap(contextMapLength, s.contextMap, s);
    if (result < BROTLI_OK) {
      return result;
    }
    final int numLiteralTrees = result;
    s.trivialLiteralContext = 1;
    for (int j = 0; j < contextMapLength; ++j) {
      if ((int) s.contextMap[j] != j >> LITERAL_CONTEXT_BITS) {
        s.trivialLiteralContext = 0;
        break;
      }
    }

    // TODO(eustas): Reuse?
    s.distContextMap = new byte[s.numDistanceBlockTypes << DISTANCE_CONTEXT_BITS];
    result = decodeContextMap(s.numDistanceBlockTypes << DISTANCE_CONTEXT_BITS,
        s.distContextMap, s);
    if (result < BROTLI_OK) {
      return result;
    }
    final int numDistTrees = result;

    s.literalTreeGroup = new int[huffmanTreeGroupAllocSize(NUM_LITERAL_CODES, numLiteralTrees)];
    result = decodeHuffmanTreeGroup(
        NUM_LITERAL_CODES, NUM_LITERAL_CODES, numLiteralTrees, s, s.literalTreeGroup);
    if (result < BROTLI_OK) {
      return result;
    }
    s.commandTreeGroup =
        new int[huffmanTreeGroupAllocSize(NUM_COMMAND_CODES, s.numCommandBlockTypes)];
    result = decodeHuffmanTreeGroup(
        NUM_COMMAND_CODES, NUM_COMMAND_CODES, s.numCommandBlockTypes, s, s.commandTreeGroup);
    if (result < BROTLI_OK) {
      return result;
    }
    int distanceAlphabetSizeMax = calculateDistanceAlphabetSize(
        s.distancePostfixBits, s.numDirectDistanceCodes, MAX_DISTANCE_BITS);
    int distanceAlphabetSizeLimit = distanceAlphabetSizeMax;
    if (s.isLargeWindow == 1) {
      distanceAlphabetSizeMax = calculateDistanceAlphabetSize(
          s.distancePostfixBits, s.numDirectDistanceCodes, MAX_LARGE_WINDOW_DISTANCE_BITS);
      result = calculateDistanceAlphabetLimit(
          s, MAX_ALLOWED_DISTANCE, s.distancePostfixBits, s.numDirectDistanceCodes);
      if (result < BROTLI_OK) {
        return result;
      }
      distanceAlphabetSizeLimit = result;
    }
    s.distanceTreeGroup =
        new int[huffmanTreeGroupAllocSize(distanceAlphabetSizeLimit, numDistTrees)];
    result = decodeHuffmanTreeGroup(
        distanceAlphabetSizeMax, distanceAlphabetSizeLimit, numDistTrees, s, s.distanceTreeGroup);
    if (result < BROTLI_OK) {
      return result;
    }
    calculateDistanceLut(s, distanceAlphabetSizeLimit);

    s.contextMapSlice = 0;
    s.distContextMapSlice = 0;
    s.contextLookupOffset1 = (int) s.contextModes[0] * 512;
    s.contextLookupOffset2 = s.contextLookupOffset1 + 256;
    s.literalTreeIdx = 0;
    s.commandTreeIdx = 0;

    s.rings[4] = 1;
    s.rings[5] = 0;
    s.rings[6] = 1;
    s.rings[7] = 0;
    s.rings[8] = 1;
    s.rings[9] = 0;
    return BROTLI_OK;
  }

  private static int copyUncompressedData(State s) {
    final byte[] ringBuffer = s.ringBuffer;
    int result;

    // Could happen if block ends at ring buffer end.
    if (s.metaBlockLength <= 0) {
      result = BitReader.reload(s);
      if (result < BROTLI_OK) {
        return result;
      }
      s.runningState = BLOCK_START;
      return BROTLI_OK;
    }

    final int chunkLength = Utils.min(s.ringBufferSize - s.pos, s.metaBlockLength);
    result = BitReader.copyRawBytes(s, ringBuffer, s.pos, chunkLength);
    if (result < BROTLI_OK) {
      return result;
    }
    s.metaBlockLength -= chunkLength;
    s.pos += chunkLength;
    if (s.pos == s.ringBufferSize) {
        s.nextRunningState = COPY_UNCOMPRESSED;
        s.runningState = INIT_WRITE;
        return BROTLI_OK;
      }

    result = BitReader.reload(s);
    if (result < BROTLI_OK) {
      return result;
    }
    s.runningState = BLOCK_START;
    return BROTLI_OK;
  }

  private static int writeRingBuffer(State s) {
    final int toWrite = Utils.min(s.outputLength - s.outputUsed,
        s.ringBufferBytesReady - s.ringBufferBytesWritten);
    // TODO(eustas): DCHECK(toWrite >= 0)
    if (toWrite != 0) {
      Utils.copyBytes(s.output, s.outputOffset + s.outputUsed, s.ringBuffer,
          s.ringBufferBytesWritten, s.ringBufferBytesWritten + toWrite);
      s.outputUsed += toWrite;
      s.ringBufferBytesWritten += toWrite;
    }

    if (s.outputUsed < s.outputLength) {
      return BROTLI_OK;
    }
    return BROTLI_OK_NEED_MORE_OUTPUT;
  }

  private static int huffmanTreeGroupAllocSize(int alphabetSizeLimit, int n) {
    final int maxTableSize = MAX_HUFFMAN_TABLE_SIZE[(alphabetSizeLimit + 31) >> 5];
    return n + n * maxTableSize;
  }

  private static int decodeHuffmanTreeGroup(int alphabetSizeMax, int alphabetSizeLimit,
      int n, State s, int[] group) {
    int next = n;
    for (int i = 0; i < n; ++i) {
      group[i] = next;
      final int result = readHuffmanCode(alphabetSizeMax, alphabetSizeLimit, group, i, s);
      if (result < BROTLI_OK) {
        return result;
      }
      next += result;
    }
    return BROTLI_OK;
  }

  // Returns offset in ringBuffer that should trigger WRITE when filled.
  private static int calculateFence(State s) {
    int result = s.ringBufferSize;
    if (s.isEager != 0) {
      result = Utils.min(result, s.ringBufferBytesWritten + s.outputLength - s.outputUsed);
    }
    return result;
  }

  private static int doUseDictionary(State s, int fence) {
    if (s.distance > MAX_ALLOWED_DISTANCE) {
      return Utils.makeError(s, BROTLI_ERROR_INVALID_BACKWARD_REFERENCE);
    }
    final int address = s.distance - s.maxDistance - 1 - s.cdTotalSize;
    if (address < 0) {
      final int result = initializeCompoundDictionaryCopy(s, -address - 1, s.copyLength);
      if (result < BROTLI_OK) {
        return result;
      }
      s.runningState = COPY_FROM_COMPOUND_DICTIONARY;
    } else {
      // Force lazy dictionary initialization.
      final ByteBuffer dictionaryData = Dictionary.getData();
      final int wordLength = s.copyLength;
      if (wordLength > Dictionary.MAX_DICTIONARY_WORD_LENGTH) {
        return Utils.makeError(s, BROTLI_ERROR_INVALID_BACKWARD_REFERENCE);
      }
      final int shift = Dictionary.sizeBits[wordLength];
      if (shift == 0) {
        return Utils.makeError(s, BROTLI_ERROR_INVALID_BACKWARD_REFERENCE);
      }
      int offset = Dictionary.offsets[wordLength];
      final int mask = (1 << shift) - 1;
      final int wordIdx = address & mask;
      final int transformIdx = address >> shift;
      offset += wordIdx * wordLength;
      final Transform.Transforms transforms = Transform.RFC_TRANSFORMS;
      if (transformIdx >= transforms.numTransforms) {
        return Utils.makeError(s, BROTLI_ERROR_INVALID_BACKWARD_REFERENCE);
      }
      final int len = Transform.transformDictionaryWord(s.ringBuffer, s.pos, dictionaryData,
          offset, wordLength, transforms, transformIdx);
      s.pos += len;
      s.metaBlockLength -= len;
      if (s.pos >= fence) {
        s.nextRunningState = MAIN_LOOP;
        s.runningState = INIT_WRITE;
        return BROTLI_OK;
      }
      s.runningState = MAIN_LOOP;
    }
    return BROTLI_OK;
  }

  private static void initializeCompoundDictionary(State s) {
    s.cdBlockMap = new byte[1 << CD_BLOCK_MAP_BITS];
    int blockBits = CD_BLOCK_MAP_BITS;
    // If this function is executed, then s.cdTotalSize > 0.
    while (((s.cdTotalSize - 1) >> blockBits) != 0) {
      blockBits++;
    }
    blockBits -= CD_BLOCK_MAP_BITS;
    s.cdBlockBits = blockBits;
    int cursor = 0;
    int index = 0;
    while (cursor < s.cdTotalSize) {
      while (s.cdChunkOffsets[index + 1] < cursor) {
        index++;
      }
      s.cdBlockMap[cursor >> blockBits] = (byte) index;
      cursor += 1 << blockBits;
    }
  }

  private static int initializeCompoundDictionaryCopy(State s, int address, int length) {
    if (s.cdBlockBits == -1) {
      initializeCompoundDictionary(s);
    }
    int index = (int) s.cdBlockMap[address >> s.cdBlockBits];
    while (address >= s.cdChunkOffsets[index + 1]) {
      index++;
    }
    if (s.cdTotalSize > address + length) {
      return Utils.makeError(s, BROTLI_ERROR_INVALID_BACKWARD_REFERENCE);
    }
    /* Update the recent distances cache */
    s.distRbIdx = (s.distRbIdx + 1) & 0x3;
    s.rings[s.distRbIdx] = s.distance;
    s.metaBlockLength -= length;
    s.cdBrIndex = index;
    s.cdBrOffset = address - s.cdChunkOffsets[index];
    s.cdBrLength = length;
    s.cdBrCopied = 0;
    return BROTLI_OK;
  }

  private static int copyFromCompoundDictionary(State s, int fence) {
    int pos = s.pos;
    final int origPos = pos;
    while (s.cdBrLength != s.cdBrCopied) {
      final int space = fence - pos;
      final int chunkLength = s.cdChunkOffsets[s.cdBrIndex + 1] - s.cdChunkOffsets[s.cdBrIndex];
      final int remChunkLength = chunkLength - s.cdBrOffset;
      int length = s.cdBrLength - s.cdBrCopied;
      if (length > remChunkLength) {
        length = remChunkLength;
      }
      if (length > space) {
        length = space;
      }
      Utils.copyBytes(
          s.ringBuffer, pos, s.cdChunks[s.cdBrIndex], s.cdBrOffset, s.cdBrOffset + length);
      pos += length;
      s.cdBrOffset += length;
      s.cdBrCopied += length;
      if (length == remChunkLength) {
        s.cdBrIndex++;
        s.cdBrOffset = 0;
      }
      if (pos >= fence) {
        break;
      }
    }
    return pos - origPos;
  }

  /**
   * Actual decompress implementation.
   */
  static int decompress(State s) {
    int result;
    if (s.runningState == UNINITIALIZED) {
      return Utils.makeError(s, BROTLI_PANIC_STATE_NOT_INITIALIZED);
    }
    if (s.runningState < 0) {
      return Utils.makeError(s, BROTLI_PANIC_UNEXPECTED_STATE);
    }
    if (s.runningState == CLOSED) {
      return Utils.makeError(s, BROTLI_PANIC_ALREADY_CLOSED);
    }
    if (s.runningState == INITIALIZED) {
      final int windowBits = decodeWindowBits(s);
      if (windowBits == -1) {  /* Reserved case for future expansion. */
        return Utils.makeError(s, BROTLI_ERROR_INVALID_WINDOW_BITS);
      }
      s.maxRingBufferSize = 1 << windowBits;
      s.maxBackwardDistance = s.maxRingBufferSize - 16;
      s.runningState = BLOCK_START;
    }

    int fence = calculateFence(s);
    int ringBufferMask = s.ringBufferSize - 1;
    byte[] ringBuffer = s.ringBuffer;

    while (s.runningState != FINISHED) {
      // TODO(eustas): extract cases to methods for the better readability.
      switch (s.runningState) {
        case BLOCK_START:
          if (s.metaBlockLength < 0) {
            return Utils.makeError(s, BROTLI_ERROR_INVALID_METABLOCK_LENGTH);
          }
          result = readNextMetablockHeader(s);
          if (result < BROTLI_OK) {
            return result;
          }
          /* Ring-buffer would be reallocated here. */
          fence = calculateFence(s);
          ringBufferMask = s.ringBufferSize - 1;
          ringBuffer = s.ringBuffer;
          continue;

        case COMPRESSED_BLOCK_START: {
          result = readMetablockHuffmanCodesAndContextMaps(s);
          if (result < BROTLI_OK) {
            return result;
          }
          s.runningState = MAIN_LOOP;
          continue;
        }

        case MAIN_LOOP:
          if (s.metaBlockLength <= 0) {
            s.runningState = BLOCK_START;
            continue;
          }
          if (s.halfOffset > BitReader.HALF_WATERLINE) {
            result = BitReader.readMoreInput(s);
            if (result < BROTLI_OK) {
              return result;
            }
          }
          if (s.commandBlockLength == 0) {
            decodeCommandBlockSwitch(s);
          }
          s.commandBlockLength--;
          BitReader.fillBitWindow(s);
          final int cmdCode = readSymbol(s.commandTreeGroup, s.commandTreeIdx, s) << 2;
          final int insertAndCopyExtraBits = (int) CMD_LOOKUP[cmdCode];
          final int insertLengthOffset = (int) CMD_LOOKUP[cmdCode + 1];
          final int copyLengthOffset = (int) CMD_LOOKUP[cmdCode + 2];
          s.distanceCode = (int) CMD_LOOKUP[cmdCode + 3];
          BitReader.fillBitWindow(s);
          {
            final int insertLengthExtraBits = insertAndCopyExtraBits & 0xFF;
            s.insertLength = insertLengthOffset + BitReader.readBits(s, insertLengthExtraBits);
          }
          BitReader.fillBitWindow(s);
          {
            final int copyLengthExtraBits = insertAndCopyExtraBits >> 8;
            s.copyLength = copyLengthOffset + BitReader.readBits(s, copyLengthExtraBits);
          }

          s.j = 0;
          s.runningState = INSERT_LOOP;
          continue;

        case INSERT_LOOP:
          if (s.trivialLiteralContext != 0) {
            while (s.j < s.insertLength) {
              if (s.halfOffset > BitReader.HALF_WATERLINE) {
                result = BitReader.readMoreInput(s);
                if (result < BROTLI_OK) {
                  return result;
                }
              }
              if (s.literalBlockLength == 0) {
                decodeLiteralBlockSwitch(s);
              }
              s.literalBlockLength--;
              BitReader.fillBitWindow(s);
              ringBuffer[s.pos] = (byte) readSymbol(s.literalTreeGroup, s.literalTreeIdx, s);
              s.pos++;
              s.j++;
              if (s.pos >= fence) {
                s.nextRunningState = INSERT_LOOP;
                s.runningState = INIT_WRITE;
                break;
              }
            }
          } else {
            int prevByte1 = (int) ringBuffer[(s.pos - 1) & ringBufferMask] & 0xFF;
            int prevByte2 = (int) ringBuffer[(s.pos - 2) & ringBufferMask] & 0xFF;
            while (s.j < s.insertLength) {
              if (s.halfOffset > BitReader.HALF_WATERLINE) {
                result = BitReader.readMoreInput(s);
                if (result < BROTLI_OK) {
                  return result;
                }
              }
              if (s.literalBlockLength == 0) {
                decodeLiteralBlockSwitch(s);
              }
              final int literalContext = Context.LOOKUP[s.contextLookupOffset1 + prevByte1]
                  | Context.LOOKUP[s.contextLookupOffset2 + prevByte2];
              final int literalTreeIdx =
                  (int) s.contextMap[s.contextMapSlice + literalContext] & 0xFF;
              s.literalBlockLength--;
              prevByte2 = prevByte1;
              BitReader.fillBitWindow(s);
              prevByte1 = readSymbol(s.literalTreeGroup, literalTreeIdx, s);
              ringBuffer[s.pos] = (byte) prevByte1;
              s.pos++;
              s.j++;
              if (s.pos >= fence) {
                s.nextRunningState = INSERT_LOOP;
                s.runningState = INIT_WRITE;
                break;
              }
            }
          }
          if (s.runningState != INSERT_LOOP) {
            continue;
          }
          s.metaBlockLength -= s.insertLength;
          if (s.metaBlockLength <= 0) {
            s.runningState = MAIN_LOOP;
            continue;
          }
          int distanceCode = s.distanceCode;
          if (distanceCode < 0) {
            // distanceCode in untouched; assigning it 0 won't affect distance ring buffer rolling.
            s.distance = s.rings[s.distRbIdx];
          } else {
            if (s.halfOffset > BitReader.HALF_WATERLINE) {
              result = BitReader.readMoreInput(s);
              if (result < BROTLI_OK) {
                return result;
              }
            }
            if (s.distanceBlockLength == 0) {
              decodeDistanceBlockSwitch(s);
            }
            s.distanceBlockLength--;
            BitReader.fillBitWindow(s);
            final int distTreeIdx =
                (int) s.distContextMap[s.distContextMapSlice + distanceCode] & 0xFF;
            distanceCode = readSymbol(s.distanceTreeGroup, distTreeIdx, s);
            if (distanceCode < NUM_DISTANCE_SHORT_CODES) {
              final int index =
                  (s.distRbIdx + DISTANCE_SHORT_CODE_INDEX_OFFSET[distanceCode]) & 0x3;
              s.distance = s.rings[index] + DISTANCE_SHORT_CODE_VALUE_OFFSET[distanceCode];
              if (s.distance < 0) {
                return Utils.makeError(s, BROTLI_ERROR_NEGATIVE_DISTANCE);
              }
            } else {
              final int extraBits = (int) s.distExtraBits[distanceCode];
              int bits;
              if (s.bitOffset + extraBits <= BitReader.BITNESS) {
                bits = BitReader.readFewBits(s, extraBits);
              } else {
                BitReader.fillBitWindow(s);
                bits = BitReader.readBits(s, extraBits);
              }
              s.distance = s.distOffset[distanceCode] + (bits << s.distancePostfixBits);
            }
          }

          if (s.maxDistance != s.maxBackwardDistance
              && s.pos < s.maxBackwardDistance) {
            s.maxDistance = s.pos;
          } else {
            s.maxDistance = s.maxBackwardDistance;
          }

          if (s.distance > s.maxDistance) {
            s.runningState = USE_DICTIONARY;
            continue;
          }

          if (distanceCode > 0) {
            s.distRbIdx = (s.distRbIdx + 1) & 0x3;
            s.rings[s.distRbIdx] = s.distance;
          }

          if (s.copyLength > s.metaBlockLength) {
            return Utils.makeError(s, BROTLI_ERROR_INVALID_BACKWARD_REFERENCE);
          }
          s.j = 0;
          s.runningState = COPY_LOOP;
          continue;

        case COPY_LOOP:
          int src = (s.pos - s.distance) & ringBufferMask;
          int dst = s.pos;
          final int copyLength = s.copyLength - s.j;
          final int srcEnd = src + copyLength;
          final int dstEnd = dst + copyLength;
          if ((srcEnd < ringBufferMask) && (dstEnd < ringBufferMask)) {
            if (copyLength < 12 || (srcEnd > dst && dstEnd > src)) {
              final int numQuads = (copyLength + 3) >> 2;
              for (int k = 0; k < numQuads; ++k) {
                ringBuffer[dst++] = ringBuffer[src++];
                ringBuffer[dst++] = ringBuffer[src++];
                ringBuffer[dst++] = ringBuffer[src++];
                ringBuffer[dst++] = ringBuffer[src++];
              }
            } else {
              Utils.copyBytesWithin(ringBuffer, dst, src, srcEnd);
            }
            s.j += copyLength;
            s.metaBlockLength -= copyLength;
            s.pos += copyLength;
          } else {
            while (s.j < s.copyLength) {
              ringBuffer[s.pos] =
                  ringBuffer[(s.pos - s.distance) & ringBufferMask];
              s.metaBlockLength--;
              s.pos++;
              s.j++;
              if (s.pos >= fence) {
                s.nextRunningState = COPY_LOOP;
                s.runningState = INIT_WRITE;
                break;
              }
            }
          }
          if (s.runningState == COPY_LOOP) {
            s.runningState = MAIN_LOOP;
          }
          continue;

        case USE_DICTIONARY:
          result = doUseDictionary(s, fence);
          if (result < BROTLI_OK) {
            return result;
          }
          continue;

        case COPY_FROM_COMPOUND_DICTIONARY:
          s.pos += copyFromCompoundDictionary(s, fence);
          if (s.pos >= fence) {
            s.nextRunningState = COPY_FROM_COMPOUND_DICTIONARY;
            s.runningState = INIT_WRITE;
            return BROTLI_OK_NEED_MORE_OUTPUT;
          }
          s.runningState = MAIN_LOOP;
          continue;

        case READ_METADATA:
          while (s.metaBlockLength > 0) {
            if (s.halfOffset > BitReader.HALF_WATERLINE) {
              result = BitReader.readMoreInput(s);
              if (result < BROTLI_OK) {
                return result;
              }
            }
            // Optimize
            BitReader.fillBitWindow(s);
            BitReader.readFewBits(s, 8);
            s.metaBlockLength--;
          }
          s.runningState = BLOCK_START;
          continue;

        case COPY_UNCOMPRESSED:
          result = copyUncompressedData(s);
          if (result < BROTLI_OK) {
            return result;
          }
          continue;

        case INIT_WRITE:
          s.ringBufferBytesReady = Utils.min(s.pos, s.ringBufferSize);
          s.runningState = WRITE;
          continue;

        case WRITE:
          result = writeRingBuffer(s);
          if (result != BROTLI_OK) {
            // Output buffer is full.
            return result;
          }
          if (s.pos >= s.maxBackwardDistance) {
            s.maxDistance = s.maxBackwardDistance;
          }
          // Wrap the ringBuffer.
          if (s.pos >= s.ringBufferSize) {
            if (s.pos > s.ringBufferSize) {
              Utils.copyBytesWithin(ringBuffer, 0, s.ringBufferSize, s.pos);
            }
            s.pos = s.pos & ringBufferMask;
            s.ringBufferBytesWritten = 0;
          }
          s.runningState = s.nextRunningState;
          continue;

        default:
          return Utils.makeError(s, BROTLI_PANIC_UNEXPECTED_STATE);
      }
    }
    if (s.runningState != FINISHED) {
      return Utils.makeError(s, BROTLI_PANIC_UNREACHABLE);
    }
    if (s.metaBlockLength < 0) {
      return Utils.makeError(s, BROTLI_ERROR_INVALID_METABLOCK_LENGTH);
    }
    result = BitReader.jumpToByteBoundary(s);
    if (result != BROTLI_OK) {
      return result;
    }
    result = BitReader.checkHealth(s, 1);
    if (result != BROTLI_OK) {
      return result;
    }
    return BROTLI_OK_DONE;
  }
}
