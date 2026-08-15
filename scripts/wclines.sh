#!/usr/bin/env bash
for f in "$@"; do
    printf "%4d %s\n" "$(wc -l < "$f")" "$f"
done | sort -rn
