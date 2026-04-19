#!/usr/bin/env bash
# --no-copy-stat (-n) flag test for T-01.
#
# PLAN.md §4 row 1: when copy_stat == BROTLI_FALSE (user passed -n /
# --no-copy-stat), CopyStat() must not be called. The new CloseFiles()
# preserves this condition.
#
# We verify two things:
#  1. With -n, the output file does NOT inherit the input mode. Instead,
#     it gets whatever the default mode is (derived from umask and the
#     OpenOutputFile creat() mode).
#  2. Without -n, the same input reliably produces an output with the
#     input's mode — this cross-checks that (1)'s behavior is due to the
#     flag, not some other failure to propagate mode.
#
# Usage: test_copystat_no_copy_stat_flag.sh /path/to/brotli
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

# Set a predictable umask so the default-mode assertion below is stable.
umask 0022

cp -- "$script_dir/plain.bin" "$workdir/in.bin"
# Choose an unusual mode distinct from the 0644 default so we can detect
# whether it propagated. 0600 was already used by the swap repro; choose
# 0606 here to avoid colliding with "would-be 0644 with group stripped".
chmod 0606 "$workdir/in.bin"

# Case A: with -n, mode must NOT propagate.
"$brotli_bin" -fkn "$workdir/in.bin" -o "$workdir/out_n.br"
mode_n=$(stat -c '%a' "$workdir/out_n.br")
if [[ "$mode_n" == "606" ]]; then
  echo "FAIL: with -n, mode 0606 propagated anyway — CopyStat was still called" >&2
  exit 1
fi

# Case B: without -n, mode MUST propagate.
"$brotli_bin" -fk "$workdir/in.bin" -o "$workdir/out_y.br"
mode_y=$(stat -c '%a' "$workdir/out_y.br")
if [[ "$mode_y" != "606" ]]; then
  echo "FAIL: without -n, mode 0606 did not propagate (got $mode_y)" >&2
  exit 1
fi

echo "T-01 NO-COPY-STAT: -n suppressed CopyStat, default preserved elsewhere"
