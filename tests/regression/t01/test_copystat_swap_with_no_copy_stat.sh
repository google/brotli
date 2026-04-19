#!/usr/bin/env bash
# Swap-under--n test for T-01.
#
# Belt-and-suspenders check: even under the LD_PRELOAD fclose-swap attack,
# passing -n (which skips CopyStat entirely) must leave target.txt's mode
# unchanged. This test runs the swap repro with -n injected into the brotli
# command line — CopyStat never runs, so the symlink is irrelevant and the
# target's mode must stay 0600.
#
# This is a defense-in-depth test: if a future change were to wire
# CopyStat() up on a code path that ignored copy_stat, this test would
# catch it even if the primary fix is intact.
#
# We inline the attack setup here instead of reusing repro_copystat_swap.sh
# because that script hard-codes the brotli arg list. The same helper
# (libfclose_swap.so) is reused as-is.
#
# Usage: test_copystat_swap_with_no_copy_stat.sh /path/to/brotli /path/to/libfclose_swap.so
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 /path/to/brotli /path/to/libfclose_swap.so" >&2
  exit 2
fi

brotli_bin=$(readlink -f -- "$1")
swap_so=$(readlink -f -- "$2")
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
workdir=$(mktemp -d)
ld_preload="$swap_so"

cleanup() {
  rm -rf -- "$workdir"
}
trap cleanup EXIT

cp -- "$script_dir/plain.bin" "$script_dir/plain.bin.br" \
  "$script_dir/target.txt" "$workdir"/
chmod 0644 "$workdir/plain.bin"
chmod 0644 "$workdir/plain.bin.br"
chmod 0600 "$workdir/target.txt"

out_path="$workdir/out"
target_path="$workdir/target.txt"

if ldd "$brotli_bin" | grep -q 'libasan'; then
  asan_runtime=$(cc -print-file-name=libasan.so)
  if [[ -f "$asan_runtime" ]]; then
    ld_preload="$asan_runtime:$ld_preload"
  fi
fi
if [[ -n "${LD_PRELOAD:-}" ]]; then
  ld_preload="$ld_preload:$LD_PRELOAD"
fi

env \
  BROTLI_SWAP_OUTPUT_ABS="$out_path" \
  BROTLI_SWAP_TARGET_ABS="$target_path" \
  LD_PRELOAD="$ld_preload" \
  "$brotli_bin" -d -n -o "$out_path" "$workdir/plain.bin.br" || true

# With -n, CopyStat never runs, so target.txt must remain 0600.
mode=$(stat -c '%a' "$target_path")
if [[ "$mode" != "600" ]]; then
  echo "FAIL: target mode changed to $mode under -n; CopyStat was called" \
       "despite copy_stat==BROTLI_FALSE" >&2
  exit 1
fi

# Bonus: the content must also be unchanged.
if ! cmp -s -- "$workdir/target.txt" "$script_dir/target.txt"; then
  echo "FAIL: target content changed under -n attack" >&2
  exit 1
fi

echo "T-01 SWAP+NO-COPY-STAT: target unchanged under attack with -n"
