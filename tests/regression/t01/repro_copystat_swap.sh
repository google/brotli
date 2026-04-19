#!/usr/bin/env bash
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
  "$brotli_bin" -d -o "$out_path" "$workdir/plain.bin.br"

if [[ ! -L "$out_path" ]]; then
  echo "output path was not swapped to a symlink" >&2
  exit 1
fi
if [[ "$(stat -c '%a' "$target_path")" != "644" ]]; then
  echo "target mode was not flipped to 0644" >&2
  exit 1
fi
if ! cmp -s -- "$workdir/target.txt" "$script_dir/target.txt"; then
  echo "target contents changed; expected metadata-only effect" >&2
  exit 1
fi

printf 'T-01 OK: target mode flipped to %s via post-close CopyStat path\n' \
  "$(stat -c '%a' "$target_path")"
