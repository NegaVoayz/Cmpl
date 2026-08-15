#!/usr/bin/env bash
cd "$(dirname "$0")/.."
find . -name '*.c' -not -path './build/*' -not -path './test/*' -not -path './.git/*' \
  | sed 's|/[^/]*$||' | sort | uniq -c | sort -rn
