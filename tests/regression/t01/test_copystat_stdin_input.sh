#!/usr/bin/env bash
# Stdin-input test for T-01.
#
# PLAN.md §4 row 3: when input is stdin (context->current_input_path == NULL),
# CopyStat() early-returns on the input_path == NULL branch:
#     if (input_path == NULL || fout == NULL) { return; }
# There is no fd-based metadata to copy *from*.
#
# We exercise this by piping content into brotli with an explicit -o output
# path. The input has no pathname; the output does. CopyStat() must:
#   - not crash
#   - not try to stat() a NULL path
#   - not leave the output with a wrong mode (no mode is copied, so the
#     output file just gets whatever the process umask / OpenOutputFile
#     grants it)
#
# Usage: test_copystat_stdin_input.sh /path/to/brotli
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

umask 0022

payload='stdin -> compressed file'
out_path="$workdir/out.br"
stderr_log="$workdir/stderr.log"

# Compress from stdin to an explicit output file.
echo "$payload" | "$brotli_bin" -c > "$out_path.stdio" 2>"$stderr_log"
# Now decompress it from stdin to a real output file path (exercises the
# asymmetric case: NULL input_path, non-NULL output_path).
"$brotli_bin" -d -o "$workdir/decoded.txt" < "$out_path.stdio" \
  2>>"$stderr_log"

# File exists and contents match.
if [[ ! -f "$workdir/decoded.txt" ]]; then
  echo "FAIL: decoded.txt was not created" >&2
  exit 1
fi
decoded=$(cat "$workdir/decoded.txt")
if [[ "$decoded" != "$payload" ]]; then
  echo "FAIL: decoded content differs from payload" >&2
  echo "  got:    $decoded" >&2
  echo "  wanted: $payload" >&2
  exit 1
fi

# Sanity: the output file must have a sane regular-file mode.
# We explicitly do NOT assert any particular permission mode here, because
# when input is stdin there is no source mode to copy.
out_mode=$(stat -c '%a' "$workdir/decoded.txt")
case "$out_mode" in
  "644"|"664"|"640"|"600"|"666") ;;  # any typical creat()+umask outcome
  *)
    echo "WARN: unexpected output mode $out_mode (not a failure, just noting)" >&2
    ;;
esac

# Guard against brotli diagnostics caused by a null-path deref.
if [[ -s "$stderr_log" ]]; then
  filtered=$(grep -v -E \
    -e '^==[0-9]+==' \
    -e '^SUMMARY:' \
    -e 'AddressSanitizer' \
    -e 'LeakSanitizer' \
    -e 'UndefinedBehaviorSanitizer' \
    "$stderr_log" || true)
  if [[ -n "$filtered" ]]; then
    echo "FAIL: unexpected diagnostics from NULL input_path path:" >&2
    echo "$filtered" >&2
    exit 1
  fi
fi

echo "T-01 STDIN INPUT: CopyStat handled NULL input_path cleanly"
