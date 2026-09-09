#!/usr/bin/env python3
"""spirv_localsize.py -- check that every entry point's workgroup size is
consistent between the LocalSize execution mode and the blockDim builtin.

`k<<<grid, block>>>` must reach SPIR-V twice, and both must agree:
  OpExecutionMode %entry LocalSize bx by bz
  %gl_WorkGroupSize = OpConstantComposite %uvec3 bx by bz   (blockDim)
spirv-val does NOT compare them, so a module can validate while every
kernel runs the wrong number of invocations (the emitter used to hard-code
both to 1 1 1).  This script parses the binary, associates each function
with the WorkgroupSize constant it reads, and compares.  It also rejects a
SIGNED blockDim constant: it validates, but lavapipe fails pipeline
creation with VK_ERROR_UNKNOWN (glslang emits uvec3).

Usage: python3 spirv_localsize.py file.spv [...]
Prints "kernel <name>: LocalSize=bx,by,bz blockDim=uvec3|ivec3|-".
Exit 0 = consistent; 1 = mismatch/unreadable.
"""

import struct
import sys

from spirv_ops import FIXED, ID_POS, ALL_ID

OP_ENTRY_POINT = 15
OP_EXECUTION_MODE = 16
OP_TYPE_INT = 21
OP_TYPE_VECTOR = 23
OP_CONSTANT = 43
OP_CONSTANT_COMPOSITE = 44
OP_FUNCTION = 54
OP_FUNCTION_END = 56
OP_DECORATE = 71

DECORATION_BUILTIN = 11
BUILTIN_WORKGROUP_SIZE = 25
MODE_LOCAL_SIZE = 17


def parse(data):
    """-> (words, [ (op, operands, index) ])"""
    words = list(struct.unpack("<%dI" % (len(data) // 4), data))
    instrs = []
    i = 5
    while i < len(words):
        wc = words[i] >> 16
        if wc == 0 or i + wc > len(words):
            break
        instrs.append((words[i] & 0xFFFF, list(words[i + 1:i + wc]), i))
        i += wc
    return words, instrs


def operand_ids(op, rest):
    if op in ALL_ID:
        return set(rest)
    if op in ID_POS:
        return {rest[p] for p in ID_POS[op] if p < len(rest)}
    return set()


def analyze(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 20 or len(data) % 4:
        return None, [f"{path}: bad size {len(data)}"]
    _, instrs = parse(data)

    entries = {}        # entry id -> name
    local = {}          # entry id -> (x, y, z)
    wgs_ids = set()     # ids decorated BuiltIn WorkgroupSize
    scalars = {}        # OpConstant id -> literal value
    composites = {}     # id -> tuple of resolved scalar values
    comp_ty = {}        # constant id -> its vector type id
    types = {}          # type id -> (opcode, operands)
    comp_signed = {}    # WorkgroupSize constant id -> signedness, or None

    for op, rest, _ in instrs:
        if op == OP_ENTRY_POINT:
            raw = b"".join(struct.pack("<I", x) for x in rest[2:])
            nz = raw.find(b"\x00")
            entries[rest[1]] = raw[:nz if nz >= 0 else len(raw)].decode(
                "utf-8", "replace")
        elif op == OP_EXECUTION_MODE and rest[1] == MODE_LOCAL_SIZE:
            local[rest[0]] = tuple(rest[2:5])
        elif op == OP_DECORATE and rest[1] == DECORATION_BUILTIN \
                and rest[2] == BUILTIN_WORKGROUP_SIZE:
            wgs_ids.add(rest[0])
        elif op == OP_CONSTANT:
            scalars[rest[1]] = rest[2]
        elif op == OP_CONSTANT_COMPOSITE:
            composites[rest[1]] = tuple(scalars.get(c, c) for c in rest[2:])
            comp_ty[rest[1]] = rest[0]
        elif op in (OP_TYPE_INT, OP_TYPE_VECTOR):
            types[rest[0]] = (op, rest)

    # the blockDim constant must be a vector of UNSIGNED 32-bit ints: a
    # signed one validates but no driver accepts it (lavapipe fails
    # vkCreateComputePipelines with VK_ERROR_UNKNOWN)
    for cid in wgs_ids:
        vec = types.get(comp_ty.get(cid))
        comp = types.get(vec[1][1]) if vec and vec[0] == OP_TYPE_VECTOR \
            and len(vec[1]) > 1 else None
        if not vec or vec[0] != OP_TYPE_VECTOR or vec[1][2] != 3:
            comp_signed[cid] = None
        elif not comp or comp[0] != OP_TYPE_INT or comp[1][1] != 32:
            comp_signed[cid] = None
        else:
            comp_signed[cid] = comp[1][2]

    # walk each function body, remembering which WorkgroupSize constants it
    # reads (a builtin read is an OpCompositeExtract/OpLoad operand)
    cur = None
    reads = {}          # function id -> set of WorkgroupSize constant ids
    for op, rest, _ in instrs:
        if op == OP_FUNCTION:
            cur = rest[1]
            reads.setdefault(cur, set())
        elif op == OP_FUNCTION_END:
            cur = None
        elif cur is not None:
            for i in operand_ids(op, rest):
                if i in wgs_ids:
                    reads[cur].add(i)

    problems = []
    for fid, name in sorted(entries.items(), key=lambda kv: kv[1]):
        ls = local.get(fid)
        if ls is None:
            problems.append(f"{path}: entry point '{name}' has no LocalSize")
            continue
        for cid in sorted(reads.get(fid, ())):
            val = composites.get(cid)
            if val != ls:
                problems.append(
                    f"{path}: entry point '{name}': blockDim {val} does not "
                    f"match LocalSize {ls}")
            if comp_signed.get(cid) == 1:
                problems.append(
                    f"{path}: entry point '{name}': blockDim is an ivec3 "
                    f"constant; WorkgroupSize must be unsigned (uvec3)")
    return (entries, local, reads, comp_signed), problems


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    rc = 0
    for path in argv[1:]:
        info, problems = analyze(path)
        if problems:
            rc = 1
            print(f"INVALID {path}")
            for p in problems:
                print(f"  {p}")
            continue
        entries, local, reads, comp_signed = info
        print(f"OK      {path}")
        for fid, name in sorted(entries.items(), key=lambda kv: kv[1]):
            x, y, z = local[fid]
            kind = "-"
            for cid in reads.get(fid, ()):
                if comp_signed.get(cid) == 0:
                    kind = "uvec3"
                elif comp_signed.get(cid) == 1:
                    kind = "ivec3"
            print(f"  kernel {name}: LocalSize={x},{y},{z} blockDim={kind}")
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv))
