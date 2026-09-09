#!/usr/bin/env bash
# scripts/build_win_cross.sh -- cross-build cmpl.exe with clang + the MSVC ABI.
#
# Used where no native Windows clang is installed: clang (Linux) compiles
# every source to a COFF object against the MSVC/UCRT headers that the
# Visual Studio + Windows SDK installation on the Windows side provides,
# and the native link.exe turns them into build/win-cross/cmpl.exe
# (scripts/build_win_cross.ps1).
#
# The repo's own include/ directory is deliberately NOT on the include path:
# its libc headers are the freestanding stubs cmpl uses when it PARSES a
# program (x86-64 SysV va_list), not headers for compiling cmpl itself.
#
# Usage: bash scripts/build_win_cross.sh
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT/scripts/src_list.sh"

OUT="$ROOT/build/win-cross"
SDK="/mnt/c/Program Files (x86)/Windows Kits/10/Include"
MSVC="/mnt/c/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC"
SDKVER="$(ls "$SDK" | sort -V | tail -1)"
MSVCVER="$(ls "$MSVC" | sort -V | tail -1)"

mkdir -p "$OUT"
rm -f "$OUT"/*.o "$OUT"/*.err

# one argument per line; clang reads it with @file.  Quoted because the
# Windows SDK path contains spaces.
{
  for d in base tokenizer pp pp/inc parser parser/lr parser/ll ast-opt ir \
           ir/builder ir/type ir/dump ir/dump/instr ir-opt gpu vulkan llvm-codegen; do
    printf -- '-I"%s"\n' "$ROOT/$d"
  done
  printf -- '-I"%s"\n' "$ROOT"
  # MSVC has no <dirent.h>; the shim implements it with FindFirstFileA
  printf -- '-I"%s"\n' "$ROOT/scripts/win_compat"
  for d in ucrt shared um; do printf -- '-isystem "%s"\n' "$SDK/$SDKVER/$d"; done
  printf -- '-isystem "%s"\n' "$MSVC/$MSVCVER/include"
} > "$OUT/args.rsp"

echo "=== compiling ${#CMPL_SOURCES[@]} sources for x86_64-pc-windows-msvc ==="
printf '%s\n' "${CMPL_SOURCES[@]}" | xargs -P "${JOBS:-8}" -I{} bash -c '
  src="$1"
  obj="'"$OUT"'/$(printf %s "$src" | tr "/" "_").o"
  # -fno-ms-compatibility: MS compatibility makes static_assert a keyword,
  # and the AST has a member of that name (ast_node.h)
  clang --target=x86_64-pc-windows-msvc -fno-ms-compatibility -O1 -w -c "$src" \
        "@'"$OUT"'/args.rsp" -o "$obj" 2>"$obj.err" || {
    echo "FAIL: $src"; head -3 "$obj.err"; }
' _ {}

n_obj=$(ls "$OUT"/*.o 2>/dev/null | wc -l)
echo "objects: $n_obj / ${#CMPL_SOURCES[@]}"
if [ "$n_obj" -ne "${#CMPL_SOURCES[@]}" ]; then
  echo "FAIL: missing objects"; exit 1
fi
echo "OK -- now run scripts/build_win_cross.ps1 to link cmpl.exe"
