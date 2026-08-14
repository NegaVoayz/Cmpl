#!/usr/bin/env bash
# bisect.sh -- find which gaps-test construct crashes cmpl_self
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
SELF="$ROOT/build/self/cmpl_self"
I="-I$ROOT/include -I$ROOT"

mk() { # mk <name> <body-lines...>
    local name="$1"; shift
    {
        echo '#include <stdio.h>'
        echo 'enum { RED = 0, GREEN = 1, BLUE = 2 };'
        echo 'int main(void) { int rc = 0;'
        for l in "$@"; do echo "$l"; done
        echo 'if (!rc) printf("ok\n"); return rc; }'
    } > "/tmp/bis_$name.c"
}

mk a  'int a[3] = {[1] = 5}; if (a[1] != 5) rc |= 1;'
mk b  'int b[3] = {[1 + 1] = 9}; if (b[2] != 9) rc |= 2;'
mk c  'int c[3] = {[GREEN] = 4}; if (c[1] != 4) rc |= 4;'
mk d  'struct S { int a[4]; int z; }; struct S s = {1, 2, .a[2] = 5, .z = 7}; if (s.z != 7) rc |= 8;'
mk e  'struct S2 { int a[4]; int z; }; struct S2 s2 = {1, 2, .z = 7}; if (s2.z != 7) rc |= 16;'
mk f  'struct P { int x; int y; }; struct P arr[3] = {[2] = {7, 8}, [0] = {1, 2}}; if (arr[2].x != 7) rc |= 32;'
mk g  'int d[2][3] = {[1] = {4, 5, 6}}; if (d[1][2] != 6) rc |= 64;'
mk h  'int g2[] = {[BLUE] = 3}; if (g2[2] != 3) rc |= 128;'

for n in a b c d e f g h; do
    fails=0
    for i in 1 2 3 4 5 6 7 8; do
        "$SELF" -emit-llvm $I -o "/tmp/bis_$n.ll" "/tmp/bis_$n.c" > /dev/null 2>&1
        [ $? -ne 0 ] && fails=$((fails+1))
    done
    echo "case $n: fails=$fails/8"
done
