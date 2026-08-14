#!/usr/bin/env bash
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BOOT="$ROOT/build/bootstrap/cmpl"
I="-I$ROOT/include -I$ROOT"

for p in probe_z probe_localstruct probe_idx_edge probe_signinit probe_desig3 probe_p3; do
    "$BOOT" -emit-llvm $I -o /tmp/vp.ll "build/$p.c" > /dev/null 2>&1
    clang /tmp/vp.ll -o /tmp/vp > /dev/null 2>&1
    /tmp/vp > /tmp/vpo.log 2>&1
    echo "$p: rc=$? out=$(head -c 70 /tmp/vpo.log | tr '\n' ' ')"
done
