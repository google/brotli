#!/usr/bin/env bash
# Strict negative regression test for T-01.
#
# test_copystat_swap_fixed.sh simply inverts repro_copystat_swap.sh's exit
# status. That inversion conflates two failure modes inside the repro:
#
#   (a) The symlink WAS placed but target mode did NOT flip — THE FIX WORKS.
#   (b) The symlink was never placed — the LD_PRELOAD helper didn't run,
#       meaning the TEST INFRASTRUCTURE failed, not the fix.
#
# Both make the repro exit non-zero, but only (a) means the vulnerability
# is fixed. This strict test replicates the repro setup inline so it can
# distinguish (a) from (b):
#
#   - Pass: symlink placed AND target mode is still 0600.
#   - Fail (regression): symlink placed AND target mode flipped to 0644.
#   - Fail (infra): symlink never placed — the test cannot conclude
#     anything about the fix.
#
# Usage: test_copystat_swap_fixed_strict.sh /path/to/brotli /path/to/libfclose_swap.so
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
pre_target_hash=$(sha256sum "$target_path" | awk '{print $1}')

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
  "$brotli_bin" -d -o "$out_path" "$workdir/plain.bin.br" \
  || true  # brotli may exit non-zero when it sees the symlinked output

# Infra check: the helper must have run and placed the symlink. If not,
# the conclusion is ambiguous.
#
# The helper fires on fclose() of the output fd. Under the fix, CopyStat()
# runs BEFORE fclose(), which is fine — fclose() is still called, and the
# helper still swaps the path after that fclose() returns. So we expect
# the symlink to be placed regardless of whether the fix is in effect.
if [[ ! -L "$out_path" ]]; then
  echo "INFRA FAIL: symlink was never placed at $out_path" >&2
  echo "  The LD_PRELOAD fclose_swap helper did not fire." >&2
  echo "  Can't conclude whether the fix works. Check LD_PRELOAD, ASan" \
       "interaction, and helper build." >&2
  exit 2
fi

# Real check: target mode must still be 0600.
post_mode=$(stat -c '%a' "$target_path")
post_target_hash=$(sha256sum "$target_path" | awk '{print $1}')

if [[ "$post_mode" == "644" ]]; then
  echo "REGRESSION: target mode flipped 0600 -> 0644 via post-close" \
       "CopyStat path" >&2
  echo "  The T-01 TOCTOU is reproducible — the fix has regressed." >&2
  exit 1
fi
if [[ "$post_mode" != "600" ]]; then
  echo "REGRESSION (unexpected): target mode is $post_mode (expected 600)" >&2
  exit 1
fi
if [[ "$pre_target_hash" != "$post_target_hash" ]]; then
  echo "REGRESSION: target content changed (hash differs)" >&2
  exit 1
fi

echo "T-01 FIXED STRICT: swap was placed but target remained 0600 unchanged"
