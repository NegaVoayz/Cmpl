#!/bin/bash
find . -name '*.c' -not -path './build/*' -not -path './test/*' | while read -r f; do
    n=$(wc -l < "$f")
    printf "%6d %s\n" "$n" "$f"
done | sort -rn
