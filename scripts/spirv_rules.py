#!/usr/bin/env python3
"""spirv_rules.py -- SPIR-V logical-layout / capability validation.

Checks that spirv_check.py's id-based pass cannot express:
  * OpCapability before OpMemoryModel
  * entry points and execution modes before decorations (section 5/6 < 8)
  * decorations before types/constants/globals (section 8 < 9)
  * no type/constant/global after the first OpFunction (section 9 < 11)
  * Function-storage OpVariable first in the function's first block
  * capabilities required by 8/16/64-bit ints and 64-bit floats
  * builtin types (WorkgroupId/LocalInvocationId/NumWorkgroups: Input
    3-component 32-bit int vectors; WorkgroupSize: a constant, not a
    variable) and void entry points
  * Workgroup variables listed in every entry point interface

check_layout(words, problems) appends one message per violation.
"""
from spirv_rules_entry import check_workgroup_interface, check_entry_points

OP_UNDEF, OP_MEMORY_MODEL, OP_ENTRY_POINT, OP_EXECUTION_MODE = 1, 14, 15, 16
OP_CAPABILITY = 17
OP_TYPE_VOID, OP_TYPE_INT, OP_TYPE_FLOAT = 19, 21, 22
OP_TYPE_VECTOR, OP_TYPE_POINTER = 23, 32
OP_CONSTANT, OP_CONSTANT_COMPOSITE = 43, 44
OP_FUNCTION, OP_FUNCTION_PARAMETER, OP_FUNCTION_END = 54, 55, 56
OP_VARIABLE, OP_DECORATE, OP_LABEL = 59, 71, 248

DECORATION_BUILTIN = 11
STORAGE_INPUT, STORAGE_WORKGROUP, STORAGE_FUNCTION = 1, 4, 7
# BuiltIn values verified against spirv-as: the table used to be shifted
# (21 = HelperInvocation), which no validator accepts.
BUILTIN_NUM_WORKGROUPS, BUILTIN_WORKGROUP_SIZE = 24, 25
BUILTIN_WORKGROUP_ID, BUILTIN_LOCAL_INVOCATION_ID = 26, 27
CAP_FLOAT64, CAP_INT64, CAP_INT16, CAP_INT8 = 10, 11, 22, 39

TYPE_OPS = {19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 39}
CONST_OPS = {41, 42, 43, 44, 45, 46, 48, 50, 51, 52, 53}
ANN_OPS = {71, 72, 73, 74, 75}
# instructions that only occur inside a function body
BODY_OPS = {80, 81, 117, 120, 246, 247}


def _section(op, rest):
    """logical-layout section number (0 = ignore in the order check)"""
    if op == OP_CAPABILITY:
        return 1
    if op == OP_MEMORY_MODEL:
        return 4
    if op in (OP_ENTRY_POINT, OP_EXECUTION_MODE):
        return 5
    if op in ANN_OPS:
        return 8
    if op in TYPE_OPS or op in CONST_OPS:
        return 9
    if op == OP_VARIABLE:
        return 9 if (len(rest) > 2 and rest[2] != STORAGE_FUNCTION) else 11
    if op in (OP_FUNCTION, OP_FUNCTION_PARAMETER, OP_FUNCTION_END, OP_LABEL):
        return 11
    if op in BODY_OPS:
        return 11
    return 11 if 100 <= op <= 400 else 0


def _vec3_i32(ty_of, tid):
    """true when tid is a 3-component vector of 32-bit integers.

    The Vulkan builtin VUIDs (e.g. VUID-LocalInvocationId-04283,
    VUID-WorkgroupId-04424, VUID-WorkgroupSize-04427) require exactly
    that, without constraining signedness."""
    tv = ty_of.get(tid)
    if not tv or tv[0] != OP_TYPE_VECTOR or len(tv[1]) < 3:
        return False
    comp = ty_of.get(tv[1][1])
    return bool(comp and comp[0] == OP_TYPE_INT and comp[1][1] == 32) and \
        tv[1][2] == 3


def _vec3_u32(ty_of, tid):
    """true when tid is a 3-component vector of UNSIGNED 32-bit integers.

    glslang emits uvec3 for every compute builtin and drivers rely on it:
    a signed WorkgroupSize constant validates but makes lavapipe fail
    pipeline creation with VK_ERROR_UNKNOWN."""
    tv = ty_of.get(tid)
    if not tv or tv[0] != OP_TYPE_VECTOR or len(tv[1]) < 3:
        return False
    comp = ty_of.get(tv[1][1])
    return bool(comp and comp[0] == OP_TYPE_INT and comp[1][1] == 32
                and comp[1][2] == 0) and tv[1][2] == 3


def _check_layout_order(seq, problems, path):
    cur = 0
    for section, word, op in seq:
        if section == 0:
            continue
        if section < cur:
            problems.append(
                f"{path}: word {word}: op {op} (section {section}) appears "
                f"after section {cur} — SPIR-V logical layout violated")
        cur = max(cur, section)


def _check_caps(caps, ty_of, problems, path):
    need = {}
    for tid, (op, rest) in ty_of.items():
        if op == OP_TYPE_INT:
            width, want = rest[1], {8: CAP_INT8, 16: CAP_INT16,
                                    64: CAP_INT64}.get(rest[1])
            if want and want not in caps:
                need[want] = f"Int{width}"
        if op == OP_TYPE_FLOAT and rest[1] == 64 and CAP_FLOAT64 not in caps:
            need[CAP_FLOAT64] = "Float64"
    for cap, name in sorted(need.items()):
        problems.append(f"{path}: uses {name} but declares no Capability {name}")


def _check_builtins(decors, ty_of, var_ty, problems, path):
    for target, dec, lits in decors:
        if dec != DECORATION_BUILTIN or not lits:
            continue
        builtin = lits[0]
        if builtin in (BUILTIN_WORKGROUP_ID, BUILTIN_LOCAL_INVOCATION_ID,
                       BUILTIN_NUM_WORKGROUPS):
            pty = ty_of.get(var_ty.get(target))
            if not pty or pty[0] != OP_TYPE_POINTER:
                problems.append(f"{path}: builtin {builtin} target {target} "
                                f"is not an Input variable")
            elif pty[1][1] != STORAGE_INPUT or not _vec3_i32(ty_of, pty[1][2]):
                problems.append(f"{path}: builtin {builtin} must be an Input "
                                f"variable of a 3-component 32-bit int vector")
        elif builtin == BUILTIN_WORKGROUP_SIZE:
            ty = ty_of.get(target)
            if ty is None or ty[0] not in (OP_CONSTANT, OP_CONSTANT_COMPOSITE):
                problems.append(f"{path}: WorkgroupSize must decorate a "
                                f"constant, not {ty[0] if ty else 'a variable'}")
            elif not _vec3_i32(ty_of, ty[1][0]):
                problems.append(f"{path}: WorkgroupSize constant must be a "
                                f"3-component 32-bit int vector")
            elif not _vec3_u32(ty_of, ty[1][0]):
                # signed components pass spirv-val but no driver accepts
                # them: lavapipe fails vkCreateComputePipelines outright
                problems.append(f"{path}: WorkgroupSize constant must be an "
                                f"UNSIGNED 3-component vector (uvec3)")


def check_layout(words, problems, path="<module>"):
    """append SPIR-V layout/capability violations found in `words`"""
    caps = set()
    ty_of = {}
    var_storage = {}
    var_ty = {}
    decors = []
    entry_pts = []
    func_ret = {}
    seq = []

    in_func = False
    body_started = False
    i = 5
    while i < len(words):
        wc = words[i] >> 16
        op = words[i] & 0xFFFF
        if wc == 0 or i + wc > len(words):
            break
        rest = list(words[i + 1:i + wc])

        seq.append((_section(op, rest), i, op))

        if op == OP_CAPABILITY and rest:
            caps.add(rest[0])
        elif op in TYPE_OPS:
            ty_of[rest[0]] = (op, rest)
        elif op in CONST_OPS and len(rest) >= 2:
            ty_of[rest[1]] = (op, rest)
        elif op == OP_VARIABLE and len(rest) >= 3:
            var_storage[rest[1]] = rest[2]
            var_ty[rest[1]] = rest[0]
        elif op == OP_DECORATE and len(rest) >= 2:
            decors.append((rest[0], rest[1], rest[2:]))
        elif op == OP_ENTRY_POINT and len(rest) >= 3:
            raw = b"".join(bytes([(x >> (8 * b)) & 0xFF for b in range(4)])
                           for x in rest[2:])
            nz = raw.find(b"\x00")
            nlen = nz if nz >= 0 else len(raw)
            nw = (nlen + 1 + 3) // 4
            entry_pts.append((rest[1], rest[2 + nw:]))
        elif op == OP_FUNCTION and len(rest) >= 2:
            func_ret[rest[1]] = rest[0]
            in_func, body_started = True, False
        elif op == OP_FUNCTION_END:
            in_func = False
        elif op == OP_VARIABLE and in_func:
            if rest[2] == STORAGE_FUNCTION and body_started:
                problems.append(
                    f"{path}: word {i}: Function OpVariable must precede all "
                    f"other instructions in the function's first block")
        elif op in (OP_LABEL, OP_FUNCTION_PARAMETER):
            pass
        elif in_func:
            body_started = True

        i += wc

    _check_layout_order(seq, problems, path)
    _check_caps(caps, ty_of, problems, path)
    _check_builtins(decors, ty_of, var_ty, problems, path)
    check_workgroup_interface(entry_pts, var_storage, problems, path)
    check_entry_points(entry_pts, func_ret, ty_of, problems, path)
    return problems
