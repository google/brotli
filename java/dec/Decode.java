/* Copyright 2015 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

package org.brotli.dec;

import static org.brotli.dec.RunningState.BLOCK_START;
import static org.brotli.dec.RunningState.CLOSED;
import static org.brotli.dec.RunningState.COMPRESSED_BLOCK_START;
import static org.brotli.dec.RunningState.COPY_LOOP;
import static org.brotli.dec.RunningState.COPY_UNCOMPRESSED;
import static org.brotli.dec.RunningState.COPY_WRAP_BUFFER;
import static org.brotli.dec.RunningState.FINISHED;
import static org.brotli.dec.RunningState.INSERT_LOOP;
import static org.brotli.dec.RunningState.MAIN_LOOP;
import static org.brotli.dec.RunningState.READ_METADATA;
import static org.brotli.dec.RunningState.TRANSFORM;
import static org.brotli.dec.RunningState.UNINITIALIZED;
import static org.brotli.dec.RunningState.WRITE;

/**
 * API for Brotli decompression.
 */
public final class Decode {

  private static final int DEFAULT_CODE_LENGTH = 8;
  private static final int CODE_LENGTH_REPEAT_CODE = 16;
  private static final int NUM_LITERAL_CODES = 256;
  private static final int NUM_INSERT_AND_COPY_CODES = 704;
  private static final int NUM_BLOCK_LENGTH_CODES = 26;
  private static final int LITERAL_CONTEXT_BITS = 6;
  private static final int DISTANCE_CONTEXT_BITS = 2;

  private static final int HUFFMAN_TABLE_BITS = 8;
  private static final int HUFFMAN_TABLE_MASK = 0xFF;

  private static final int CODE_LENGTH_CODES = 18;
  private static final int[] CODE_LENGTH_CODE_ORDER = {
      1, 2, 3, 4, 0, 5, 17, 6, 16, 7, 8, 9, 10, 11, 12, 13, 14, 15,
  };

  private static final int NUM_DISTANCE_SHORT_CODES = 16;
  private static final int[] DISTANCE_SHORT_CODE_INDEX_OFFSET = {
      3, 2, 1, 0, 3, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2
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

  /**
   * Decodes a number in the range [0..255], by reading 1 - 11 bits.
   */
  private static int decodeVarLenUnsignedByte(BitReader br) {
    if (BitReader.readBits(br, 1) != 0) {
      int n = BitReader.readBits(br, 3);
      if (n == 0) {
        return 1;
      } else {
        return BitReader.readBits(br, n) + (1 << n);
      }
    }
    return 0;
  }

  private static void decodeMetaBlockLength(BitReader br, State state) {
    state.inputEnd = BitReader.readBits(br, 1) == 1;
    state.metaBlockLength = 0;
    state.isUncompressed = false;
    state.isMetadata = false;
    if (state.inputEnd && BitReader.readBits(br, 1) != 0) {
      return;
    }
    int sizeNibbles = BitReader.readBits(br, 2) + 4;
    if (sizeNibbles == 7) {
      state.isMetadata = true;
      if (BitReader.readBits(br, 1) != 0) {
        throw new BrotliRuntimeException("Corrupted reserved bit");
      }
      int sizeBytes = BitReader.readBits(br, 2);
      if (sizeBytes == 0) {
        return;
      }
      for (int i = 0; i < sizeBytes; i++) {
        int bits = BitReader.readBits(br, 8);
        if (bits == 0 && i + 1 == sizeBytes && sizeBytes > 1) {
          throw new BrotliRuntimeException("Exuberant nibble");
        }
        state.metaBlockLength |= bits << (i * 8);
      }
    } else {
      for (int i = 0; i < sizeNibbles; i++) {
        int bits = BitReader.readBits(br, 4);
        if (bits == 0 && i + 1 == sizeNibbles && sizeNibbles > 4) {
          throw new BrotliRuntimeException("Exuberant nibble");
        }
        state.metaBlockLength |= bits << (i * 4);
      }
    }
    state.metaBlockLength++;
    if (!state.inputEnd) {
      state.isUncompressed = BitReader.readBits(br, 1) == 1;
    }
  }

  /**
   * Decodes the next Huffman code from bit-stream.
   */
  private static int readSymbol(int[] table, int offset, BitReader br) {
    BitReader.fillBitWindow(br);
    offset += (int) (br.accumulator >>> br.bitOffset) & HUFFMAN_TABLE_MASK;
    int n = (table[offset] >> 16) - HUFFMAN_TABLE_BITS;
    if (n > 0) {
      br.bitOffset += HUFFMAN_TABLE_BITS;
      offset += table[offset] & 0xFFFF;
      offset += (br.accumulator >>> br.bitOffset) & ((1 << n) - 1);
    }
    br.bitOffset += table[offset] >> 16;
    return table[offset] & 0xFFFF;
  }

  private static int readBlockLength(int[] table, int offset, BitReader br) {
    int code = readSymbol(table, offset, br);
    int n = Prefix.BLOCK_LENGTH_N_BITS[code];
    return Prefix.BLOCK_LENGTH_OFFSET[code] + BitReader.readBits(br, n);
  }

  private static int translateShortCodes(int code, int[] ringBuffer, int index) {
    if (code < NUM_DISTANCE_SHORT_CODES) {
      index += DISTANCE_SHORT_CODE_INDEX_OFFSET[code];
      index &= 3;
      return ringBuffer[index] + DISTANCE_SHORT_CODE_VALUE_OFFSET[code];
    }
    return code - NUM_DISTANCE_SHORT_CODES + 1;
  }

  private static void moveToFront(int[] v, int index) {
    int value = v[index];
    for (; index > 0; index--) {
      v[index] = v[index - 1];
    }
    v[0] = value;
  }

  private static void inverseMoveToFrontTransform(byte[] v, int vLen) {
    int[] mtf = new int[256];
    for (int i = 0; i < 256; i++) {
      mtf[i] = i;
    }
    for (int i = 0; i < vLen; i++) {
      int index = v[i] & 0xFF;
      v[i] = (byte) mtf[index];
      if (index != 0) {
        moveToFront(mtf, index);
      }
    }
  }

  private static void readHuffmanCodeLengths(
      int[] codeLengthCodeLengths, int numSymbols, int[] codeLengths, BitReader br) {
    int symbol = 0;
    int prevCodeLen = DEFAULT_CODE_LENGTH;
    int repeat = 0;
    int repeatCodeLen = 0;
    int space = 32768;
    int[] table = new int[32];

    Huffman.buildHuffmanTable(table, 0, 5, codeLengthCodeLengths, CODE_LENGTH_CODES);

    while (symbol < numSymbols && space > 0) {
      BitReader.readMoreInput(br);
      BitReader.fillBitWindow(br);
      int p = (int) ((br.accumulator >>> br.bitOffset)) & 31;
      br.bitOffset += table[p] >> 16;
      int codeLen = table[p] & 0xFFFF;
      if (codeLen < CODE_LENGTH_REPEAT_CODE) {
        repeat = 0;
        codeLengths[symbol++] = codeLen;
        if (codeLen != 0) {
          prevCodeLen = codeLen;
          space -= 32768 >> codeLen;
        }
      } else {
        int extraBits = codeLen - 14;
        int newLen = 0;
        if (codeLen == CODE_LENGTH_REPEAT_CODE) {
          newLen = prevCodeLen;
        }
        if (repeatCodeLen != newLen) {
          repeat = 0;
          repeatCodeLen = newLen;
        }
        int oldRepeat = repeat;
        if (repeat > 0) {
          repeat -= 2;
          repeat <<= extraBits;
        }
        repeat += BitReader.readBits(br, extraBits) + 3;
        int repeatDelta = repeat - oldRepeat;
        if (symbol + repeatDelta > numSymbols) {
          throw new BrotliRuntimeException("symbol + repeatDelta > numSymbols"); // COV_NF_LINE
        }
        for (int i = 0; i < repeatDelta; i++) {
          codeLengths[symbol++] = repeatCodeLen;
        }
        if (repeatCodeLen != 0) {
          space -= repeatDelta << (15 - repeatCodeLen);
        }
      }
    }
    if (space != 0) {
      throw new BrotliRuntimeException("Unused space"); // COV_NF_LINE
    }
    // TODO: Pass max_symbol to Huffman table builder instead?
    Utils.fillWithZeroes(codeLengths, symbol, numSymbols - symbol);
  }

  // TODO: Use specialized versions for smaller tables.
  static void readHuffmanCode(int alphabetSize, int[] table, int offset, BitReader br) {
    boolean ok = true;
    int simpleCodeOrSkip;
    BitReader.readMoreInput(br);
    // TODO: Avoid allocation.
    int[] codeLengths = new int[alphabetSize];
    simpleCodeOrSkip = BitReader.readBits(br, 2);
    if (simpleCodeOrSkip == 1) { // Read symbols, codes & code lengths directly.
      int maxBitsCounter = alphabetSize - 1;
      int maxBits = 0;
      int[] symbols = new int[4];
      int numSymbols = BitReader.readBits(br, 2) + 1;
      while (maxBitsCounter != 0) {
        maxBitsCounter >>= 1;
        maxBits++;
      }
      Utils.fillWithZeroes(codeLengths, 0, alphabetSize);
      for (int i = 0; i < numSymbols; i++) {
        symbols[i] = BitReader.readBits(br, maxBits) % alphabetSize;
        codeLengths[symbols[i]] = 2;
      }
      codeLengths[symbols[0]] = 1;
      switch (numSymbols) {
        case 1:
          break;
        case 2:
          ok = symbols[0] != symbols[1];
          codeLengths[symbols[1]] = 1;
          break;
        case 3:
          ok = symbols[0] != symbols[1] && symbols[0] != symbols[2] && symbols[1] != symbols[2];
          break;
        case 4:
          ok = symbols[0] != symbols[1] && symbols[0] != symbols[2] && symbols[0] != symbols[3]
              && symbols[1] != symbols[2] && symbols[1] != symbols[3] && symbols[2] != symbols[3];
          if (BitReader.readBits(br, 1) == 1) {
            codeLengths[symbols[2]] = 3;
            codeLengths[symbols[3]] = 3;
          } else {
            codeLengths[symbols[0]] = 2;
          }
          break;
      }
    } else { // Decode Huffman-coded code lengths.
      int[] codeLengthCodeLengths = new int[CODE_LENGTH_CODES];
      int space = 32;
      int numCodes = 0;
      for (int i = simpleCodeOrSkip; i < CODE_LENGTH_CODES && space > 0; i++) {
        int codeLenIdx = CODE_LENGTH_CODE_ORDER[i];
        BitReader.fillBitWindow(br);
        int p = (int) (br.accumulator >>> br.bitOffset) & 15;
        // TODO: Demultiplex FIXED_TABLE.
        br.bitOffset += FIXED_TABLE[p] >> 16;
        int v = FIXED_TABLE[p] & 0xFFFF;
        codeLengthCodeLengths[codeLenIdx] = v;
        if (v != 0) {
          space -= (32 >> v);
          numCodes++;
        }
      }
      ok = (numCodes == 1 || space == 0);
      readHuffmanCodeLengths(codeLengthCodeLengths, alphabetSize, codeLengths, br);
    }
    if (!ok) {
      throw new BrotliRuntimeException("Can't readHuffmanCode"); // COV_NF_LINE
    }
    Huffman.buildHuffmanTable(table, offset, HUFFMAN_TABLE_BITS, codeLengths, alphabetSize);
  }

  private static int decodeContextMap(int contextMapSize, byte[] contextMap, BitReader br) {
    BitReader.readMoreInput(br);
    int numTrees = decodeVarLenUnsignedByte(br) + 1;

    if (numTrees == 1) {
      Utils.fillWithZeroes(contextMap, 0, contextMapSize);
      return numTrees;
    }

    boolean useRleForZeros = BitReader.readBits(br, 1) == 1;
    int maxRunLengthPrefix = 0;
    if (useRleForZeros) {
      maxRunLengthPrefix = BitReader.readBits(br, 4) + 1;
    }
    int[] table = new int[Huffman.HUFFMAN_MAX_TABLE_SIZE];
    readHuffmanCode(numTrees + maxRunLengthPrefix, table, 0, br);
    for (int i = 0; i < contextMapSize; ) {
      BitReader.readMoreInput(br);
      int code = readSymbol(table, 0, br);
      if (code == 0) {
        contextMap[i] = 0;
        i++;
      } else if (code <= maxRunLengthPrefix) {
        int reps = (1 << code) + BitReader.readBits(br, code);
        while (reps != 0) {
          if (i >= contextMapSize) {
            throw new BrotliRuntimeException("Corrupted context map"); // COV_NF_LINE
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
    if (BitReader.readBits(br, 1) == 1) {
      inverseMoveToFrontTransform(contextMap, contextMapSize);
    }
    return numTrees;
  }

  private static void decodeBlockTypeAndLength(State state, int treeType) {
    final BitReader br = state.br;
    final int[] ringBuffers = state.blockTypeRb;
    final int offset = treeType * 2;
    int blockType = readSymbol(
        state.blockTypeTrees, treeType * Huffman.HUFFMAN_MAX_TABLE_SIZE, br);
    state.blockLength[treeType] = readBlockLength(state.blockLenTrees,
        treeType * Huffman.HUFFMAN_MAX_TABLE_SIZE, br);

    if (blockType == 1) {
      blockType = ringBuffers[offset + 1] + 1;
    } else if (blockType == 0) {
      blockType = ringBuffers[offset];
    } else {
      blockType -= 2;
    }
    if (blockType >= state.numBlockTypes[treeType]) {
      blockType -= state.numBlockTypes[treeType];
    }
    ringBuffers[offset] = ringBuffers[offset + 1];
    ringBuffers[offset + 1] = blockType;
  }

  private static void decodeLiteralBlockSwitch(State state) {
    decodeBlockTypeAndLength(state, 0);
    int literalBlockType = state.blockTypeRb[1];
    state.contextMapSlice = literalBlockType << LITERAL_CONTEXT_BITS;
    state.literalTreeIndex = state.contextMap[state.contextMapSlice] & 0xFF;
    state.literalTree = state.hGroup0.trees[state.literalTreeIndex];
    int contextMode = state.contextModes[literalBlockType];
    state.contextLookupOffset1 = Context.LOOKUP_OFFSETS[contextMode];
    state.contextLookupOffset2 = Context.LOOKUP_OFFSETS[contextMode + 1];
  }

  private static void decodeCommandBlockSwitch(State state) {
    decodeBlockTypeAndLength(state, 1);
    state.treeCommandOffset = state.hGroup1.trees[state.blockTypeRb[3]];
  }

  private static void decodeDistanceBlockSwitch(State state) {
    decodeBlockTypeAndLength(state, 2);
    state.distContextMapSlice = state.blockTypeRb[5] << DISTANCE_CONTEXT_BITS;
  }

  static void maybeReallocateRingBuffer(State state) {
    int newSize = state.maxRingBufferSize;
    if ((long) newSize > state.expectedTotalSize) {
      /* TODO: Handle 2GB+ cases more gracefully. */
      int minimalNewSize = (int) state.expectedTotalSize + state.customDictionary.length;
      while ((newSize >> 1) > minimalNewSize) {
        newSize >>= 1;
      }
      if (!state.inputEnd && newSize < 16384 && state.maxRingBufferSize >= 16384) {
        newSize = 16384;
      }
    }
    if (newSize <= state.ringBufferSize) {
      return;
    }
    int ringBufferSizeWithSlack = newSize + Dictionary.MAX_TRANSFORMED_WORD_LENGTH;
    byte[] newBuffer = new byte[ringBufferSizeWithSlack];
    if (state.ringBuffer != null) {
      System.arraycopy(state.ringBuffer, 0, newBuffer, 0, state.ringBufferSize);
    } else {
      /* Prepend custom dictionary, if any. */
      if (state.customDictionary.length != 0) {
        int length = state.customDictionary.length;
        int offset = 0;
        if (length > state.maxBackwardDistance) {
          offset = length - state.maxBackwardDistance;
          length = state.maxBackwardDistance;
        }
        System.arraycopy(state.customDictionary, offset, newBuffer, 0, length);
        state.pos = length;
        state.bytesToIgnore = length;
      }
    }
    state.ringBuffer = newBuffer;
    state.ringBufferSize = newSize;
  }

  /**
   * Reads next metablock header.
   *
   * @param state decoding state
   */
  static void readMeablockInfo(State state) {
    final BitReader br = state.br;

    if (state.inputEnd) {
      state.nextRunningState = FINISHED;
      state.bytesToWrite = state.pos & (state.ringBufferSize - 1);
      state.bytesWritten = 0;
      state.runningState = WRITE;
      return;
    }
    // TODO: Reset? Do we need this?
    state.hGroup0.codes = null;
    state.hGroup0.trees = null;
    state.hGroup1.codes = null;
    state.hGroup1.trees = null;
    state.hGroup2.codes = null;
    state.hGroup2.trees = null;

    BitReader.readMoreInput(br);
    decodeMetaBlockLength(br, state);
    if (state.metaBlockLength == 0 && !state.isMetadata) {
      return;
    }
    if (state.isUncompressed || state.isMetadata) {
      BitReader.jumpToByteBoundry(br);
      state.runningState = state.isMetadata ? READ_METADATA : COPY_UNCOMPRESSED;
    } else {
      state.runningState = COMPRESSED_BLOCK_START;
    }

    if (state.isMetadata) {
      return;
    }
    state.expectedTotalSize += state.metaBlockLength;
    if (state.ringBufferSize < state.maxRingBufferSize) {
      maybeReallocateRingBuffer(state);
    }
  }

  static void readMetablockHuffmanCodesAndContextMaps(State state) {
    final BitReader br = state.br;

    for (int i = 0; i < 3; i++) {
      state.numBlockTypes[i] = decodeVarLenUnsignedByte(br) + 1;
      state.blockLength[i] = 1 << 28;
      if (state.numBlockTypes[i] > 1) {
        readHuffmanCode(state.numBlockTypes[i] + 2, state.blockTypeTrees,
            i * Huffman.HUFFMAN_MAX_TABLE_SIZE, br);
        readHuffmanCode(NUM_BLOCK_LENGTH_CODES, state.blockLenTrees,
            i * Huffman.HUFFMAN_MAX_TABLE_SIZE, br);
        state.blockLength[i] = readBlockLength(state.blockLenTrees,
            i * Huffman.HUFFMAN_MAX_TABLE_SIZE, br);
      }
    }

    BitReader.readMoreInput(br);
    state.distancePostfixBits = BitReader.readBits(br, 2);
    state.numDirectDistanceCodes =
        NUM_DISTANCE_SHORT_CODES + (BitReader.readBits(br, 4) << state.distancePostfixBits);
    state.distancePostfixMask = (1 << state.distancePostfixBits) - 1;
    int numDistanceCodes = state.numDirectDistanceCodes + (48 << state.distancePostfixBits);
    // TODO: Reuse?
    state.contextModes = new byte[state.numBlockTypes[0]];
    for (int i = 0; i < state.numBlockTypes[0];) {
      /* Ensure that less than 256 bits read between readMoreInput. */
      int limit = Math.min(i + 96, state.numBlockTypes[0]);
      for (; i < limit; ++i) {
        state.contextModes[i] = (byte) (BitReader.readBits(br, 2) << 1);
      }
      BitReader.readMoreInput(br);
    }

    // TODO: Reuse?
    state.contextMap = new byte[state.numBlockTypes[0] << LITERAL_CONTEXT_BITS];
    int numLiteralTrees = decodeContextMap(state.numBlockTypes[0] << LITERAL_CONTEXT_BITS,
        state.contextMap, br);
    state.trivialLiteralContext = true;
    for (int j = 0; j < state.numBlockTypes[0] << LITERAL_CONTEXT_BITS; j++) {
      if (state.contextMap[j] != j >> LITERAL_CONTEXT_BITS) {
        state.trivialLiteralContext = false;
        break;
      }
    }

    // TODO: Reuse?
    state.distContextMap = new byte[state.numBlockTypes[2] << DISTANCE_CONTEXT_BITS];
    int numDistTrees = decodeContextMap(state.numBlockTypes[2] << DISTANCE_CONTEXT_BITS,
        state.distContextMap, br);

    HuffmanTreeGroup.init(state.hGroup0, NUM_LITERAL_CODES, numLiteralTrees);
    HuffmanTreeGroup.init(state.hGroup1, NUM_INSERT_AND_COPY_CODES, state.numBlockTypes[1]);
    HuffmanTreeGroup.init(state.hGroup2, numDistanceCodes, numDistTrees);

    HuffmanTreeGroup.decode(state.hGroup0, br);
    HuffmanTreeGroup.decode(state.hGroup1, br);
    HuffmanTreeGroup.decode(state.hGroup2, br);

    state.contextMapSlice = 0;
    state.distContextMapSlice = 0;
    state.contextLookupOffset1 = Context.LOOKUP_OFFSETS[state.contextModes[0]];
    state.contextLookupOffset2 = Context.LOOKUP_OFFSETS[state.contextModes[0] + 1];
    state.literalTreeIndex = 0;
    state.literalTree = state.hGroup0.trees[0];
    state.treeCommandOffset = state.hGroup1.trees[0]; // TODO: == 0?

    state.blockTypeRb[0] = state.blockTypeRb[2] = state.blockTypeRb[4] = 1;
    state.blockTypeRb[1] = state.blockTypeRb[3] = state.blockTypeRb[5] = 0;
  }

  static void copyUncompressedData(State state) {
    final BitReader br = state.br;
    final byte[] ringBuffer = state.ringBuffer;
    final int ringBufferMask = state.ringBufferSize - 1;

    while (state.metaBlockLength > 0) {
      BitReader.readMoreInput(br);
      // Optimize
      ringBuffer[state.pos & ringBufferMask] = (byte) (BitReader.readBits(br, 8));
      state.metaBlockLength--;
      if ((state.pos++ & ringBufferMask) == ringBufferMask) {
        state.nextRunningState = COPY_UNCOMPRESSED;
        state.bytesToWrite = state.ringBufferSize;
        state.bytesWritten = 0;
        state.runningState = WRITE;
        return;
      }
    }
    state.runningState = BLOCK_START;
  }

  static boolean writeRingBuffer(State state) {
    /* Ignore custom dictionary bytes. */
    if (state.bytesToIgnore != 0) {
      state.bytesWritten += state.bytesToIgnore;
      state.bytesToIgnore = 0;
    }
    int toWrite = Math.min(state.outputLength - state.outputUsed,
        state.bytesToWrite - state.bytesWritten);
    if (toWrite != 0) {
      System.arraycopy(state.ringBuffer, state.bytesWritten, state.output,
          state.outputOffset + state.outputUsed, toWrite);
      state.outputUsed += toWrite;
      state.bytesWritten += toWrite;
    }

    return state.outputUsed < state.outputLength;
  }

  static void setCustomDictionary(State state, byte[] data) {
    state.customDictionary = (data == null) ? new byte[0] : data;
  }

  /**
   * Actual decompress implementation.
   */
  static void decompress(State state) {
    if (state.runningState == UNINITIALIZED) {
      throw new IllegalStateException("Can't decompress until initialized");
    }
    if (state.runningState == CLOSED) {
      throw new IllegalStateException("Can't decompress after close");
    }
    final BitReader br = state.br;
    int ringBufferMask = state.ringBufferSize - 1;
    byte[] ringBuffer = state.ringBuffer;

    while (state.runningState != FINISHED) {
      // TODO: extract cases to methods for the better readability.
      switch (state.runningState) {
        case BLOCK_START:
          if (state.metaBlockLength < 0) {
            throw new BrotliRuntimeException("Invalid metablock length");
          }
          readMeablockInfo(state);
          /* Ring-buffer would be reallocated here. */
          ringBufferMask = state.ringBufferSize - 1;
          ringBuffer = state.ringBuffer;
          continue;

        case COMPRESSED_BLOCK_START:
          readMetablockHuffmanCodesAndContextMaps(state);
          state.runningState = MAIN_LOOP;
          // Fall through

        case MAIN_LOOP:
          if (state.metaBlockLength <= 0) {
            // Protect pos from overflow, wrap it around at every GB of input data.
            state.pos &= 0x3fffffff;
            state.runningState = BLOCK_START;
            continue;
          }
          BitReader.readMoreInput(br);
          if (state.blockLength[1] == 0) {
            decodeCommandBlockSwitch(state);
          }
          state.blockLength[1]--;
          int cmdCode = readSymbol(state.hGroup1.codes, state.treeCommandOffset, br);
          int rangeIdx = cmdCode >>> 6;
          state.distanceCode = 0;
          if (rangeIdx >= 2) {
            rangeIdx -= 2;
            state.distanceCode = -1;
          }
          int insertCode = Prefix.INSERT_RANGE_LUT[rangeIdx] + ((cmdCode >>> 3) & 7);
          int copyCode = Prefix.COPY_RANGE_LUT[rangeIdx] + (cmdCode & 7);
          state.insertLength = Prefix.INSERT_LENGTH_OFFSET[insertCode] + BitReader
              .readBits(br, Prefix.INSERT_LENGTH_N_BITS[insertCode]);
          state.copyLength = Prefix.COPY_LENGTH_OFFSET[copyCode] + BitReader
              .readBits(br, Prefix.COPY_LENGTH_N_BITS[copyCode]);

          state.j = 0;
          state.runningState = INSERT_LOOP;

          // Fall through
        case INSERT_LOOP:
          if (state.trivialLiteralContext) {
            while (state.j < state.insertLength) {
              BitReader.readMoreInput(br);
              if (state.blockLength[0] == 0) {
                decodeLiteralBlockSwitch(state);
              }
              state.blockLength[0]--;
              ringBuffer[state.pos & ringBufferMask] = (byte) readSymbol(
                  state.hGroup0.codes, state.literalTree, br);
              state.j++;
              if ((state.pos++ & ringBufferMask) == ringBufferMask) {
                state.nextRunningState = INSERT_LOOP;
                state.bytesToWrite = state.ringBufferSize;
                state.bytesWritten = 0;
                state.runningState = WRITE;
                break;
              }
            }
          } else {
            int prevByte1 = ringBuffer[(state.pos - 1) & ringBufferMask] & 0xFF;
            int prevByte2 = ringBuffer[(state.pos - 2) & ringBufferMask] & 0xFF;
            while (state.j < state.insertLength) {
              BitReader.readMoreInput(br);
              if (state.blockLength[0] == 0) {
                decodeLiteralBlockSwitch(state);
              }
              int literalTreeIndex = state.contextMap[state.contextMapSlice
                + (Context.LOOKUP[state.contextLookupOffset1 + prevByte1]
                    | Context.LOOKUP[state.contextLookupOffset2 + prevByte2])] & 0xFF;
              state.blockLength[0]--;
              prevByte2 = prevByte1;
              prevByte1 = readSymbol(
                  state.hGroup0.codes, state.hGroup0.trees[literalTreeIndex], br);
              ringBuffer[state.pos & ringBufferMask] = (byte) prevByte1;
              state.j++;
              if ((state.pos++ & ringBufferMask) == ringBufferMask) {
                state.nextRunningState = INSERT_LOOP;
                state.bytesToWrite = state.ringBufferSize;
                state.bytesWritten = 0;
                state.runningState = WRITE;
                break;
              }
            }
          }
          if (state.runningState != INSERT_LOOP) {
            continue;
          }
          state.metaBlockLength -= state.insertLength;
          if (state.metaBlockLength <= 0) {
            state.runningState = MAIN_LOOP;
            continue;
          }
          if (state.distanceCode < 0) {
            BitReader.readMoreInput(br);
            if (state.blockLength[2] == 0) {
              decodeDistanceBlockSwitch(state);
            }
            state.blockLength[2]--;
            state.distanceCode = readSymbol(state.hGroup2.codes, state.hGroup2.trees[
                state.distContextMap[state.distContextMapSlice
                    + (state.copyLength > 4 ? 3 : state.copyLength - 2)] & 0xFF], br);
            if (state.distanceCode >= state.numDirectDistanceCodes) {
              state.distanceCode -= state.numDirectDistanceCodes;
              int postfix = state.distanceCode & state.distancePostfixMask;
              state.distanceCode >>>= state.distancePostfixBits;
              int n = (state.distanceCode >>> 1) + 1;
              int offset = ((2 + (state.distanceCode & 1)) << n) - 4;
              state.distanceCode = state.numDirectDistanceCodes + postfix
                  + ((offset + BitReader.readBits(br, n)) << state.distancePostfixBits);
            }
          }

          // Convert the distance code to the actual distance by possibly looking up past distances
          // from the ringBuffer.
          state.distance = translateShortCodes(state.distanceCode, state.distRb, state.distRbIdx);
          if (state.distance < 0) {
            throw new BrotliRuntimeException("Negative distance"); // COV_NF_LINE
          }

          if (state.pos < state.maxBackwardDistance
              && state.maxDistance != state.maxBackwardDistance) {
            state.maxDistance = state.pos;
          } else {
            state.maxDistance = state.maxBackwardDistance;
          }

          state.copyDst = state.pos & ringBufferMask;
          if (state.distance > state.maxDistance) {
            state.runningState = TRANSFORM;
            continue;
          }

          if (state.distanceCode > 0) {
            state.distRb[state.distRbIdx & 3] = state.distance;
            state.distRbIdx++;
          }

          if (state.copyLength > state.metaBlockLength) {
            throw new BrotliRuntimeException("Invalid backward reference"); // COV_NF_LINE
          }
          state.j = 0;
          state.runningState = COPY_LOOP;
          // fall through
        case COPY_LOOP:
          for (; state.j < state.copyLength;) {
            ringBuffer[state.pos & ringBufferMask] =
                ringBuffer[(state.pos - state.distance) & ringBufferMask];
            // TODO: condense
            state.metaBlockLength--;
            state.j++;
            if ((state.pos++ & ringBufferMask) == ringBufferMask) {
              state.nextRunningState = COPY_LOOP;
              state.bytesToWrite = state.ringBufferSize;
              state.bytesWritten = 0;
              state.runningState = WRITE;
              break;
            }
          }
          if (state.runningState == COPY_LOOP) {
            state.runningState = MAIN_LOOP;
          }
          continue;

        case TRANSFORM:
          if (state.copyLength >= Dictionary.MIN_WORD_LENGTH
              && state.copyLength <= Dictionary.MAX_WORD_LENGTH) {
            int offset = Dictionary.OFFSETS_BY_LENGTH[state.copyLength];
            int wordId = state.distance - state.maxDistance - 1;
            int shift = Dictionary.SIZE_BITS_BY_LENGTH[state.copyLength];
            int mask = (1 << shift) - 1;
            int wordIdx = wordId & mask;
            int transformIdx = wordId >>> shift;
            offset += wordIdx * state.copyLength;
            if (transformIdx < Transform.TRANSFORMS.length) {
              int len = Transform.transformDictionaryWord(ringBuffer, state.copyDst,
                  Dictionary.getData(), offset, state.copyLength,
                  Transform.TRANSFORMS[transformIdx]);
              state.copyDst += len;
              state.pos += len;
              state.metaBlockLength -= len;
              if (state.copyDst >= state.ringBufferSize) {
                state.nextRunningState = COPY_WRAP_BUFFER;
                state.bytesToWrite = state.ringBufferSize;
                state.bytesWritten = 0;
                state.runningState = WRITE;
                continue;
              }
            } else {
              throw new BrotliRuntimeException("Invalid backward reference"); // COV_NF_LINE
            }
          } else {
            throw new BrotliRuntimeException("Invalid backward reference"); // COV_NF_LINE
          }
          state.runningState = MAIN_LOOP;
          continue;

        case COPY_WRAP_BUFFER:
          System.arraycopy(ringBuffer, state.ringBufferSize, ringBuffer, 0,
              state.copyDst - state.ringBufferSize);
          state.runningState = MAIN_LOOP;
          continue;

        case READ_METADATA:
          while (state.metaBlockLength > 0) {
            BitReader.readMoreInput(br);
            // Optimize
            BitReader.readBits(br, 8);
            state.metaBlockLength--;
          }
          state.runningState = BLOCK_START;
          continue;


        case COPY_UNCOMPRESSED:
          copyUncompressedData(state);
          continue;

        case WRITE:
          if (!writeRingBuffer(state)) {
            // Output buffer is full.
            return;
          }
          state.runningState = state.nextRunningState;
          continue;

        default:
          throw new BrotliRuntimeException("Unexpected state " + state.runningState);
      }
    }
    if (state.runningState == FINISHED) {
      if (state.metaBlockLength < 0) {
        throw new BrotliRuntimeException("Invalid metablock length");
      }
      BitReader.jumpToByteBoundry(br);
      BitReader.checkHealth(state.br);
    }
  }
}
