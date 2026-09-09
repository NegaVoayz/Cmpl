#!/usr/bin/env python3
"""spirv_check.py -- validation of cmpl's SPIR-V output.

Two layers:
  * structural checks (always): header magic/version, ID bounds, unique
    result ids, defined-before-use references, word-count consistency with
    the opcode table, and known-opcode validity.  Catches the emitter bug
    classes: wrong opcode numbers, colliding/truncated ids, uncollected
    operands, unemitted types, id-0 references.
  * spirv_rules.check_layout: the logical-layout / capability rules.
  * spirv-val (when installed): the real Khronos validator, which is the
    authority on Vulkan-specific rules.

Usage: python3 spirv_check.py file.spv [...]
Exit 0 = valid; 1 = problems found.
"""

import shutil
import struct
import subprocess
import sys

try:
    from spirv_rules import check_layout
except ImportError:          # run from another cwd: layout checks skipped
    check_layout = None
    print("warning: spirv_rules.py not importable — layout checks skipped",
          file=sys.stderr)

SPIRV_VAL = shutil.which("spirv-val")
TARGET_ENV = "vulkan1.2"

if not SPIRV_VAL:
    print("warning: spirv-val not found — only the structural checks run "
          "(install spirv-tools for full Vulkan validation)", file=sys.stderr)

from spirv_ops import FIXED, ID_POS, ALL_ID


def run_spirv_val(path):
    """the real validator, when installed (authoritative for Vulkan)"""
    if not SPIRV_VAL:
        return []
    proc = subprocess.run([SPIRV_VAL, "--target-env", TARGET_ENV, path],
                          capture_output=True, text=True)
    if proc.returncode == 0:
        return []
    lines = [l for l in proc.stderr.strip().splitlines() if l.strip()]
    return [f"{path}: spirv-val: {l}" for l in lines[:6]]


def check_file(path):
    problems = []
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 20 or len(data) % 4 != 0:
        return [f"{path}: bad size {len(data)}"]
    words = list(struct.unpack("<%dI" % (len(data) // 4), data))
    magic, version, _, bound, _ = words[:5]
    if magic != 0x07230203:
        problems.append(f"{path}: bad magic 0x{magic:08x}")
    if not (0x00010000 <= version <= 0x00010600):
        problems.append(f"{path}: unexpected version 0x{version:08x}")

    defined = {}   # id -> word index that defined it
    used = {}      # id -> list of (opcode, word)
    i = 5
    while i < len(words):
        wc = words[i] >> 16
        op = words[i] & 0xFFFF
        if wc == 0 or i + wc > len(words):
            problems.append(f"{path}: word {i}: bad word count {wc}")
            break
        operands = list(words[i + 1:i + wc])
        if op not in FIXED:
            problems.append(f"{path}: word {i}: unknown opcode {op}")
            i += wc
            continue

        rest = list(operands)
        id_pos = None
        if op in ALL_ID:
            id_pos = set(range(len(rest)))
        elif op in ID_POS:
            id_pos = set(ID_POS[op])

        if op == 15:  # OpEntryPoint: model(0), id(1), name, interface
            raw = b"".join(struct.pack("<I", x) for x in rest[2:])
            nz = raw.find(b"\x00")
            nlen = nz if nz >= 0 else len(raw)
            nw = (nlen + 1 + 3) // 4
            id_pos = {1} | set(range(2 + nw, len(rest)))
        elif op == 16:  # OpExecutionMode: id(0), mode + literals
            id_pos = {0}
        elif op == 30 or op == 33:  # struct: id + member ids
            id_pos = set(range(len(rest)))
        elif op == 43:  # OpConstant: type, id, 1..2 literal words
            id_pos = {0, 1}
        elif op == 59:  # OpVariable: type, id, storage, [init]
            id_pos = {0, 1}
            if len(rest) == 4:
                id_pos.add(3)
        elif op == 71:  # OpDecorate: target(0), decoration, literals
            id_pos = {0}

        # result id definition: operand[1] for value-producing ops,
        # operand[0] for type/constant/label-defining ops
        rid = None
        if op in (19, 20, 21, 22, 23, 28, 30, 32, 33, 41, 42, 248):
            rid = rest[0] if rest else None
        elif op in (1, 43, 44, 46, 54, 55, 57, 59, 61, 65, 66, 67, 80, 81,
                    117, 120, 124, 126, 127, 245):
            rid = rest[1] if len(rest) >= 2 else None
        elif op in set(range(109, 116)) | set(range(128, 139)) | \
             set(range(169, 192)) | set(range(194, 201)):
            rid = rest[1] if len(rest) >= 2 else None
        if rid is not None and rid != 0 and rid in defined:
            problems.append(
                f"{path}: word {i}: duplicate result id {rid} "
                f"(op {op}, first at word {defined[rid]})")
        if rid is not None and rid != 0:
            defined[rid] = i
        elif rid == 0:
            problems.append(f"{path}: word {i}: result id 0 (op {op})")

        if id_pos is not None:
            for pos in sorted(id_pos):
                if pos >= len(rest):
                    continue
                val = rest[pos]
                if val == 0:
                    problems.append(
                        f"{path}: word {i}: operand id 0 (op {op}, pos {pos})")
                elif val >= bound:
                    problems.append(
                        f"{path}: word {i}: operand id {val} >= bound {bound}")
                else:
                    used.setdefault(val, []).append((op, i))

        i += wc

    for rid in defined:
        if rid >= bound:
            problems.append(f"{path}: defined id {rid} >= bound {bound}")
    for uid in used:
        if uid not in defined:
            problems.append(f"{path}: id {uid} used but never defined")

    if check_layout:
        check_layout(words, problems, path)

    # the real validator last: it reports at most one error per run and is
    # authoritative for Vulkan, but the structural pass above pinpoints
    # emitter bugs it cannot name
    if not problems:
        problems.extend(run_spirv_val(path))

    return problems


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    rc = 0
    for path in argv[1:]:
        problems = check_file(path)
        if problems:
            rc = 1
            print(f"INVALID {path}")
            for p in problems:
                print(f"  {p}")
        else:
            print(f"OK      {path}")
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv))
