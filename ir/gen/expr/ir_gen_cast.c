/* ir_gen_cast.c -- scalar coercion + explicit cast lowering. */

#include "../ir_gen.h"
#include "ir_gen_expr.h"

#include <stdlib.h>

/* coerce any scalar/pointer value to 'target' (mirrors AST_CAST).
 * signedness-aware: int widening uses sext for signed (zext for i1/
 * unsigned), int↔float uses sitofp/fptosi only for signed operands. */
IR_Value*
coerce_to(IR_Builder* b, IR_Value* v, IR_Type* target)
{
    if (!v || !v->type || !target) return v;
    if (ir_type_eq(v->type, target)) return v;

    /* _Bool target: normalize to i1 (nonzero → 1), not a trunc */
    if (target->kind == IR_I1)
        return coerce_to_i1(b, v);

    /* int ↔ ptr: bitcast */
    if (v->type->kind == IR_PTR || target->kind == IR_PTR)
        return ir_build_bitcast(b, v, target);
    /* float ↔ float: bitcast (dumper emits fpext/fptrunc) */
    if ((v->type->kind == IR_F32 || v->type->kind == IR_F64) &&
        (target->kind == IR_F32 || target->kind == IR_F64))
        return ir_build_bitcast(b, v, target);
    /* int → float */
    if (v->type->kind >= IR_I1 && v->type->kind <= IR_I64 &&
        (target->kind == IR_F32 || target->kind == IR_F64))
        return widen_zext(v->type)
            ? ir_build_uitofp(b, v, target)
            : ir_build_sitofp(b, v, target);
    /* float → int */
    if ((v->type->kind == IR_F32 || v->type->kind == IR_F64) &&
        target->kind >= IR_I1 && target->kind <= IR_I64)
        return target->is_unsigned
            ? ir_build_fptoui(b, v, target)
            : ir_build_fptosi(b, v, target);
    /* int widening / narrowing */
    if (ir_type_size(v->type) < ir_type_size(target))
        return widen_zext(v->type)
            ? ir_build_zext(b, v, target)
            : ir_build_sext(b, v, target);
    if (ir_type_size(v->type) > ir_type_size(target))
        return ir_build_trunc(b, v, target);
    return ir_build_bitcast(b, v, target);
}

IR_Value*
gen_cast(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;

    IR_Value* cv = gen_expr(ctx, n->body.cast.cast_expr);
      if (!cv) return NULL;
      IR_Type* target = ir_type_from_ast(b->arena, n->body.cast.type_expr);
      if (!target || !cv->type) return cv;
      /* already matching types — nothing to do */
      if (ir_type_eq(cv->type, target)) return cv;
      /* _Bool target: normalize to i1 (nonzero → 1), not a trunc */
      if (target->kind == IR_I1) return coerce_to_i1(b, cv);
      /* int ↔ ptr: use bitcast */
      if (cv->type->kind == IR_PTR || target->kind == IR_PTR)
          return ir_build_bitcast(b, cv, target);
      /* float ↔ float: use bitcast (dumper emits fpext/fptrunc) */
      if ((cv->type->kind == IR_F32 || cv->type->kind == IR_F64) &&
          (target->kind == IR_F32 || target->kind == IR_F64))
          return ir_build_bitcast(b, cv, target);
      /* int → float: sitofp/uitofp (zext/trunc are invalid across int/float) */
      if (cv->type->kind >= IR_I1 && cv->type->kind <= IR_I64 &&
          (target->kind == IR_F32 || target->kind == IR_F64))
          return widen_zext(cv->type)
              ? ir_build_uitofp(b, cv, target)
              : ir_build_sitofp(b, cv, target);
      /* float → int: fptosi/fptoui (truncates toward zero, as C requires) */
      if ((cv->type->kind == IR_F32 || cv->type->kind == IR_F64) &&
          target->kind >= IR_I1 && target->kind <= IR_I64)
          return target->is_unsigned
              ? ir_build_fptoui(b, cv, target)
              : ir_build_fptosi(b, cv, target);
      /* int widening: zext (unsigned/i1) or sext (signed) */
      if (ir_type_size(cv->type) < ir_type_size(target))
          return widen_zext(cv->type)
              ? ir_build_zext(b, cv, target)
              : ir_build_sext(b, cv, target);
      /* int narrowing: trunc */
      if (ir_type_size(cv->type) > ir_type_size(target))
          return ir_build_trunc(b, cv, target);
      /* same-size int conversion, or struct→struct: bitcast */
            return ir_build_bitcast(b, cv, target);
}
