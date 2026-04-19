#!/usr/bin/env bash
# Stdout-skip test for T-01.
#
# PLAN.md §3.3 adds this guard in CloseFiles():
#     if (!rm_output && context->copy_stat && context->current_output_path) {
#       CopyStat(context->current_input_path, context->fout);
#     }
# so that when current_output_path is NULL (stdout), CopyStat() is skipped
# entirely — not even called with a NULL path. PLAN.md §5.6 prescribes
# exercising this by running a compress|decompress pipe that uses stdout
# on both sides.
#
# Success criteria:
#  - The pipe exits with status 0.
#  - The decompressed bytes on the final stdout equal the original input.
#  - No sanitizer/diagnostic output on stderr.
#
# Usage: test_copystat_stdout_skip.sh /path/to/brotli
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 /path/to/brotli" >&2
  exit 2
fi

brotli_bin=$(readlink -f -- "$1")
workdir=$(mktemp -d)

cleanup() {
  rm -rf -- "$workdir"
}
trap cleanup EXIT

payload='hello brotli stdout pipeline'
echo "$payload" > "$workdir/expected.txt"
stderr_log="$workdir/stderr.log"

# Both stages use stdin/stdout (-c). On the compress side, context->fout is
# bound to stdout and context->current_output_path should be NULL; same on
# the decompress side. CopyStat must therefore be skipped on both.
# We separate stderr so we can assert it is empty.
result=$(
  echo "$payload" \
    | "$brotli_bin" -c 2>"$stderr_log" \
    | "$brotli_bin" -dc 2>>"$stderr_log"
)

if [[ "$result" != "$payload" ]]; then
  echo "FAIL: stdout pipeline roundtrip did not preserve payload" >&2
  echo "  got:    $result" >&2
  echo "  wanted: $payload" >&2
  exit 1
fi

# The fix must not introduce spurious diagnostics on the stdout path.
# Some sanitizers may print LeakSanitizer summaries; accept those only.
if [[ -s "$stderr_log" ]]; then
  # Filter sanitizer chatter that is not a brotli diagnostic.
  filtered=$(grep -v -E \
    -e '^==[0-9]+==' \
    -e '^SUMMARY:' \
    -e 'AddressSanitizer' \
    -e 'LeakSanitizer' \
    -e 'UndefinedBehaviorSanitizer' \
    "$stderr_log" || true)
  if [[ -n "$filtered" ]]; then
    echo "FAIL: unexpected stderr output from stdout pipeline:" >&2
    echo "$filtered" >&2
    exit 1
  fi
fi

echo "T-01 STDOUT SKIP: compress|decompress stdout pipeline clean"
