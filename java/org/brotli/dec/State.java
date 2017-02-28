/* Copyright 2015 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

package org.brotli.dec;

import static org.brotli.dec.RunningState.BLOCK_START;
import static org.brotli.dec.RunningState.CLOSED;
import static org.brotli.dec.RunningState.UNINITIALIZED;

import java.io.IOException;
import java.io.InputStream;

final class State {
  RunningState runningState = UNINITIALIZED;
  RunningState nextRunningState;
  final BitReader br = new BitReader();
  byte[] ringBuffer;
  final int[] blockTypeTrees = new int[3 * Huffman.HUFFMAN_MAX_TABLE_SIZE];
  final int[] blockLenTrees = new int[3 * Huffman.HUFFMAN_MAX_TABLE_SIZE];

  // Current meta-block header information.
  int metaBlockLength;
  boolean inputEnd;
  boolean isUncompressed;
  boolean isMetadata;

  final HuffmanTreeGroup hGroup0 = new HuffmanTreeGroup();
  final HuffmanTreeGroup hGroup1 = new HuffmanTreeGroup();
  final HuffmanTreeGroup hGroup2 = new HuffmanTreeGroup();
  final int[] blockLength = new int[3];
  final int[] numBlockTypes = new int[3];
  final int[] blockTypeRb = new int[6];
  final int[] distRb = {16, 15, 11, 4};
  int pos = 0;
  int maxDistance = 0;
  int distRbIdx = 0;
  boolean trivialLiteralContext = false;
  int literalTreeIndex = 0;
  int literalTree;
  int j;
  int insertLength;
  byte[] contextModes;
  byte[] contextMap;
  int contextMapSlice;
  int distContextMapSlice;
  int contextLookupOffset1;
  int contextLookupOffset2;
  int treeCommandOffset;
  int distanceCode;
  byte[] distContextMap;
  int numDirectDistanceCodes;
  int distancePostfixMask;
  int distancePostfixBits;
  int distance;
  int copyLength;
  int copyDst;
  int maxBackwardDistance;
  int maxRingBufferSize;
  int ringBufferSize = 0;
  long expectedTotalSize = 0;
  byte[] customDictionary = new byte[0];
  int bytesToIgnore = 0;

  int outputOffset;
  int outputLength;
  int outputUsed;
  int bytesWritten;
  int bytesToWrite;
  byte[] output;

  // TODO: Update to current spec.
  private static int decodeWindowBits(BitReader br) {
    if (BitReader.readBits(br, 1) == 0) {
      return 16;
    }
    int n = BitReader.readBits(br, 3);
    if (n != 0) {
      return 17 + n;
    }
    n = BitReader.readBits(br, 3);
    if (n != 0) {
      return 8 + n;
    }
    return 17;
  }

  /**
   * Associate input with decoder state.
   *
   * @param state uninitialized state without associated input
   * @param input compressed data source
   */
  static void setInput(State state, InputStream input) {
    if (state.runningState != UNINITIALIZED) {
      throw new IllegalStateException("State MUST be uninitialized");
    }
    BitReader.init(state.br, input);
    int windowBits = decodeWindowBits(state.br);
    if (windowBits == 9) { /* Reserved case for future expansion. */
      throw new BrotliRuntimeException("Invalid 'windowBits' code");
    }
    state.maxRingBufferSize = 1 << windowBits;
    state.maxBackwardDistance = state.maxRingBufferSize - 16;
    state.runningState = BLOCK_START;
  }

  static void close(State state) throws IOException {
    if (state.runningState == UNINITIALIZED) {
      throw new IllegalStateException("State MUST be initialized");
    }
    if (state.runningState == CLOSED) {
      return;
    }
    state.runningState = CLOSED;
    BitReader.close(state.br);
  }
}
