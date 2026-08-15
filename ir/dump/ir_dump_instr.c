/* ir_dump_instr.c -- LLVM IR instruction printer */

/* TODO(refactor): file still >200 lines — the GEP and BITCAST printers are
 * the bulk; split them into ir_dump_gep.c / ir_dump_cast.c in P2. */

#include "ir.h"

#include <stdio.h>

/* from ir_dump.c */
extern void        dump_type(FILE* out, IR_Type* ty);
extern void        dump_value(FILE* out, IR_Value* val);
extern const char* cond_str(IR_Cond cond);
extern const char* fcmp_cond_str(IR_Cond cond);

/* ---------------------------------------------------------------
 *  Family printers (called by dump_instr below)
 * --------------------------------------------------------------- */

/* result-vreg prefix: emits "  %%%d = " for value-producing instructions
 * and nothing (or just the indent) for terminators / void calls. */
static void
print_result(FILE* out, IR_Instr* inst)
{
    switch (inst->opcode) {
    case IROP_STORE: case IROP_RET: case IROP_BR: case IROP_COND_BR:
    case IROP_UNREACHABLE:
        return;
    case IROP_CALL:
        if (inst->type && inst->type->kind == IR_VOID) {
            fprintf(out, "  ");
            return;
        }
        break;
    default:
        break;
    }
    fprintf(out, "  %%%d = ", inst->result->id);
}

static void
dump_memory_op(FILE* out, IR_Instr* inst)
{
    switch (inst->opcode) {
    case IROP_ALLOCA:
        fprintf(out, "alloca ");
        dump_type(out, inst->type->inner ? inst->type->inner : inst->type);
        break;

    case IROP_LOAD:
        fprintf(out, "load ");
        dump_type(out, inst->type);
        fprintf(out, ", ptr ");
        dump_value(out, inst->operands[0]);
        break;

    case IROP_STORE:
        fprintf(out, "  store ");
        dump_type(out, inst->operands[0]->type);
        fprintf(out, " ");
        dump_value(out, inst->operands[0]);
        fprintf(out, ", ptr ");
        dump_value(out, inst->operands[1]);
        break;

    default:
        break;
    }
}

/* generic two-operand arithmetic/bitwise printer. `names` must be indexed
 * by (opcode - base); the opcodes grouped in each dispatch case are
 * required to be contiguous starting at `base`. */
static void
dump_binop(FILE* out, IR_Instr* inst, const char** names, IR_Opcode base)
{
    int idx = inst->opcode - base;
    fprintf(out, "%s ", names[idx]);
    dump_type(out, inst->type);
    fprintf(out, " ");
    dump_value(out, inst->operands[0]);
    fprintf(out, ", ");
    dump_value(out, inst->operands[1]);
}

static void
dump_cmp(FILE* out, IR_Instr* inst, const char* mnemonic, const char* cond)
{
    fprintf(out, "%s %s ", mnemonic, cond);
    dump_type(out, inst->operands[0]->type);
    fprintf(out, " ");
    dump_value(out, inst->operands[0]);
    fprintf(out, ", ");
    dump_value(out, inst->operands[1]);
}

static void
dump_call(FILE* out, IR_Instr* inst)
{
    fprintf(out, "call ");
    dump_type(out, inst->type);
    if (inst->callee.length > 0)
        fprintf(out, " @%.*s(", inst->callee.length, inst->callee.data);
    else {
        /* indirect call through function pointer */
        fprintf(out, " ");
        dump_value(out, inst->operands[0]);
        fprintf(out, "(");
    }

    for (int i = 0; i < inst->n_call_args; i++) {
        if (i > 0) fprintf(out, ", ");
        dump_type(out, inst->call_args[i]->type);
        fprintf(out, " ");
        dump_value(out, inst->call_args[i]);
    }
    fprintf(out, ")");
}

static void
dump_term_op(FILE* out, IR_Instr* inst)
{
    switch (inst->opcode) {
    case IROP_RET:
        fprintf(out, "  ret ");

        if (inst->operands[0]) {
            dump_type(out, inst->operands[0]->type);
            fprintf(out, " ");
            dump_value(out, inst->operands[0]);
        } else {
            fprintf(out, "void");
        }
        break;

    case IROP_BR:
        fprintf(out, "  br label %%%s",
                inst->in_blocks[0]->name.data);
        break;

    case IROP_COND_BR:
        fprintf(out, "  br i1 ");
        dump_value(out, inst->operands[0]);
        fprintf(out, ", label %%%s, label %%%s",
                inst->in_blocks[0]->name.data,
                inst->in_blocks[1]->name.data);
        break;

    case IROP_UNREACHABLE:
        fprintf(out, "  unreachable");
        break;

    default:
        break;
    }
}

static void
dump_gep(FILE* out, IR_Instr* inst)
{
    /* the GEP base type is what the pointer points to.
     * for a global array, operands[0]->type is IR_ARRAY (not ptr),
     * so use the full array type as the base (2D indexing needs
     * the outer dimension). for a ptr, use ->inner (the pointee). */
    IR_Type* base = inst->operands[0] ? inst->operands[0]->type : NULL;
    IR_Type* elem = base;
    if (base && base->kind == IR_PTR)
        elem = base->inner;

    if (!elem || elem->kind == IR_VOID) elem = t_i8;

    fprintf(out, "getelementptr ");
    dump_type(out, elem);
    { IR_Type* base_ty = inst->operands[0] ? inst->operands[0]->type : NULL;
      if (base_ty && base_ty->kind == IR_PTR && base_ty->addrspace > 0)
          fprintf(out, ", ptr addrspace(%d) ", base_ty->addrspace);
      else
          fprintf(out, ", ptr "); }
    dump_value(out, inst->operands[0]);

    int idx0_is_zero = (inst->operands[1] &&
                        inst->operands[1]->kind == VAL_CONST_INT &&
                        inst->operands[1]->body.int_val == 0);
    int idx1_is_variable = (inst->operands[2] &&
                            inst->operands[2]->kind != VAL_CONST_INT);

    /* For struct type with pattern [0, variable]: treat as array
     * access and emit only the variable index. LLVM requires
     * struct field indices to be constant. */
    if (idx0_is_zero && idx1_is_variable &&
        elem && (elem->kind == IR_STRUCT || elem->kind == IR_UNION)) {
        fprintf(out, ", ");
        dump_type(out, inst->operands[2]->type);
        fprintf(out, " ");
        dump_value(out, inst->operands[2]);
    } else if (idx0_is_zero && inst->operands[2] &&
               (!elem || (elem->kind != IR_ARRAY && elem->kind != IR_STRUCT && elem->kind != IR_UNION))) {
        /* scalar: skip zero idx0, emit idx1 directly */
        fprintf(out, ", ");
        dump_type(out, inst->operands[2]->type);
        fprintf(out, " ");
        dump_value(out, inst->operands[2]);
    } else {
        fprintf(out, ", ");
        dump_type(out, inst->operands[1]->type);
        fprintf(out, " ");
        dump_value(out, inst->operands[1]);

        if (inst->operands[2]) {
            fprintf(out, ", ");
            dump_type(out, inst->operands[2]->type);
            fprintf(out, " ");
            dump_value(out, inst->operands[2]);
        }
    }
}

static void
dump_bitcast(FILE* out, IR_Instr* inst)
{
    IR_Type* src = inst->operands[0]->type;
    IR_Type* dst = inst->type;
    int src_ptr = src && src->kind == IR_PTR;
    int dst_ptr = dst && dst->kind == IR_PTR;
    int src_fp = src && (src->kind == IR_F32 || src->kind == IR_F64);
    int dst_fp = dst && (dst->kind == IR_F32 || dst->kind == IR_F64);

    if (src_ptr && !dst_ptr)
        fprintf(out, "ptrtoint ");
    else if (!src_ptr && dst_ptr)
        fprintf(out, "inttoptr ");
    else if (src_fp && dst_fp &&
             ir_type_size(src) < ir_type_size(dst))
        fprintf(out, "fpext ");
    else if (src_fp && dst_fp &&
             ir_type_size(src) > ir_type_size(dst))
        fprintf(out, "fptrunc ");
    else if (src && dst && !src_ptr && !dst_ptr &&
             ir_type_size(src) < ir_type_size(dst))
        fprintf(out, "zext ");
    else if (src && dst && !src_ptr && !dst_ptr &&
             ir_type_size(src) > ir_type_size(dst))
        fprintf(out, "trunc ");
    else
        fprintf(out, "bitcast ");

    dump_type(out, src);
    fprintf(out, " ");
    dump_value(out, inst->operands[0]);
    fprintf(out, " to ");
    dump_type(out, dst);
}

/* trunc/zext/sext/sitofp/uitofp/fptosi/fptoui all share this shape:
 *   <op> <src-ty> <val> to <dst-ty> */
static void
dump_cast(FILE* out, IR_Instr* inst, const char* mnemonic)
{
    fprintf(out, "%s ", mnemonic);
    dump_type(out, inst->operands[0]->type);
    fprintf(out, " ");
    dump_value(out, inst->operands[0]);
    fprintf(out, " to ");
    dump_type(out, inst->type);
}

static void
dump_select(FILE* out, IR_Instr* inst)
{
    fprintf(out, "select i1 ");
    dump_value(out, inst->operands[0]);
    fprintf(out, ", ");
    dump_type(out, inst->type);
    fprintf(out, " ");
    dump_value(out, inst->operands[1]);
    fprintf(out, ", ");
    dump_type(out, inst->type);
    fprintf(out, " ");
    dump_value(out, inst->operands[2]);
}

static void
dump_phi(FILE* out, IR_Instr* inst)
{
    fprintf(out, "phi ");
    dump_type(out, inst->type);
    fprintf(out, " ");
    for (int i = 0; i < inst->n_incoming; i++) {
        if (i > 0) fprintf(out, ", ");
        fprintf(out, "[ ");
        dump_value(out, inst->in_vals[i]);
        fprintf(out, ", %%%s ]", inst->in_blocks[i] ?
               inst->in_blocks[i]->name.data : "???");
    }
}

/* ---------------------------------------------------------------
 *  Instruction printer -- opcode dispatcher
 * --------------------------------------------------------------- */

void dump_instr(FILE* out, IR_Instr* inst)
{
    if (!inst) return;

    print_result(out, inst);

    switch (inst->opcode) {
    case IROP_ALLOCA: case IROP_LOAD: case IROP_STORE:
        dump_memory_op(out, inst);
        break;

    case IROP_ADD: case IROP_SUB: case IROP_MUL:
    case IROP_SDIV: case IROP_SREM:
    case IROP_UDIV: case IROP_UREM:
    {   static const char* names[] = {"add","sub","mul","sdiv","srem","udiv","urem"};
        dump_binop(out, inst, names, IROP_ADD);
        break; }

    case IROP_FADD: case IROP_FSUB: case IROP_FMUL: case IROP_FDIV:
    {   static const char* names[] = {"fadd","fsub","fmul","fdiv"};
        dump_binop(out, inst, names, IROP_FADD);
        break; }

    case IROP_AND: case IROP_OR: case IROP_XOR:
    case IROP_SHL: case IROP_LSHR: case IROP_ASHR:
    {   static const char* names[] = {"shl","lshr","ashr","and","or","xor"};
        dump_binop(out, inst, names, IROP_SHL);
        break; }

    case IROP_ICMP:
        dump_cmp(out, inst, "icmp", cond_str(inst->cond));
        break;

    case IROP_FCMP:
        dump_cmp(out, inst, "fcmp", fcmp_cond_str(inst->cond));
        break;

    case IROP_CALL:
        dump_call(out, inst);
        break;

    case IROP_RET: case IROP_BR: case IROP_COND_BR: case IROP_UNREACHABLE:
        dump_term_op(out, inst);
        break;

    case IROP_GEP:
        dump_gep(out, inst);
        break;

    case IROP_BITCAST:
        dump_bitcast(out, inst);
        break;

    case IROP_TRUNC:  dump_cast(out, inst, "trunc");  break;
    case IROP_ZEXT:   dump_cast(out, inst, "zext");   break;
    case IROP_SEXT:   dump_cast(out, inst, "sext");   break;
    case IROP_SITOFP: dump_cast(out, inst, "sitofp"); break;
    case IROP_UITOFP: dump_cast(out, inst, "uitofp"); break;
    case IROP_FPTOSI: dump_cast(out, inst, "fptosi"); break;
    case IROP_FPTOUI: dump_cast(out, inst, "fptoui"); break;

    case IROP_SELECT:
        dump_select(out, inst);
        break;

    case IROP_PHI:
        dump_phi(out, inst);
        break;

    default:
        fprintf(out, "  ; unknown opcode %d", inst->opcode);
        break;
    }
    fprintf(out, "\n");
}
