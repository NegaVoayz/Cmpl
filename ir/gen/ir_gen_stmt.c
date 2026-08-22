/* ir_gen_stmt.c -- AST-to-IR statement dispatch + leaf statements.
 *
 * gen_stmt dispatches on the statement node type; the if/switch/loop
 * control-flow generators live in ir_gen_stmt_ctrl.c.  Variable
 * declarations (static locals, block locals) live in
 * ir_gen_stmt_decl.c.  ir_is_terminator and link_blocks are shared
 * across those files (declared in ir_gen.h).
 */

#include "ir.h"

#include "ast.h"
#include "arena.h"
#include "ir_gen.h"

/* check if an opcode is a terminator (nothing can follow it in a block) */
int ir_is_terminator(IR_Opcode op)
{
    return op == IROP_RET || op == IROP_BR ||
           op == IROP_COND_BR || op == IROP_UNREACHABLE;
}

/* helper: link new blocks after existing ones */
void link_blocks(IR_Func* f, IR_Block* b)
{
    if (f->last_block)
        f->last_block->next = b;
    else
        f->blocks = b;
    f->last_block = b;
}

/* ---------------------------------------------------------------
 *  Return statement: coerce value to the function return type
 * --------------------------------------------------------------- */

static void gen_stmt_return(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;
    IR_Value* rv = gen_expr(ctx, n->body.ret.expr);

    if (!rv && ctx->ret_type && ctx->ret_type->kind != IR_VOID) {
        rv = gen_undef(b, ctx->ret_type);
    }

    /* coerce return value to function return type */
    if (rv && rv->type && ctx->ret_type && ctx->ret_type->kind != IR_VOID) {
        int rk = rv->type->kind;
        int fk = ctx->ret_type->kind;

        if (rk != fk || rk == IR_PTR) {
            /* undef: just fix type */
            if (rv->kind == VAL_UNDEF) {
                rv->type = ctx->ret_type;
            }
            /* int 0 → ptr null */
            else if (fk == IR_PTR && rk != IR_PTR) {
                if (rv->kind == VAL_CONST_INT && rv->body.int_val == 0)
                    rv = ir_const_null(b->arena, ctx->ret_type);
                else
                    rv = ir_build_bitcast(b, rv, ctx->ret_type);
            }
            /* any scalar/pointer → _Bool return: nonzero → 1 */
            else if (fk == IR_I1 && rk != IR_I1)
                rv = coerce_to_i1(b, rv);
            /* int → float: sitofp */
            else if (rk >= IR_I1 && rk <= IR_I64 &&
                     (fk == IR_F32 || fk == IR_F64))
                rv = ir_build_sitofp(b, rv, ctx->ret_type);
            /* float → int: fptosi */
            else if ((rk == IR_F32 || rk == IR_F64) &&
                     fk >= IR_I1 && fk <= IR_I64)
                rv = ir_build_fptosi(b, rv, ctx->ret_type);
            /* i1 → wider int */
            else if (rk == IR_I1 && fk != IR_I1)
                rv = ir_build_zext(b, rv, ctx->ret_type);
            /* ptr → int or int→int: try trunc/zext/bitcast */
            else if (rk != fk)
                rv = ir_build_bitcast(b, rv, ctx->ret_type);
        }
    }

    ir_build_ret(b, rv);
}

/* ---------------------------------------------------------------
 *  Statement generation
 * --------------------------------------------------------------- */

void gen_stmt(GenCtx* ctx, AST_Node* n)
{
    if (!n) return;

    IR_Builder* b = ctx->b;

    switch (n->type) {
    case AST_BLOCK:
        sym_scope_push(ctx);
        for (AST_Node* s = n->body.block.stmts; s; s = s->next) gen_stmt(ctx, s);
        sym_scope_pop(ctx);
        break;
    case AST_EXPR_STMT: gen_expr(ctx, n->body.expr_stmt.expr); break;
    case AST_RETURN: gen_stmt_return(ctx, n); break;
    case AST_IF: gen_stmt_if(ctx, n); break;
    case AST_WHILE: gen_stmt_while(ctx, n); break;
    case AST_DO_WHILE: gen_stmt_do_while(ctx, n); break;
    case AST_FOR: gen_stmt_for(ctx, n); break;
    case AST_BREAK: if (ctx->break_blk) ir_build_br(b, ctx->break_blk); break;
    case AST_CONTINUE: if (ctx->cont_blk) ir_build_br(b, ctx->cont_blk); break;
    case AST_VAR_DECL: gen_stmt_var_decl(ctx, n); break;
    case AST_SWITCH: gen_stmt_switch(ctx, n); break;
    case AST_CASE: case AST_DEFAULT:
        for (AST_Node* s = n->body.case_stmt.stmt; s; s = s->next)
            gen_stmt(ctx, s);
        break;
    case AST_GOTO: gen_stmt_goto(ctx, n); break;
    case AST_LABEL: gen_stmt_label(ctx, n); break;
    case AST_STATIC_ASSERT:
        /* already evaluated module-wide (ir_check_static_asserts);
         * a true assert emits no code */
        break;
    default: break;
    }
}
