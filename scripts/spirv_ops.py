#!/usr/bin/env python3
"""spirv_ops.py -- opcode tables used by spirv_check.py.

FIXED maps an opcode to its number of fixed operand words (negative =
handled specially in the walker).  ID_POS lists the operand positions that
are id references.  ALL_ID lists opcodes whose operands are all ids.
"""

# opcode -> number of fixed operand words (excluding the opcode word).
# Negative means "handled specially" in the walker.
FIXED = {
    0: 0, 1: 2, 14: 2, 15: -1, 16: -1, 17: 1, 19: 1, 20: 1, 21: 3,
    22: 2, 23: 2, 28: 3, 30: -1, 32: 3, 33: -1, 41: 1, 42: 1,
    43: -1, 44: -1, 46: 2, 54: 4, 55: 2, 56: 0, 57: -1, 59: -1, 61: -1,
    62: -1, 65: -1, 66: -1, 67: -1, 71: -1, 72: -1, 80: -1, 81: -1,
    117: 3, 120: 3, 124: 3, 126: 3, 127: 3,
    128: 4, 129: 4, 130: 4, 131: 4, 132: 4, 133: 4, 134: 4,  # arith
    135: 4, 136: 4, 137: 4, 138: 4,
    109: 3, 110: 3, 111: 3, 112: 3, 113: 3, 114: 3, 115: 3,
    169: 4, 170: 4, 171: 4, 172: 4, 173: 4, 174: 4, 175: 4,  # compare
    176: 4, 177: 4, 178: 4, 179: 4, 180: 4, 181: 4, 182: 4, 183: 4,
    184: 4, 185: 4, 186: 4, 187: 4, 188: 4, 189: 4, 190: 4, 191: 4,
    194: 4, 195: 4, 196: 4, 197: 4, 198: 4, 199: 4, 200: 4,
    245: -1,  # OpPhi
    246: 3,   # OpLoopMerge: merge, continue, loop control
    247: 2,   # OpSelectionMerge: merge, selection control
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
    1: {0, 1}, 15: {1}, 16: {0}, 19: {0}, 20: {0}, 21: {0}, 22: {0}, 23: {0, 1},
    28: {0, 1, 2}, 32: {0, 2}, 41: {0}, 42: {0}, 43: {0, 1}, 46: {0, 1},
    54: {0, 1, 3}, 55: {0, 1}, 59: {0, 1}, 61: {0, 1, 2}, 62: {0, 1},
    71: {0}, 72: {0}, 81: {0, 1, 2}, 117: {0, 1, 2}, 120: {0, 1, 2},
    124: {0, 1, 2}, 126: {0, 1, 2}, 127: {0, 1, 2},
    246: {0, 1}, 247: {0}, 248: {0}, 249: {0}, 250: {1, 2}, 254: {0},
}

# opcodes with all-id operands (result type + result id [+ operands])
ALL_ID = {30, 33, 44, 57, 65, 66, 67, 80, 245}

# conversion/arith/compare: every operand is an id
for _op in list(range(109, 116)) + list(range(128, 139)) + \
           list(range(169, 192)) + list(range(194, 201)):
    ALL_ID.add(_op)
