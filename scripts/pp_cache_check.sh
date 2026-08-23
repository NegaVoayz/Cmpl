#!/usr/bin/env bash
# pp_cache_check.sh -- B-43 regression: the pp checkpoint cache must be
# byte-identical to fresh preprocessing (batch vs per-process) AND actually
# fire (CMPL_PP_CACHE_STATS hits > 0).
#
# Corpus: guarded / unguarded / nested / macro-state / undef-redefine headers
# included by TUs that share some pre-states and diverge on others.  Every TU
# is compiled twice with the SAME binary:
#   per-process (fresh; cache is NULL) -> build/pp_cache_check/one
#   batch (shared cache)               -> build/pp_cache_check/batch
# and each pair cmp'd (p0x-normalized).  The batch stderr must show hits>0,
# proving one TU stored an entry a later TU replayed.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
CMPL="$ROOT/build/bootstrap/cmpl"
OUT="$ROOT/build/pp_cache_check"
HDR="$OUT/hdr"
TU="$OUT/tu"
mkdir -p "$HDR" "$TU" "$OUT/one" "$OUT/batch"

flat() { local s="$1"; s="${s//\//_}"; s="${s%.c}"; echo "$s"; }

# --- adversarial headers ------------------------------------------------------
cat > "$HDR/guard.h" <<'EOF'
#ifndef GUARD_H
#define GUARD_H
int guarded_x = 1;
#endif
EOF

cat > "$HDR/unguarded.h" <<'EOF'
int unguarded_y = 2;
EOF

cat > "$HDR/nested.h" <<'EOF'
#include "guard.h"
#include "unguarded.h"
EOF

cat > "$HDR/cond.h" <<'EOF'
#if defined(EXTRA_FEATURE)
int extra_z = 5;
#else
int extra_z = 3;
#endif
EOF

cat > "$HDR/undef_red.h" <<'EOF'
#define TEMP_MACRO 1
#undef TEMP_MACRO
int after_undef = 7;
EOF

# --- TUs (one per attack axis) ------------------------------------------------
cat > "$TU/t1.c" <<'EOF'
#include "guard.h"
#include "unguarded.h"
#include "nested.h"
int main(void) { return 0; }
EOF
cp "$TU/t1.c" "$TU/t2.c"   # identical TU -> must cache-hit all three headers

cat > "$TU/t3.c" <<'EOF'
#define EXTRA_FEATURE 1
#include "cond.h"
int main(void) { return 0; }
EOF

cat > "$TU/t4.c" <<'EOF'
#include "cond.h"
int main(void) { return 0; }
EOF

cat > "$TU/t5.c" <<'EOF'
#define GUARD_H
#include "guard.h"
#include "guard.h"
int main(void) { return 0; }
EOF

cat > "$TU/t6.c" <<'EOF'
#include "undef_red.h"
#define TEMP_MACRO 9
int main(void) { return TEMP_MACRO; }
EOF
cp "$TU/t6.c" "$TU/t7.c"   # identical TU -> must cache-hit undef_red.h

FILES=(t1 t2 t3 t4 t5 t6 t7)
I_ARGS="-I$HDR"

# --- per-process baseline -----------------------------------------------------
P=0; F=0
for t in "${FILES[@]}"; do
    if ! "$CMPL" -emit-llvm $I_ARGS -o "$OUT/one/$t.ll" "$TU/$t.c" \
         >/dev/null 2>"$OUT/one/$t.err"; then
        echo "baseline FAIL (cmpl): $t"; F=$((F+1)); continue
    fi
    P=$((P+1))
done
echo "pp_cache baseline per-process: PASS=$P FAIL=$F"

# --- batch (shared cache, stats on) ------------------------------------------
: > "$OUT/filelist.txt"
for t in "${FILES[@]}"; do
    echo "$TU/$t.c" >> "$OUT/filelist.txt"
done

CMPL_PP_CACHE_STATS=1 "$CMPL" batch -emit-llvm $I_ARGS -o "$OUT/batch" \
    "$OUT/filelist.txt" 2>"$OUT/batch.err"
if [ $? -ne 0 ]; then
    echo "pp_cache batch FAILED (exit $?)"
    tail -5 "$OUT/batch.err"
    exit 1
fi

# --- byte-identity (p0x-normalized) ------------------------------------------
IDENT=0; DIFF=0
for t in "${FILES[@]}"; do
    fb="$(flat "$TU/$t.c")"
    if ! cmp -s <(sed 's/p0x[0-9a-f]*//g' "$OUT/one/$t.ll") \
                <(sed 's/p0x[0-9a-f]*//g' "$OUT/batch/$fb.ll"); then
        echo "pp_cache DIFF: $t"; DIFF=$((DIFF+1))
    else
        IDENT=$((IDENT+1))
    fi
done
echo "pp_cache byte-identical: $IDENT differ: $DIFF"

# --- the cache must fire (hits > 0) ------------------------------------------
HITS=$(grep -oE '[0-9]+ hits' "$OUT/batch.err" | grep -oE '[0-9]+' | head -1)
HITS=${HITS:-0}
echo "pp_cache stats: $HITS hits"

if [ "$F" -gt 0 ] || [ "$DIFF" -gt 0 ] || [ "$HITS" -lt 1 ]; then
    echo "pp_cache_check: FAIL"
    exit 1
fi
echo "pp_cache_check: PASS ($IDENT identical, $HITS cache hits)"
