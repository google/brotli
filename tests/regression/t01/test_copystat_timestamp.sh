#!/usr/bin/env bash
# Timestamp-copy test for T-01.
#
# PLAN.md §3.2 replaces path-based utime() with fd-based futimens() under
# HAVE_UTIMENSAT. On Linux we always have HAVE_UTIMENSAT, so this test
# validates futimens() is actually invoked with the input's mtime.
#
# Procedure:
#  1. Create an input file and set its mtime to a well-known past date.
#  2. Sleep briefly so the ambient "now" drifts from that mtime.
#  3. Compress. The compressed output should have the same mtime as the
#     input (give or take filesystem-timestamp resolution).
#  4. Decompress to a fresh path. The decompressed output should also
#     carry the input's mtime (actually: the compressed file's mtime,
#     which is the input's mtime from step 3).
#
# This guards against:
#  - CopyTimeStat refactor dropping the call entirely.
#  - The futimens path using wrong indices or wrong struct fields.
#
# Usage: test_copystat_timestamp.sh /path/to/brotli
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

cp -- "$script_dir/plain.bin" "$workdir/in.bin"
# Pinned mtime in the past (well before the current CI run).
past="202001020304.05"
touch -m -t "$past" "$workdir/in.bin"
expected_mtime=$(stat -c '%Y' "$workdir/in.bin")

# Small drift to ensure "now" != expected_mtime in the default-create case.
sleep 1

"$brotli_bin" -fk "$workdir/in.bin" -o "$workdir/in.bin.br"
compressed_mtime=$(stat -c '%Y' "$workdir/in.bin.br")

if [[ "$compressed_mtime" != "$expected_mtime" ]]; then
  echo "FAIL: compressed mtime ($compressed_mtime) != input mtime" \
       "($expected_mtime)" >&2
  exit 1
fi

"$brotli_bin" -df "$workdir/in.bin.br" -o "$workdir/out.bin"
decompressed_mtime=$(stat -c '%Y' "$workdir/out.bin")

if [[ "$decompressed_mtime" != "$expected_mtime" ]]; then
  echo "FAIL: decompressed mtime ($decompressed_mtime) != input mtime" \
       "($expected_mtime)" >&2
  exit 1
fi

echo "T-01 TIMESTAMP: futimens copied mtime through compress+decompress"
