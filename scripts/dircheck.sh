#!/usr/bin/env bash
# Report any directory with more than 9 .c files.
cd "$(dirname "$0")/.."
find . -name '*.c' -not -path './build/*' -not -path './test/*' -not -path './.git/*' \
  | sed 's|/[^/]*$||' | sort | uniq -c | sort -rn \
  | awk '{ if ($1 > 9) print }'
echo "dir-count check done"
