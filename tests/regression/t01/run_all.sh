#!/usr/bin/env bash
# T-01 master test runner.
#
# Runs all tests for google/brotli#1461 (CopyStat TOCTOU) and summarizes
# results. Tests that require the LD_PRELOAD helper receive its path as
# the second argument; tests that don't are invoked with the brotli binary
# only.
#
# Usage:
#   run_all.sh /path/to/brotli /path/to/libfclose_swap.so
#
# Exit code: 0 if every test passed, non-zero otherwise.
#
# Individual tests exit with 0 on pass, non-zero on fail. Infrastructure
# failures from test_copystat_swap_fixed_strict.sh (exit 2) are surfaced
# distinctly in the summary.
set -u

if [[ $# -ne 2 ]]; then
  echo "usage: $0 /path/to/brotli /path/to/libfclose_swap.so" >&2
  exit 2
fi

brotli_bin="$1"
swap_so="$2"
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

# Tests that need only the brotli binary.
brotli_only=(
  "test_copystat_positive.sh"
  "test_copystat_various_modes.sh"
  "test_copystat_mode_mask.sh"
  "test_copystat_stdout_skip.sh"
  "test_copystat_stdin_input.sh"
  "test_copystat_no_copy_stat_flag.sh"
  "test_copystat_roundtrip.sh"
  "test_copystat_timestamp.sh"
)

# Tests that need both the brotli binary and the LD_PRELOAD helper.
attack_tests=(
  "test_copystat_swap_fixed.sh"
  "test_copystat_swap_fixed_strict.sh"
  "test_copystat_swap_with_no_copy_stat.sh"
)

passed=0
failed=0
infra_fail=0
fail_names=()
infra_names=()

run_one() {
  local script="$1"
  shift
  local label="$script"
  printf '=== %s ===\n' "$label"
  if "$script_dir/$script" "$@"; then
    passed=$((passed + 1))
    printf 'PASS: %s\n\n' "$label"
    return 0
  fi
  local rc=$?
  if [[ $rc -eq 2 ]]; then
    infra_fail=$((infra_fail + 1))
    infra_names+=("$label")
    printf 'INFRA: %s (exit %d)\n\n' "$label" "$rc"
  else
    failed=$((failed + 1))
    fail_names+=("$label")
    printf 'FAIL: %s (exit %d)\n\n' "$label" "$rc"
  fi
  return $rc
}

for t in "${brotli_only[@]}"; do
  run_one "$t" "$brotli_bin" || true
done

for t in "${attack_tests[@]}"; do
  run_one "$t" "$brotli_bin" "$swap_so" || true
done

total=$((passed + failed + infra_fail))
printf -- '----- T-01 SUMMARY -----\n'
printf 'passed: %d / %d\n' "$passed" "$total"
if [[ $failed -ne 0 ]]; then
  printf 'failed: %d\n' "$failed"
  for n in "${fail_names[@]}"; do printf '  - %s\n' "$n"; done
fi
if [[ $infra_fail -ne 0 ]]; then
  printf 'infra-failures: %d\n' "$infra_fail"
  for n in "${infra_names[@]}"; do printf '  - %s\n' "$n"; done
fi

if [[ $failed -ne 0 || $infra_fail -ne 0 ]]; then
  exit 1
fi
exit 0
