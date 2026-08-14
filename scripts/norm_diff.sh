#!/usr/bin/env bash
# normalize anon-struct address noise (p0x...) and diff stage1 vs stage2.
cd "$(dirname "$0")/.."
SAME=0; DIFF=0; DIFFL=""
for f in build/self_stage2/*.ll; do
    b="$(basename "$f" .ll)"
    b1="${b%.c}.ll"
    [ -f "build/self/$b1" ] || continue
    sed 's/p0x[0-9a-f]*//g' "$f" > /tmp/n2.ll
    sed 's/p0x[0-9a-f]*//g' "build/self/$b1" > /tmp/n1.ll
    if diff -q /tmp/n1.ll /tmp/n2.ll > /dev/null; then
        SAME=$((SAME+1))
    else
        DIFF=$((DIFF+1)); DIFFL="$DIFFL $b"
    fi
done
echo "normalized stage1-vs-stage2: identical=$SAME differ=$DIFF"
[ -n "$DIFFL" ] && echo "differing:$DIFFL"
