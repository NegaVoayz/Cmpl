#!/usr/bin/env bash
# Count files over 200 lines and print them.
cd "$(dirname "$0")/.."
bash scripts/count_lines.sh > /tmp/lines.txt
echo "TOTAL files over 200: $(awk '$1 > 200' /tmp/lines.txt | wc -l)"
awk '$1 > 200 {print}' /tmp/lines.txt
