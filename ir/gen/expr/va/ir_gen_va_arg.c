/* ir_gen_va_arg.c -- __builtin_va_arg(ap, T) inline lowering.
 *
 * The x86-64 SysV va_list is a 24-byte struct: gp_offset(i32) @0,
 * fp_offset(i32) @4, overflow_arg_area(ptr) @8, reg_save_area(ptr)
 * @16.  va_arg(ap, T) reads the next argument:
 *
 *   off = *(u32*)(&ap + field)            (gp_offset or fp_offset)
 *   if (off < threshold)                  register-save path
 *       addr = reg_save_area + off;  *field = off + reg_adv
 *   else                                  overflow path
 *       addr = overflow_arg_area;   overflow += 8
 *   result = load T addr  (phi-merged across the two paths)
 *
 * The constants match gcc's own -O0 lowering (verified against
 * `gcc -std=c11 -O0 -S`): GP < 48 advance 8, FP < 176 advance 16
 * (8 XMM slots of 16 bytes), overflow always advances 8.
 */

#include "../../ir_gen.h"
#include "../ir_gen_expr.h"

#include <stdio.h>

/* va_list struct field indices (order fixed by include/stdarg.h) */
enum { VA_GP = 0, VA_FP = 1, VA_OVERFLOW = 2, VA_REGSAVE = 3 };

/* classify the requested type for the SysV va_arg algorithm.  Returns
 * 1 (GP) or 2 (FP) for supported scalars, 0 for unsupported aggregates.
 * Fills the offset field index, the register threshold and the register
 * path's offset advancement. */
static int
va_classify(IR_Type* T, int* field, int* threshold, int* reg_adv)
{
    if (T->kind >= IR_I1 && T->kind <= IR_I64) {
        if (ir_type_size(T) > 8) return 0;
        *field = VA_GP; *threshold = 48; *reg_adv = 8;
        return 1;
    }
    if (T->kind == IR_PTR) {
        *field = VA_GP; *threshold = 48; *reg_adv = 8;
        return 1;
    }
    if (T->kind == IR_F32 || T->kind == IR_F64) {
        *field = VA_FP; *threshold = 176; *reg_adv = 16;
        return 2;
    }
    return 0;
}

IR_Value*
gen_va_arg_expr(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;
    IR_Type* T = ir_type_from_ast(b->arena, n->body.va_arg.type_expr);
    int field, thr, adv;

    if (!T || !va_classify(T, &field, &thr, &adv)) {
        fprintf(stderr, "cmpl: error: __builtin_va_arg at line %d col %d: "
                "unsupported type\n", n->loc.line, n->loc.col);
        ctx->mod->had_error = 1;
        return NULL;
    }

    /* va_list is an array type (C99 7.15): the operand's value after
     * lvalue conversion is the DECAYED pointer to the first element —
     * a local array decays (GEP 0,0), a va_list parameter (itself a
     * pointer) loads its value.  gen_expr produces exactly that; the
     * raw storage address (gen_store_ptr) would be the array slot or
     * the pointer variable's slot, not the va_list the ABI reads. */
    IR_Value* ap = gen_expr(ctx, n->body.va_arg.ap);

    if (!ap) return NULL;

    IR_Value* off_gep = ir_build_gep(b, ap,
        ir_const_int(b, t_i32, 0), ir_const_int(b, t_i32, field));
    IR_Value* off = ir_build_load(b, off_gep);
    IR_Value* cond = ir_build_icmp(b, IR_COND_ULT, off,
                                   ir_const_int(b, t_u32, thr));

    /* evaluate both paths in separate blocks, phi-merge the result */
    IR_Block* reg_blk = ir_builder_new_block(b, "va.reg");
    IR_Block* ovf_blk = ir_builder_new_block(b, "va.ovf");
    IR_Block* merge_blk = ir_builder_new_block(b, "va.merge");
    if (b->cur_func->last_block) b->cur_func->last_block->next = reg_blk;
    else b->cur_func->blocks = reg_blk;
    reg_blk->next = ovf_blk;
    ovf_blk->next = merge_blk;
    b->cur_func->last_block = merge_blk;

    ir_build_cond_br(b, cond, reg_blk, ovf_blk);

    /* register path: the argument sits in reg_save_area at `off` */
    ir_builder_set_block(b, reg_blk);
    IR_Value* rsa = ir_build_load(b, ir_build_gep(b, ap,
        ir_const_int(b, t_i32, 0), ir_const_int(b, t_i32, VA_REGSAVE)));
    IR_Value* byte_addr = ir_build_gep(b, rsa,
        ir_build_zext(b, off, t_i64), NULL);
    IR_Value* reg_val = ir_build_load(b,
        ir_build_bitcast(b, byte_addr, ir_ptr_type(b->arena, T, 0)));
    ir_build_store(b, ir_build_add(b, off, ir_const_int(b, t_u32, adv)),
                   off_gep);
    IR_Block* reg_end = b->cur_block;
    ir_build_br(b, merge_blk);

    /* overflow path: the argument sits at overflow_arg_area */
    ir_builder_set_block(b, ovf_blk);
    IR_Value* ofa = ir_build_load(b, ir_build_gep(b, ap,
        ir_const_int(b, t_i32, 0), ir_const_int(b, t_i32, VA_OVERFLOW)));
    IR_Value* ovf_val = ir_build_load(b,
        ir_build_bitcast(b, ofa, ir_ptr_type(b->arena, T, 0)));
    IR_Value* new_ofa = ir_build_gep(b, ofa,
        ir_const_int(b, t_i64, 8), NULL);
    ir_build_store(b, new_ofa, ir_build_gep(b, ap,
        ir_const_int(b, t_i32, 0), ir_const_int(b, t_i32, VA_OVERFLOW)));
    IR_Block* ovf_end = b->cur_block;
    ir_build_br(b, merge_blk);

    ir_builder_set_block(b, merge_blk);
    return build_phi2(b, T, reg_val, reg_end, ovf_val, ovf_end);
}
