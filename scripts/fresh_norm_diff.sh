#!/usr/bin/env bash
# fresh_norm_diff.sh -- stage1-vs-stage2 normalized IR diff restricted to
# the CURRENT 122-source layout (excludes stale artifacts from old layouts).
set -u
cd "$(dirname "$0")/.."
sed -n '/^SOURCES=(/,/^)/p' scripts/build_self_linux.sh | grep -oE '[A-Za-z0-9_./-]+\.c' | sort -u > /tmp/srcs2.txt
SAME=0; DIFF=0; NOSTAGE2=0; DIFFL=""
while read s; do
  b=$(echo "$s" | tr '/' '_')   # ast-opt/ast_walk.c -> ast-opt_ast_walk.c
  b1="${b%.c}.ll"               # stage-1 name (build_self strips .c)
  b2="${b}.ll"                  # stage-2 name (rebuild_self2 keeps .c)
  if [ -f "build/self_stage2/$b2" ]; then
    sed 's/p0x[0-9a-f]*//g' "build/self/$b1" > /tmp/n1.ll
    sed 's/p0x[0-9a-f]*//g' "build/self_stage2/$b2" > /tmp/n2.ll
    if diff -q /tmp/n1.ll /tmp/n2.ll > /dev/null 2>&1; then SAME=$((SAME+1)); else DIFF=$((DIFF+1)); DIFFL="$DIFFL $b"; fi
  else NOSTAGE2=$((NOSTAGE2+1)); fi
done < /tmp/srcs2.txt
echo "fresh-only: identical=$SAME differ=$DIFF no-stage2=$NOSTAGE2"
[ -n "$DIFFL" ] && echo "differing:$DIFFL"
