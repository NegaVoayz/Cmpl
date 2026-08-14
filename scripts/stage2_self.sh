#!/usr/bin/env bash
# stage2_self.sh -- the stage-2 self-host check: rebuild cmpl_self2 from
# cmpl_self, run the whole test corpus through it, and compare stage-1 vs
# stage-2 IR (normalized).  This is the strongest self-host signal: a
# correct self-hosting compiler must reproduce its own IR byte-for-byte,
# modulo anon-name noise and the known benign fold-quality diffs.
#
# Reuses scripts/rebuild_self2.sh (stage-2 rebuild + corpus) and
# scripts/norm_diff.sh (normalized IR diff) rather than duplicating them.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo "########## stage-2 rebuild + corpus ##########"
bash scripts/rebuild_self2.sh

echo ""
echo "########## stage-1 vs stage-2 normalized IR diff ##########"
bash scripts/norm_diff.sh
