# Copyright 2026 The Brotli Authors. All rights reserved.
#
# Distributed under MIT license.
# See file LICENSE for detail or copy at https://opensource.org/licenses/MIT

import brotli
import pytest


@pytest.fixture(scope='module')
def empty_dictionary_commands():
  # Build the compressed input shared by the two tests below: 16,777,217
  # commands that produce nothing, followed by one literal A. The details
  # below describe how that input is encoded.
  #
  # WBITS=16; a final compressed metablock with MLEN=1. Each category has
  # one block type, with no block lengths or switching tables in the stream.
  # NPOSTFIX=NDIRECT=0, LSB literal contexts, one literal and distance tree.
  # Simple Huffman trees: literal {65}, command {130, 138}, distance {42}.
  # Command 130 inserts nothing and copies a dictionary word of length 4.
  # Command 138 inserts one literal; that completes the metablock.
  header = bytes.fromhex('02000000445009a248a8')

  # Distance symbol 42 has 14 extra bits. Value 2052 gives distance 34817,
  # selecting dictionary word 0 with transform 34 (omit first 4), so the
  # command produces no output. Its command code is one zero bit and its
  # distance Huffman code takes zero bits: 15 bits per empty command.
  command = 2052 << 1
  eight_commands = sum(command << (15 * i) for i in range(8))
  # More than 2^24 commands and distances, followed by command 138 (one bit).
  # Repeat in groups of eight to avoid storing a large unpacked bit array.
  return (
      header
      + eight_commands.to_bytes(15, 'little') * (1 << 21)
      + (command | (1 << 15)).to_bytes(2, 'little')
  )


def test_empty_dictionary_commands_do_not_require_block_switches(
    empty_dictionary_commands,
):
  # Decode 16,777,217 commands that produce nothing, followed by one literal A.
  # There is only one block type, so no block switch is needed. The decoder
  # must reach the A instead of exhausting its 2^24 countdown and failing.
  assert brotli.decompress(empty_dictionary_commands) == b'A'


@pytest.mark.parametrize('chunk_size', [4093, 65536])
def test_stream_empty_dictionary_commands(
    empty_dictionary_commands, chunk_size,
):
  # The same check, with input arriving in pieces.
  decompressor = brotli.Decompressor()
  output = b''
  # Parse the header and the first distance with one-byte input pieces.
  for byte in empty_dictionary_commands[:12]:
    output += decompressor.process(bytes([byte]))
  for offset in range(12, len(empty_dictionary_commands), chunk_size):
    output += decompressor.process(
        empty_dictionary_commands[offset:offset + chunk_size]
    )
  assert decompressor.is_finished()
  assert output == b'A'
