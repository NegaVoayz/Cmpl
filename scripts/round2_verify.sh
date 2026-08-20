#!/usr/bin/env bash
# round2_verify.sh -- full verification battery: bootstrap, run_tests
# (A/B/C), full_self (corpus via cmpl_self + stage-2), stage-3, Stage C
# via cmpl_self2/3, self_o1 (-O1 self-build convergence), diff_o1
# (full-corpus -O1 differential), and diff_gcc.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

bash scripts/build_bootstrap.sh || exit 1

echo "########## run_tests.sh (A/B/C) ##########"
STDOUT_DIFF=1 bash scripts/run_tests.sh

echo "########## full_self.sh (corpus via cmpl_self + stage-2) ##########"
bash scripts/full_self.sh

echo "########## stage-3 ##########"
bash scripts/stage3_self.sh 2>&1 | grep -E "stage-3|corpus|normalized|identical|differ|LINKFAIL|failed:"

echo "########## Stage C via cmpl_self2 / cmpl_self3 ##########"
bash scripts/run_stageC_via.sh build/self_stage2/cmpl_self2 build/self_stage2 2>&1 | grep -E "Stage C|failed:"
bash scripts/run_stageC_via.sh build/self_stage3/cmpl_self3 build/self_stage3 2>&1 | grep -E "Stage C|failed:"

echo "########## self_o1 (-O1 self-build convergence) ##########"
bash scripts/self_o1.sh || exit 1

echo "########## diff_o1 (full-corpus -O1) ##########"
bash scripts/diff_o1.sh || exit 1

echo "########## diff_gcc ##########"
bash scripts/diff_gcc.sh | tail -3
