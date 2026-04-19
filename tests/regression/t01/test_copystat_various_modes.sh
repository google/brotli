#!/usr/bin/env bash
# Mode-matrix test for T-01. Exercises fchmod over the 9 permission bits
# (S_IRWXU | S_IRWXG | S_IRWXO) that CopyStat() is specified to copy.
#
# For each canonical mode below, set the input file mode, compress, and
# verify the compressed output carries the same mode. Also decompress and
# verify the decompressed output carries the same mode.
#
# The mode mask documented in PLAN.md §3.1 is:
#     statbuf.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO)
# i.e. only the low 9 bits. Any setuid/setgid/sticky bits (02000, 04000,
# 01000) MUST NOT leak through — that is tested separately in
# test_copystat_mode_mask.sh.
#
# Usage: test_copystat_various_modes.sh /path/to/brotli
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

# Representative modes: owner-only, group-readable, world-readable,
# world-readable-exec, and asymmetric combinations that round-trip cleanly
# through fchmod() but would be corrupted by any accidental umask masking.
modes=(
  "600"
  "640"
  "644"
  "664"
  "660"
  "400"
  "444"
  "755"
  "700"
  "604"
)

failures=0
for mode in "${modes[@]}"; do
  sub=$(mktemp -d -p "$workdir")
  cp -- "$script_dir/plain.bin" "$sub/in.bin"
  chmod "$mode" "$sub/in.bin"

  "$brotli_bin" -fk "$sub/in.bin" -o "$sub/in.bin.br"
  compressed_mode=$(stat -c '%a' "$sub/in.bin.br")

  "$brotli_bin" -df "$sub/in.bin.br" -o "$sub/out.bin"
  decompressed_mode=$(stat -c '%a' "$sub/out.bin")

  if [[ "$compressed_mode" != "$mode" ]]; then
    echo "FAIL mode=$mode: compress produced $compressed_mode" >&2
    failures=$((failures + 1))
  fi
  if [[ "$decompressed_mode" != "$mode" ]]; then
    echo "FAIL mode=$mode: decompress produced $decompressed_mode" >&2
    failures=$((failures + 1))
  fi
done

if [[ $failures -ne 0 ]]; then
  echo "T-01 MODE MATRIX: $failures failures across ${#modes[@]} modes" >&2
  exit 1
fi

echo "T-01 MODE MATRIX: all ${#modes[@]} modes propagated correctly"
