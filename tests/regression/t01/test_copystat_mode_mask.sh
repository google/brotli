#!/usr/bin/env bash
# Mode-mask test for T-01.
#
# PLAN.md §3.1 specifies that CopyStat() calls:
#     fchmod(fd, statbuf.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO));
#
# i.e. only the 9 low permission bits are copied. Setuid (04000), setgid
# (02000), and sticky (01000) bits MUST NOT propagate from the input to
# the output: that would be a silent privilege-escalation surprise.
#
# This test sets a setgid-like mode on the input (chmod will typically
# clear setuid on a non-executable file, so we test setgid + sticky),
# compresses, and asserts the compressed output's mode lacks those bits.
#
# Usage: test_copystat_mode_mask.sh /path/to/brotli
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

# Set setgid + sticky + rw-r--r--. Setuid (04000) is often cleared by kernel
# on non-executable regular files owned by non-root; setgid (02000) is the
# portable bit we can reliably set without root.
chmod 03644 "$workdir/in.bin" || true

actual_input_mode=$(stat -c '%a' "$workdir/in.bin")
# Proceed only if the kernel accepted at least some special bit; otherwise
# the test cannot meaningfully assert anything.
if [[ "$actual_input_mode" == "644" ]]; then
  echo "SKIP: filesystem/kernel stripped all special mode bits from input" >&2
  exit 0
fi

"$brotli_bin" -fk "$workdir/in.bin" -o "$workdir/in.bin.br"
compressed_mode=$(stat -c '%a' "$workdir/in.bin.br")

# The compressed output should carry the 9 permission bits only (644), NOT
# any of the special bits. Extract the 3 lowest octal digits.
# stat -c '%a' returns either "NNN" or "NNNN" depending on whether any
# special bit is set in the output.
if [[ ${#compressed_mode} -eq 4 ]]; then
  echo "FAIL: output carries special bits (mode=$compressed_mode); only 9" \
       "permission bits should be copied" >&2
  echo "  input_mode=$actual_input_mode, output_mode=$compressed_mode" >&2
  exit 1
fi

# Also assert the 9 perm bits match the input's 9 perm bits.
input_perm="${actual_input_mode: -3}"
if [[ "$compressed_mode" != "$input_perm" ]]; then
  echo "FAIL: 9-bit permission part did not propagate (input=$input_perm," \
       "output=$compressed_mode)" >&2
  exit 1
fi

echo "T-01 MODE MASK: special bits not copied (input=$actual_input_mode," \
     "output=$compressed_mode)"
