#!/usr/bin/env bash
# batch_check.sh -- B-42 regression test: `cmpl batch` .ll output must be
# byte-identical (modulo the ASLR p0x suffix) to the per-process .ll output.
#
# Compiles every source in src_list.sh two ways with the SAME binary:
#   per-process (one cmpl invocation per file)  -> build/batch_check/one
#   batch (one invocation for all files)        -> build/batch_check/batch
# then cmp-s each normalized pair.  Anonymous struct names embed the type's
# pointer address as "%struct.anon.N.p0x<hex>" (ASLR-dependent), so both
# sides are sed-normalized first, exactly like scripts/norm_diff.sh.
# A 2-TU smoke additionally exercises the "two files in one process vs two
# processes" audit with an anonymous struct and a string literal.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
CMPL="$ROOT/build/bootstrap/cmpl"
OUT="$ROOT/build/batch_check"
mkdir -p "$OUT/one" "$OUT/batch"

source "$ROOT/scripts/src_list.sh"
SOURCES=("${CMPL_SOURCES[@]}")

# must match build_self_linux.sh line 29 exactly (order matters)
I_ARGS_STR="-Iinclude -Ibase -I. -Itokenizer -Ipp -Ipp/inc -Iparser -Iparser/lr -Iparser/ll -Iast-opt -Iir -Iir/builder -Iir/type -Iir/dump -Iir/dump/instr -Iir-opt -Igpu -Ivulkan -Illvm-codegen"

flat() { local s="$1"; s="${s//\//_}"; s="${s%.c}"; echo "$s"; }

# --- per-process baseline ---------------------------------------------------
P=0; F=0
for src in "${SOURCES[@]}"; do
    base="$(flat "$src")"
    if ! "$CMPL" -emit-llvm $I_ARGS_STR -o "$OUT/one/$base.ll" "$src" \
         >/dev/null 2>"$OUT/one/$base.err"; then
        echo "baseline FAIL (cmpl): $src"; F=$((F+1)); continue
    fi
    P=$((P+1))
done
echo "baseline per-process: PASS=$P FAIL=$F"

# --- batch ------------------------------------------------------------------
printf '%s\n' "${SOURCES[@]}" > "$OUT/filelist.txt"
if ! "$CMPL" batch -emit-llvm $I_ARGS_STR -o "$OUT/batch" \
     "$OUT/filelist.txt" 2>"$OUT/batch.err"; then
    echo "batch FAILED (exit $?)"
    tail -5 "$OUT/batch.err"
    exit 1
fi

# --- byte-identity (p0x-normalized) -----------------------------------------
IDENT=0; DIFF=0
for src in "${SOURCES[@]}"; do
    base="$(flat "$src")"
    if ! cmp -s <(sed 's/p0x[0-9a-f]*//g' "$OUT/one/$base.ll") \
                <(sed 's/p0x[0-9a-f]*//g' "$OUT/batch/$base.ll"); then
        echo "DIFF: $src"; DIFF=$((DIFF+1))
    else
        IDENT=$((IDENT+1))
    fi
done
echo "byte-identical: $IDENT differ: $DIFF"

# --- 2-TU smoke: anon struct (p0x path) + string literal --------------------
cat > "$OUT/t1.c" <<'EOF'
struct { int x; int y; } g1;
int f1(void) { return g1.x + g1.y; }
EOF
cat > "$OUT/t2.c" <<'EOF'
const char* g2 = "hello batch";
int f2(void) { return g2[0]; }
EOF
printf '%s\n' "$OUT/t1.c" "$OUT/t2.c" > "$OUT/smoke.list"
"$CMPL" batch -emit-llvm $I_ARGS_STR -o "$OUT/batch" "$OUT/smoke.list" 2>/dev/null
"$CMPL" -emit-llvm $I_ARGS_STR -o "$OUT/one/t1.ll" "$OUT/t1.c" >/dev/null 2>&1
"$CMPL" -emit-llvm $I_ARGS_STR -o "$OUT/one/t2.ll" "$OUT/t2.c" >/dev/null 2>&1
SMOKE=0
for s in "$OUT/t1.c" "$OUT/t2.c"; do
    fb="$(flat "$s")"
    b="$(basename "$s" .c)"
    if ! cmp -s <(sed 's/p0x[0-9a-f]*//g' "$OUT/one/$b.ll") \
                <(sed 's/p0x[0-9a-f]*//g' "$OUT/batch/$fb.ll"); then
        echo "smoke DIFF: $b"; SMOKE=$((SMOKE+1))
    fi
done
echo "2-TU smoke: $([ $SMOKE -eq 0 ] && echo PASS || echo FAIL)"

if [ "$F" -gt 0 ] || [ "$DIFF" -gt 0 ] || [ "$SMOKE" -gt 0 ]; then
    echo "batch_check: FAIL"
    exit 1
fi
echo "batch_check: PASS ($IDENT identical, $P baseline ok)"
