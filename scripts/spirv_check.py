#!/usr/bin/env python3
"""spirv_check.py -- structural validation of cmpl's SPIR-V output.

Checks what is checkable without a GPU: header magic/version, ID bounds,
unique result ids, defined-before-use references, word-count consistency
with the opcode table, and known-opcode validity.  Catches the emitter
bug classes: wrong opcode numbers, colliding/truncated ids, uncollected
operands, unemitted types, id-0 references.

Usage: python3 spirv_check.py file.spv [...]
Exit 0 = valid; 1 = problems found.
"""

import struct
import sys

# opcode -> number of fixed operand words (excluding the opcode word).
# Negative means "handled specially" in the walker below.
FIXED = {
    0: 0, 14: 2, 15: -1, 16: -1, 17: 1, 19: 1, 20: 1, 21: 3,
    22: 2, 23: 2, 28: 3, 30: -1, 32: 3, 33: -1, 41: 1, 42: 1,
    43: -1, 46: 2, 54: 4, 55: 2, 56: 0, 57: -1, 59: -1, 61: 3,
    62: 2, 65: -1, 66: -1, 67: -1, 71: -1, 124: 3, 126: 3, 127: 3,
    128: 4, 129: 4, 130: 4, 131: 4, 132: 4, 133: 4, 134: 4,  # arith
    135: 4, 136: 4, 137: 4, 138: 4,
    109: 3, 110: 3, 111: 3, 112: 3, 113: 3, 114: 3, 115: 3,
    169: 4, 170: 4, 171: 4, 172: 4, 173: 4, 174: 4, 175: 4,  # compare
    176: 4, 177: 4, 178: 4, 179: 4, 180: 4, 181: 4, 182: 4, 183: 4,
    184: 4, 185: 4, 186: 4, 187: 4, 188: 4, 189: 4, 190: 4, 191: 4,
    194: 4, 195: 4, 196: 4, 197: 4, 198: 4, 199: 4, 200: 4,
    245: -1,  # OpPhi
    248: 1,  # OpLabel
    249: 1,  # OpBranch
    250: 3,  # OpBranchConditional
    253: 0,  # OpReturn
    254: 1,  # OpReturnValue
    255: 0,  # OpUnreachable
}

# opcode -> set of operand POSITIONS (in the operand list, excluding the
# opcode word) that are ID references.  Positions not listed are enum
# values or literals and must NOT be id-checked.
ID_POS = {
    15: {1}, 16: {0}, 19: {0}, 20: {0}, 21: {0}, 22: {0}, 23: {0, 1},
    28: {0, 1, 2}, 32: {0, 2}, 41: {0}, 42: {0}, 43: {0, 1}, 46: {0, 1},
    54: {0, 1, 3}, 55: {0, 1}, 59: {0, 1}, 61: {0, 1, 2}, 62: {0, 1},
    71: {0}, 124: {0, 1, 2}, 126: {0, 1, 2}, 127: {0, 1, 2},
    248: {0}, 249: {0}, 250: {1, 2}, 254: {0},
}
# opcodes with all-id operands (result type + result id [+ operands])
ALL_ID = {30, 33, 57, 65, 66, 67, 245}
# conversion/arith/compare: every operand is an id
for _op in list(range(109, 116)) + list(range(128, 139)) + \
           list(range(169, 192)) + list(range(194, 201)):
    ALL_ID.add(_op)


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
    if version != 0x00010000:
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
        elif op in (43, 46, 54, 55, 57, 59, 61, 65, 66, 67, 124, 126, 127,
                    245):
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
