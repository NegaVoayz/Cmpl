/* ir_dump.c -- LLVM IR text dumper (.ll syntax) */

#include "ir.h"

#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------
 *  Type printer
 * --------------------------------------------------------------- */

static void
dump_type(FILE* out, IR_Type* ty)
{
    if (!ty) { fprintf(out, "void"); return; }

    switch (ty->kind) {
    case IR_VOID:  fprintf(out, "void"); break;
    case IR_I1:    fprintf(out, "i1"); break;
    case IR_I8:    fprintf(out, "i8"); break;
    case IR_I16:   fprintf(out, "i16"); break;
    case IR_I32:   fprintf(out, "i32"); break;
    case IR_I64:   fprintf(out, "i64"); break;
    case IR_F32:   fprintf(out, "float"); break;
    case IR_F64:   fprintf(out, "double"); break;

    case IR_PTR:
        dump_type(out, ty->inner);

        if (ty->addrspace > 0)
            fprintf(out, " addrspace(%d)*", ty->addrspace);
        else
            fprintf(out, "*");
        break;

    case IR_ARRAY:
        fprintf(out, "[%d x ", ty->size);
        dump_type(out, ty->inner);
        fprintf(out, "]");
        break;

    case IR_FUNC:
        dump_type(out, ty->inner);
        fprintf(out, " (");

        for (IR_Type* p = ty->members; p; p = p->next) {
            if (p != ty->members) fprintf(out, ", ");
            dump_type(out, p);
        }
        fprintf(out, ")");
        break;

    case IR_STRUCT:
        if (ty->name.data)
            fprintf(out, "%%struct.%.*s", ty->name.length, ty->name.data);
        else
            fprintf(out, "{}");
        break;

    default: fprintf(out, "?"); break;
    }
}

/* ---------------------------------------------------------------
 *  Value printer
 * --------------------------------------------------------------- */

static void
dump_value(FILE* out, IR_Value* val)
{
    if (!val) { fprintf(out, "void"); return; }

    switch (val->kind) {
    case VAL_CONST_INT:
        fprintf(out, "%ld", val->body.int_val);
        break;

    case VAL_CONST_FLOAT:
        fprintf(out, "%g", val->body.float_val);
        break;

    case VAL_CONST_NULL:
        fprintf(out, "null");
        break;

    case VAL_CONST_STRING:
        fprintf(out, "c\"%.*s\"", val->body.str_val.length, val->body.str_val.data);
        break;

    case VAL_PARAM:
        dump_type(out, val->type);
        fprintf(out, " %%%d", val->id);
        break;

    case VAL_INSTR:
        fprintf(out, "%%%d", val->id);
        break;

    case VAL_GLOBAL:
        fprintf(out, "@%.*s", val->name.length, val->name.data);
        break;

    case VAL_UNDEF:
        dump_type(out, val->type);
        fprintf(out, " undef");
        break;

    default: fprintf(out, "?"); break;
    }
}

/* ---------------------------------------------------------------
 *  Condition printer
 * --------------------------------------------------------------- */

static const char*
cond_str(IR_Cond cond)
{
    switch (cond) {
    case IR_COND_EQ:  return "eq";
    case IR_COND_NE:  return "ne";
    case IR_COND_UGT: return "ugt";
    case IR_COND_UGE: return "uge";
    case IR_COND_ULT: return "ult";
    case IR_COND_ULE: return "ule";
    case IR_COND_SGT: return "sgt";
    case IR_COND_SGE: return "sge";
    case IR_COND_SLT: return "slt";
    case IR_COND_SLE: return "sle";
    default: return "?";
    }
}

/* ---------------------------------------------------------------
 *  Instruction printer
 * --------------------------------------------------------------- */

static void
dump_instr(FILE* out, IR_Instr* inst)
{
    if (!inst) return;

    /* print result vreg for instructions that produce values */
    switch (inst->opcode) {
    case IROP_STORE: case IROP_RET: case IROP_BR: case IROP_COND_BR:
    case IROP_UNREACHABLE:
        break;
    default:
        fprintf(out, "  %%%d = ", inst->result->id);
        break;
    }

    switch (inst->opcode) {
    case IROP_ALLOCA:
        fprintf(out, "alloca ");
        dump_type(out, inst->type->inner ? inst->type->inner : inst->type);
        break;

    case IROP_LOAD:
        fprintf(out, "load ");
        dump_type(out, inst->type);
        fprintf(out, ", ");
        dump_value(out, inst->operands[0]);
        break;

    case IROP_STORE:
        fprintf(out, "  store ");
        dump_value(out, inst->operands[0]);
        fprintf(out, ", ");
        dump_value(out, inst->operands[1]);
        break;

    case IROP_ADD: case IROP_SUB: case IROP_MUL:
    case IROP_SDIV: case IROP_SREM:
    {
        const char* op_names[] = {"add","sub","mul","sdiv","srem"};
        int idx = inst->opcode - IROP_ADD;
        fprintf(out, "%s ", op_names[idx]);
        dump_type(out, inst->type);
        fprintf(out, " ");
        dump_value(out, inst->operands[0]);
        fprintf(out, ", ");
        dump_value(out, inst->operands[1]);
        break;
    }

    case IROP_FADD: case IROP_FSUB: case IROP_FMUL: case IROP_FDIV:
    {
        const char* op_names[] = {"fadd","fsub","fmul","fdiv"};
        int idx = inst->opcode - IROP_FADD;
        fprintf(out, "%s ", op_names[idx]);
        dump_value(out, inst->operands[0]);
        fprintf(out, ", ");
        dump_value(out, inst->operands[1]);
        break;
    }

    case IROP_AND: case IROP_OR: case IROP_XOR:
    case IROP_SHL: case IROP_LSHR: case IROP_ASHR:
    {
        const char* op_names[] = {"and","or","xor","shl","lshr","ashr"};
        int idx = inst->opcode - IROP_AND;
        fprintf(out, "%s ", op_names[idx]);
        dump_type(out, inst->type);
        fprintf(out, " ");
        dump_value(out, inst->operands[0]);
        fprintf(out, ", ");
        dump_value(out, inst->operands[1]);
        break;
    }

    case IROP_ICMP:
        fprintf(out, "icmp %s ", cond_str(inst->cond));
        dump_type(out, inst->operands[0]->type);
        fprintf(out, " ");
        dump_value(out, inst->operands[0]);
        fprintf(out, ", ");
        dump_value(out, inst->operands[1]);
        break;

    case IROP_CALL:
        fprintf(out, "call ");
        dump_type(out, inst->type);
        fprintf(out, " @%.*s(", inst->callee.length, inst->callee.data);

        for (int i = 0; i < inst->n_call_args; i++) {
            if (i > 0) fprintf(out, ", ");
            dump_value(out, inst->call_args[i]);
        }
        fprintf(out, ")");
        break;

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

    case IROP_GEP:
        fprintf(out, "getelementptr ");
        dump_type(out, inst->operands[0]->type->inner);
        fprintf(out, ", ");
        dump_value(out, inst->operands[0]);
        fprintf(out, ", ");

        if (inst->operands[1])
            dump_value(out, inst->operands[1]);

        if (inst->operands[2])
            fprintf(out, ", %%%d", inst->operands[2]->id);
        break;

    case IROP_BITCAST:
        fprintf(out, "bitcast ");
        dump_value(out, inst->operands[0]);
        fprintf(out, " to ");
        dump_type(out, inst->type);
        break;

    case IROP_TRUNC:
        fprintf(out, "trunc ");
        dump_value(out, inst->operands[0]);
        fprintf(out, " to ");
        dump_type(out, inst->type);
        break;

    case IROP_ZEXT:
        fprintf(out, "zext ");
        dump_value(out, inst->operands[0]);
        fprintf(out, " to ");
        dump_type(out, inst->type);
        break;

    case IROP_SEXT:
        fprintf(out, "sext ");
        dump_value(out, inst->operands[0]);
        fprintf(out, " to ");
        dump_type(out, inst->type);
        break;

    case IROP_SELECT:
        fprintf(out, "select ");
        dump_value(out, inst->operands[0]);
        fprintf(out, ", ");
        dump_value(out, inst->operands[1]);
        fprintf(out, ", ");
        dump_value(out, inst->operands[2]);
        break;

    case IROP_UNREACHABLE:
        fprintf(out, "  unreachable");
        break;

    default:
        fprintf(out, "  ; unknown opcode %d", inst->opcode);
        break;
    }
    fprintf(out, "\n");
}

/* ---------------------------------------------------------------
 *  Block printer
 * --------------------------------------------------------------- */

static void
dump_block(FILE* out, IR_Block* blk)
{
    fprintf(out, "%.*s:\n", blk->name.length, blk->name.data);

    for (IR_Instr* inst = blk->first; inst; inst = inst->next)
        dump_instr(out, inst);
}

/* ---------------------------------------------------------------
 *  Function printer
 * --------------------------------------------------------------- */

static void
dump_func(FILE* out, IR_Func* func)
{
    /* linkage / define */
    switch (func->linkage) {
    case LINK_INTERNAL: fprintf(out, "internal "); break;
    case LINK_DEVICE:   fprintf(out, "spir_func "); break;
    case LINK_KERNEL:   fprintf(out, "spir_kernel "); break;
    default: break;
    }

    if (func->blocks)
        fprintf(out, "define ");
    else
        fprintf(out, "declare ");

    /* return type */
    dump_type(out, func->ret_type);
    fprintf(out, " @%.*s(", func->name.length, func->name.data);

    /* params */
    for (int i = 0; i < func->n_params; i++) {
        if (i > 0) fprintf(out, ", ");
        dump_value(out, func->params[i]);
    }
    fprintf(out, ")");

    if (!func->blocks) {
        fprintf(out, "\n");
        return;
    }

    fprintf(out, " {\n");

    for (IR_Block* blk = func->blocks; blk; blk = blk->next)
        dump_block(out, blk);

    fprintf(out, "}\n");
}

/* ---------------------------------------------------------------
 *  Module printer
 * --------------------------------------------------------------- */

void
ir_dump_module(IR_Module* mod, FILE* out)
{
    if (!mod || !out) return;

    /* target triple + data layout */
    if (mod->target_triple)
        fprintf(out, "target triple = \"%s\"\n\n", mod->target_triple);

    if (mod->data_layout)
        fprintf(out, "target datalayout = \"%s\"\n\n", mod->data_layout);

    /* globals + declarations */
    for (IR_Func* func = mod->funcs; func; func = func->next) {
        dump_func(out, func);
        if (func->next) fprintf(out, "\n");
    }
}

/* ---------------------------------------------------------------
 *  Single function dumper (convenience)
 * --------------------------------------------------------------- */

void
ir_dump_func(IR_Func* func, FILE* out)
{
    dump_func(out, func);
}
