#!/usr/bin/env python3
"""Scan .c files for functions longer than 80 lines (heuristic brace counter)."""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKIP = ('build', 'test', '.git')

def scan(path):
    with open(path, encoding='utf-8', errors='replace') as fh:
        lines = fh.readlines()
    funcs = []
    depth = 0
    cur = None      # (name, start_line)
    brace_only = 0  # depth attributable to pure brace lines (struct/array defs)
    for i, raw in enumerate(lines, 1):
        line = raw.rstrip('\n')
        stripped = line.strip()
        if not stripped or stripped.startswith('//') or stripped.startswith('/*') or stripped.startswith('*'):
            continue
        # count braces on this line
        opens = stripped.count('{')
        closes = stripped.count('}')
        if cur is None and opens:
            # heuristic: function definition starts at a line with '{' not preceded by ';' '}' ')' alone
            prev = lines[i-2].rstrip('\n').strip() if i >= 2 else ''
            if (prev and not prev.endswith((';', '}', '{', ':')) and re.search(r'[a-zA-Z_]\w*\s*\(', prev)) or \
               re.search(r'\)\s*\{', stripped) or opens > closes or stripped.endswith('{'):
                name_m = re.search(r'([a-zA-Z_]\w*)\s*\(', prev or stripped)
                cur = [name_m.group(1) if name_m else '?', i]
        if cur is not None:
            # nested funcs inside could confuse; track net depth
            if opens or closes:
                depth += opens - closes
                if depth <= 0:
                    funcs.append((cur[0], cur[1], i, i - cur[1] + 1))
                    cur = None
                    depth = 0
    return funcs

def main():
    total = []
    for dirpath, dirs, files in os.walk(ROOT):
        dirs[:] = [d for d in dirs if d not in SKIP]
        for fn in files:
            if fn.endswith('.c'):
                p = os.path.join(dirpath, fn)
                for name, start, end, length in scan(p):
                    if length > 80:
                        total.append((length, os.path.relpath(p, ROOT), name, start, end))
    total.sort(reverse=True)
    for length, path, name, start, end in total:
        print(f"{length:5d}  {path}:{start}-{end}  {name}")
    print(f"\n{len(total)} functions over 80 lines")

if __name__ == '__main__':
    main()
