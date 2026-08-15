/* ir_builder_cast.c -- IR builder: cast/bitcast instruction builders */

#include "ir.h"

#include "ir_builder.h"

#define CAST_BUILDER(FN, OP)                                  \
IR_Value* FN(IR_Builder* b, IR_Value* v, IR_Type* to) {       \
    IR_Instr* inst = make_instr(b, OP, to);                    \
    inst->operands[0] = v; append_instr(b, inst);              \
    return inst->result;                                       \
}

CAST_BUILDER(ir_build_bitcast, IROP_BITCAST)
CAST_BUILDER(ir_build_trunc,   IROP_TRUNC)
CAST_BUILDER(ir_build_zext,    IROP_ZEXT)
CAST_BUILDER(ir_build_sext,    IROP_SEXT)
CAST_BUILDER(ir_build_sitofp,  IROP_SITOFP)
CAST_BUILDER(ir_build_uitofp,  IROP_UITOFP)
CAST_BUILDER(ir_build_fptosi,  IROP_FPTOSI)
CAST_BUILDER(ir_build_fptoui,  IROP_FPTOUI)

#undef CAST_BUILDER
