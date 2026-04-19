#!/usr/bin/env bash
# Negative regression test for T-01 (google/brotli#1461).
#
# Wraps repro_copystat_swap.sh and INVERTS the success condition.
#
# Before the fix: the inner repro script exits 0 and flips target.txt mode
# from 0600 -> 0644 via a post-close symlink swap.
# After the fix:  CopyStat() runs BEFORE fclose(), uses fchmod() on the still-
# open fd, and can no longer be redirected at a swapped pathname. The target
# file mode must remain 0600, and the inner script must therefore exit non-
# zero. A non-zero exit from the inner script is what we want.
#
# Usage: test_copystat_swap_fixed.sh /path/to/brotli /path/to/libfclose_swap.so
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 /path/to/brotli /path/to/libfclose_swap.so" >&2
  exit 2
fi

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

if "$script_dir/repro_copystat_swap.sh" "$@" >/dev/null 2>&1; then
  echo "REGRESSION: T-01 post-close CopyStat TOCTOU is reproducible again" >&2
  echo "  The attacker still got target.txt mode flipped to 0644." >&2
  echo "  The fix in CloseFiles() / CopyStat() has regressed." >&2
  exit 1
fi

echo "T-01 FIXED: CopyStat no longer follows post-close symlink swap"
