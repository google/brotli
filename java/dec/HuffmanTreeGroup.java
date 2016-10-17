/* Copyright 2015 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

package org.brotli.dec;

/**
 * Contains a collection of huffman trees with the same alphabet size.
 */
final class HuffmanTreeGroup {

  /**
   * The maximal alphabet size in this group.
   */
  private int alphabetSize;

  /**
   * Storage for Huffman lookup tables.
   */
  int[] codes;

  /**
   * Offsets of distinct lookup tables in {@link #codes} storage.
   */
  int[] trees;

  /**
   * Initializes the Huffman tree group.
   *
   * @param group POJO to be initialised
   * @param alphabetSize the maximal alphabet size in this group
   * @param n number of Huffman codes
   */
  static void init(HuffmanTreeGroup group, int alphabetSize, int n) {
    group.alphabetSize = alphabetSize;
    group.codes = new int[n * Huffman.HUFFMAN_MAX_TABLE_SIZE];
    group.trees = new int[n];
  }

  /**
   * Decodes Huffman trees from input stream and constructs lookup tables.
   *
   * @param group target POJO
   * @param br data source
   */
  static void decode(HuffmanTreeGroup group, BitReader br) {
    int next = 0;
    int n = group.trees.length;
    for (int i = 0; i < n; i++) {
      group.trees[i] = next;
      Decode.readHuffmanCode(group.alphabetSize, group.codes, next, br);
      next += Huffman.HUFFMAN_MAX_TABLE_SIZE;
    }
  }
}
