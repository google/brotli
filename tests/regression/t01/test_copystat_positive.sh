#!/usr/bin/env bash
# Positive regression test for T-01 (google/brotli#1461).
#
# After the fix, CopyStat() uses fchmod() / fchown() via fileno(fout) to copy
# the input file's permission bits onto the output file. This test confirms
# that behavior is preserved by the fix (i.e. we did not regress by breaking
# CopyStat entirely).
#
# Creates a plaintext input with an unusual mode (0640, distinct from the
# typical 0644 default), decompresses a compressed copy to a fresh output
# path, and asserts the output file carries the input file's 0640 mode.
#
# Usage: test_copystat_positive.sh /path/to/brotli
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

# Default umask can mask out group/other bits on new files; start permissive.
umask 0022

cp -- "$script_dir/plain.bin" "$workdir/input.bin"
chmod 0640 "$workdir/input.bin"

# Compress (use -k to keep the input; output mode also gets CopyStat-copied).
"$brotli_bin" -fk "$workdir/input.bin" -o "$workdir/input.bin.br"

input_mode=$(stat -c '%a' "$workdir/input.bin")
compressed_mode=$(stat -c '%a' "$workdir/input.bin.br")
if [[ "$input_mode" != "640" ]]; then
  echo "pre-condition failed: input mode is $input_mode, expected 640" >&2
  exit 1
fi
if [[ "$compressed_mode" != "640" ]]; then
  echo "POSITIVE FAIL: compress direction did not copy mode" >&2
  echo "  input mode = $input_mode, compressed mode = $compressed_mode (want 640)" >&2
  exit 1
fi

# Decompress to a fresh output path.
rm -f -- "$workdir/out.bin"
"$brotli_bin" -d "$workdir/input.bin.br" -o "$workdir/out.bin"

out_mode=$(stat -c '%a' "$workdir/out.bin")
if [[ "$out_mode" != "640" ]]; then
  echo "POSITIVE FAIL: decompress did not copy mode from input.bin.br to out.bin" >&2
  echo "  compressed mode = $compressed_mode, decompressed mode = $out_mode (want 640)" >&2
  exit 1
fi

# Content round-trip check: decompressed output must match the original.
if ! cmp -s -- "$workdir/input.bin" "$workdir/out.bin"; then
  echo "POSITIVE FAIL: decompressed content differs from original input" >&2
  exit 1
fi

echo "T-01 POSITIVE: CopyStat still propagates 0640 through compress+decompress"
