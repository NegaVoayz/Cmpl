/* ir_builder.c -- IR builder: lifecycle, block mgmt, basic instructions */

#include "ir.h"

#include <stdlib.h>
#include <string.h>

#include "arena.h"

/* shared with ir_builder_ops.c */
IR_Value* make_vreg(IR_Builder* b, IR_Type* ty);
IR_Instr* make_instr(IR_Builder* b, IR_Opcode op, IR_Type* ty);
void      append_instr(IR_Builder* b, IR_Instr* inst);

/* ---------------------------------------------------------------
 *  Builder lifetime
 * --------------------------------------------------------------- */

IR_Builder*
ir_builder_new(IR_Module* mod, Arena* a)
{
    IR_Builder* b = arena_alloc(a, sizeof(IR_Builder));
    b->module = mod;
    b->next_vreg_id = 0;
    b->next_label_id = 0;
    b->entry_block = NULL;
    b->arena = a;
    return b;
}

/* ---------------------------------------------------------------
 *  Block management
 * --------------------------------------------------------------- */

IR_Block*
ir_builder_new_block(IR_Builder* b, const char* name)
{
    IR_Block* blk = arena_alloc(b->arena, sizeof(IR_Block));

    if (name) {
        /* make label unique by appending a counter to avoid collisions
         * when multiple blocks share the same logical name (e.g. nested for loops) */
        char buf[64];
        int id = b->next_label_id++;
        int n = snprintf(buf, sizeof(buf), "%s.%d", name, id);
        char* copy = arena_alloc(b->arena, n + 1);
        memcpy(copy, buf, n + 1);
        blk->name.data = copy;
        blk->name.length = n;
    }
    return blk;
}

void
ir_builder_set_block(IR_Builder* b, IR_Block* block)
{
    b->cur_block = block;
}

/* ---------------------------------------------------------------
 *  Internal helpers
 * --------------------------------------------------------------- */

IR_Value*
make_vreg(IR_Builder* b, IR_Type* ty)
{
    IR_Value* v = arena_alloc(b->arena, sizeof(IR_Value));
    v->kind = VAL_INSTR;
    v->type = ty;
    v->id = b->next_vreg_id++;
    return v;
}

IR_Instr*
make_instr(IR_Builder* b, IR_Opcode op, IR_Type* ty)
{
    IR_Instr* inst = arena_alloc(b->arena, sizeof(IR_Instr));
    inst->opcode = op;
    inst->type = ty;
    inst->result = make_vreg(b, ty);
    inst->result->def_instr = inst;
    /* zero the call/phi extension fields so readers that don't check
     * opcode (e.g. dumpers scanning operands) never see garbage */
    inst->n_call_args = 0;
    inst->call_args = NULL;
    inst->n_incoming = 0;
    inst->in_vals = NULL;
    inst->in_blocks = NULL;
    return inst;
}

void
append_instr(IR_Builder* b, IR_Instr* inst)
{
    IR_Block* blk = b->cur_block;

    if (!blk->first) {
        blk->first = inst;
        blk->last = inst;
    } else {
        blk->last->next = inst;
        blk->last = inst;
    }
}

/* ---------------------------------------------------------------
 *  Value constructors
 * --------------------------------------------------------------- */

IR_Value*
ir_const_int(IR_Builder* b, IR_Type* ty, long long val)
{
    IR_Value* v = arena_alloc(b->arena, sizeof(IR_Value));
    v->kind = VAL_CONST_INT;
    v->type = ty;
    v->body.int_val = val;
    return v;
}

IR_Value*
ir_const_float(Arena* a, IR_Type* ty, double val)
{
    IR_Value* v = arena_alloc(a, sizeof(IR_Value));
    v->kind = VAL_CONST_FLOAT;
    v->type = ty;
    v->body.float_val = val;
    return v;
}

IR_Value*
ir_const_null(Arena* a, IR_Type* ty)
{
    IR_Value* v = arena_alloc(a, sizeof(IR_Value));
    v->kind = VAL_CONST_NULL;
    v->type = ty;
    return v;
}

IR_Value*
ir_const_aggregate(Arena* a, IR_Type* ty, IR_Value** elems, int count)
{
    IR_Value* v = arena_alloc(a, sizeof(IR_Value));
    v->kind = VAL_CONST_AGGREGATE;
    v->type = ty;
    v->body.aggregate.elems = elems;
    v->body.aggregate.count = count;
    return v;
}

/* ---------------------------------------------------------------
 *  Instruction builders -- memory
 * --------------------------------------------------------------- */

IR_Value*
ir_build_alloca(IR_Builder* b, IR_Type* ty)
{
    IR_Instr* inst = make_instr(b, IROP_ALLOCA, ir_ptr_type(b->arena, ty, 0));

    /* LLVM convention: allocas must live in the entry block so they
     * dominate all uses.  If we are generating code inside a branch or
     * loop body, redirect the alloca to the entry block, inserting it
     * just before the terminator (if one exists). */
    if (b->entry_block && b->cur_block != b->entry_block) {
        IR_Block* entry = b->entry_block;
        IR_Instr* term = entry->last;

        /* find the last non-terminator instruction in the entry block */
        if (term && (term->opcode == IROP_BR || term->opcode == IROP_COND_BR ||
                     term->opcode == IROP_RET || term->opcode == IROP_UNREACHABLE)) {
            IR_Instr* prev = NULL;

            for (IR_Instr* i = entry->first; i && i != term; i = i->next)
                prev = i;
            if (prev) {
                prev->next = inst;
                inst->next = term;
            } else {
                inst->next = entry->first;
                entry->first = inst;
            }
        } else {
            /* no terminator yet — just append normally */
            if (!entry->first) {
                entry->first = inst;
                entry->last = inst;
            } else {
                entry->last->next = inst;
                entry->last = inst;
            }
        }
    } else {
        append_instr(b, inst);
    }

    return inst->result;
}

IR_Value*
ir_build_load(IR_Builder* b, IR_Value* ptr)
{
    IR_Type* elem;

    if (ptr->type && ptr->type->kind == IR_PTR) {
        /* PTR source: for alloca with inner, use inner type;
           for opaque ptr (no inner, or VAL_GLOBAL), use generic ptr */
        if (ptr->kind == VAL_GLOBAL || !ptr->type->inner)
            elem = ir_ptr_type(b->arena, t_i8, 0);
        else
            elem = ptr->type->inner;
    } else if (ptr->kind == VAL_GLOBAL) {
        /* global with non-ptr type (e.g. array): globals are always
         * pointers in LLVM; treat the type as the pointee */
        elem = ptr->type;
    } else {
        elem = (ptr->type && ptr->type->inner) ? ptr->type->inner : t_i32;
    }

    /* array decay: loading from ptr-to-array gives ptr to first element */
    if (elem && elem->kind == IR_ARRAY) {
        return ir_build_gep(b, ptr,
            ir_const_int(b, t_i32, 0), ir_const_int(b, t_i32, 0));
    }

    IR_Instr* inst = make_instr(b, IROP_LOAD, elem);

    inst->operands[0] = ptr;
    append_instr(b, inst);
    return inst->result;
}

IR_Value*
ir_build_store(IR_Builder* b, IR_Value* val, IR_Value* ptr)
{
    /* Coerce the stored value to the pointee type:
     *   - larger int  -> truncate (i64 -> i32 alloca, ptr-ptr sub)
     *   - smaller int -> zext (i1/i8/i16 -> i32/i64, e.g. `x = (a==b)`)
     *   - int <-> float via sitofp/fptosi (e.g. `double x = 3;`)
     *   - float widening via bitcast (dumper emits fpext)
     * Struct/ptr narrowing uses bitcast. */
    if (val && val->type && ptr && ptr->type &&
        ptr->type->kind == IR_PTR && ptr->type->inner) {
        int val_sz = ir_type_size(val->type);
        int elem_sz = ir_type_size(ptr->type->inner);
        int val_int = (val->type->kind >= IR_I1 && val->type->kind <= IR_I64);
        int elem_int = (ptr->type->inner->kind >= IR_I1 &&
                        ptr->type->inner->kind <= IR_I64);
        int val_fp = (val->type->kind == IR_F32 || val->type->kind == IR_F64);
        int elem_fp = (ptr->type->inner->kind == IR_F32 ||
                       ptr->type->inner->kind == IR_F64);
        if (val_int && elem_int) {
            if (val_sz > elem_sz && elem_sz > 0)
                val = ir_build_trunc(b, val, ptr->type->inner);
            else if (val->type->kind != ptr->type->inner->kind)
                val = ir_build_zext(b, val, ptr->type->inner);
        } else if (val_int && elem_fp)
            val = ir_build_sitofp(b, val, ptr->type->inner);
        else if (val_fp && elem_int)
            val = ir_build_fptosi(b, val, ptr->type->inner);
        else if (val_fp && elem_fp && val_sz < elem_sz)
            val = ir_build_bitcast(b, val, ptr->type->inner);
    }

    IR_Instr* inst = make_instr(b, IROP_STORE, t_void);
    inst->operands[0] = val;
    inst->operands[1] = ptr;
    append_instr(b, inst);
    return inst->result;
}
