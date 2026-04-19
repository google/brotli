#!/usr/bin/env bash
# Content-roundtrip regression test for T-01.
#
# PLAN.md §5.3 asks us to confirm the basic compress+decompress round-trip
# is not broken by the CopyStat reorder in CloseFiles(). The reorder moves
# CopyStat() before fclose(); if that reorder accidentally also moved or
# dropped data-writing code, content would diverge.
#
# Inputs tested:
#  - Small ASCII text
#  - Empty file (edge case: zero-length input)
#  - Medium binary (plain.bin, 65537 bytes — odd size to hit unaligned paths)
#  - A file exactly on a common buffer boundary (65536 bytes)
#
# For each: compress, then decompress, then cmp against the original.
# Also asserts each intermediate file exists and is non-zero where
# appropriate, which guards against "CopyStat ran before write completed"
# style regressions from a bad reorder.
#
# Usage: test_copystat_roundtrip.sh /path/to/brotli
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 /path/to/brotli" >&2
  exit 2
fi

brotli_bin=$(readlink -f -- "$1")
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
workdir=$(mktemp -d)

cleanup() {
  rm -rf -- "$workdir"
}
trap cleanup EXIT

umask 0022

roundtrip() {
  local label="$1" src="$2"
  local sub
  sub=$(mktemp -d -p "$workdir")
  cp -- "$src" "$sub/in.bin"
  chmod 0644 "$sub/in.bin"

  "$brotli_bin" -fk "$sub/in.bin" -o "$sub/in.bin.br"
  if [[ ! -s "$sub/in.bin.br" && -s "$sub/in.bin" ]]; then
    echo "FAIL [$label]: compressed output is empty but input was not" >&2
    return 1
  fi

  "$brotli_bin" -df "$sub/in.bin.br" -o "$sub/out.bin"
  if ! cmp -s -- "$sub/in.bin" "$sub/out.bin"; then
    echo "FAIL [$label]: round-trip content mismatch" >&2
    return 1
  fi
}

# Case: small ASCII text.
printf 'hello brotli\n' > "$workdir/small.txt"
roundtrip "small-ascii" "$workdir/small.txt"

# Case: empty file.
: > "$workdir/empty.bin"
roundtrip "empty" "$workdir/empty.bin"

# Case: the stock 65537-byte plain.bin used by the T-01 assets.
roundtrip "plain-65537" "$script_dir/plain.bin"

# Case: exactly 65536 bytes (a buffer-boundary-adjacent size).
dd if=/dev/zero of="$workdir/aligned.bin" bs=65536 count=1 status=none
roundtrip "aligned-65536" "$workdir/aligned.bin"

# Case: high-entropy binary (so compression is a no-op-ish) to exercise the
# close path with minimal buffering differences.
dd if=/dev/urandom of="$workdir/random.bin" bs=4096 count=3 status=none
roundtrip "random-12288" "$workdir/random.bin"

echo "T-01 ROUNDTRIP: 5 input shapes round-tripped cleanly"
