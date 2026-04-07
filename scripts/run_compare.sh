#!/usr/bin/env bash
# Run a Layer-3 soundness check on a single C source file.
#
# Usage: run_compare.sh <test.c>
# Env:
#   SVF_DIR     — root of SVF source (default: ../SVF relative to this script)
#   TS_SVF      — path to ts-svf binary (default: <build>/ts-svf)
#
# Exit 0 if our points-to set is a sound superset of LLVM/wpa's on the
# variables that exist in both. Non-zero on UNSOUND or MISMATCH.

set -eu
src="$1"
script_dir="$(cd "$(dirname "$0")" && pwd)"
proj_dir="$(dirname "$script_dir")"

: "${SVF_DIR:=$(cd "$proj_dir/.." && pwd)/SVF}"
: "${TS_SVF:=$proj_dir/build/ts-svf}"
CLANG="$SVF_DIR/llvm-18.1.0.obj/bin/clang"
WPA="$SVF_DIR/Release-build/bin/wpa"
export LD_LIBRARY_PATH="$SVF_DIR/Release-build/lib:$SVF_DIR/z3.obj/bin:${LD_LIBRARY_PATH:-}"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
ll="$tmp/in.ll"

"$CLANG" -S -emit-llvm -fno-discard-value-names -O0 "$src" -o "$ll" 2>/dev/null
"$WPA"  -ander -print-all-pts "$ll" 2>/dev/null \
    | python3 "$script_dir/extract_pts.py" wpa > "$tmp/expected.txt"
"$TS_SVF" --dump-pts "$src" 2>/dev/null \
    | python3 "$script_dir/extract_pts.py" ts > "$tmp/actual.txt"
python3 "$script_dir/compare_pts.py" "$tmp/expected.txt" "$tmp/actual.txt"
