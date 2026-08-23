#!/usr/bin/env bash
# lock.sh -- shared build/test lock for the self-host scripts (run_tests.sh,
# full_self.sh, build_self_linux.sh, rebuild_self2.sh).
#
# SOURCE it, don't exec it: `. "$ROOT/scripts/lock.sh"` -- the lock lives on
# fd 9 of the calling shell, so it must be acquired in the current process
# and stays held for that script's lifetime.
#
# Re-entrant: the first script in a process tree acquires build/.cmpl.lock
# and exports CMPL_LOCK_HELD=1; child scripts (sourced or spawned) that
# source this module see the flag and skip, so a driver calling a leaf
# script never deadlocks on its own lock.
#
# flock is advisory and auto-released when the holding process exits, so a
# crashed run never leaves a stale lock.  If flock is unavailable, the lock
# degrades to a no-op (pre-lock behavior).

if [ "${CMPL_LOCK_HELD:-0}" != 1 ]; then
    LOCK_ROOT="${LOCK_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"

    if command -v flock >/dev/null 2>&1; then
        mkdir -p "$LOCK_ROOT/build"
        exec 9>"$LOCK_ROOT/build/.cmpl.lock"
        if ! flock -n 9; then
            echo "cmpl: waiting for another build/test run (build/.cmpl.lock)..." >&2
            flock 9
        fi
        export CMPL_LOCK_HELD=1
    fi
fi
