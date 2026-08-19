/* ir_gen_stmt.c -- AST-to-IR statement dispatch + leaf statements.
 *
 * gen_stmt dispatches on the statement node type; the if/switch/loop
 * control-flow generators live in ir_gen_stmt_ctrl.c.  ir_is_terminator
 * and link_blocks are shared with that file (declared in ir_gen.h).
 */

#include "ir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
        IR_Value* undef = arena_alloc(b->arena, sizeof(IR_Value));
        undef->kind = VAL_UNDEF; undef->type = ctx->ret_type; rv = undef;
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
 *  Static local: module-level global with a function-mangled name.
 *  C11 6.2.4p3: the object persists across calls, so it must live in
 *  static storage (mod->globals), not in a per-call alloca.  The
 *  initializer is constant by definition (C11 6.7.9p4); a failed
 *  const fold falls back to zero-init.
 * --------------------------------------------------------------- */

static IR_Value* gen_static_local(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;
    IR_Module*  mod = ctx->mod;
    IR_Type*    vt = ir_type_from_ast(b->arena, n->body.var_decl.var_type);

    if (!vt || vt->kind == IR_VOID) vt = t_i8;

    /* unique global name: "<func>.<var>.<line>" — source identifiers
     * cannot contain '.', so this cannot collide with user globals */
    int   flen = b->cur_func && b->cur_func->name.data
               ? (int)b->cur_func->name.length : 0;
    int   vlen = (int)n->body.var_decl.name.length;
    char* nm = arena_alloc(b->arena, flen + vlen + 24);

    if (b->cur_func && b->cur_func->name.data) {
        memcpy(nm, b->cur_func->name.data, flen);
        nm[flen] = '.';
    } else {
        nm[0] = '.';
        flen = 1;
    }
    memcpy(nm + flen + 1, n->body.var_decl.name.data, vlen);
    sprintf(nm + flen + 1 + vlen, ".%d", n->loc.line);

    IR_Value* gv = arena_alloc(b->arena, sizeof(IR_Value));
    gv->kind = VAL_GLOBAL;
    gv->name.data = nm;
    gv->name.length = (int)strlen(nm);
    gv->type = vt;
    gv->linkage = IR_LINK_INTERNAL;   /* static local: internal */

    if (n->body.var_decl.init) {
        gv->body.init_val = gen_const_init(b->arena,
                                           n->body.var_decl.init, vt,
                                           (TypedefEntry*)mod->enum_vals,
                                           (HashMap*)mod->global_types, NULL);
    }
    if (!gv->body.init_val) {
        IR_Value* init = arena_alloc(b->arena, sizeof(IR_Value));
        init->kind = (vt->kind == IR_PTR) ? VAL_CONST_NULL : VAL_CONST_INT;
        init->type = vt;
        gv->body.init_val = init;
    }

    gv->next = mod->globals;
    mod->globals = gv;
    sym_add(ctx, n->body.var_decl.name, gv);
    return gv;
}

/* ---------------------------------------------------------------
 *  Variable declaration: alloca + type-aware initialiser
 * --------------------------------------------------------------- */

static void gen_stmt_var_decl(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;

    if (n->body.var_decl.linkage == LINK_STATIC) {   /* static local */
        gen_static_local(ctx, n);
        return;
    }

    IR_Type* vt = ir_type_from_ast(b->arena, n->body.var_decl.var_type);

    if (!vt || vt->kind == IR_VOID) vt = t_i8;
    IR_Value* al = ir_build_alloca(b, vt);
    sym_add(ctx, n->body.var_decl.name, al);

    if (n->body.var_decl.init &&
        n->body.var_decl.init->type == AST_INIT_LIST) {
        /* array/struct initializer: recursive, type-aware stores
         * (nested braces, designators, C99 cursor).  The old per-element
         * gen_expr loop dropped inner brace lists (no AST_INIT_LIST case
         * in gen_expr → undef) and emitted wrong GEP chains.  Zero the
         * uncovered slots first (C99 6.7.8p21: unlisted members/elements
         * are zero-initialized). */
        if (vt->kind == IR_ARRAY || vt->kind == IR_STRUCT ||
            vt->kind == IR_UNION)
            ir_gen_zero_fill(ctx, al, vt);
        ir_gen_init_one(ctx, al, n->body.var_decl.init, vt, NULL);
    } else if (n->body.var_decl.init) {
        AST_Node* initn = n->body.var_decl.init;
        if (initn->type == AST_STRING_LIT && vt &&
            vt->kind == IR_ARRAY && vt->size > 0) {
            /* char a[N] = "s": copy bytes, not the pointer */
            gen_string_array_init(ctx, al, initn, vt);
        } else {
            IR_Value* init = gen_expr(ctx, initn);
            if (init && vt == t_i32 &&
                n->body.var_decl.var_type &&
                n->body.var_decl.var_type->kind == TYPE_NAMED &&
                !n->body.var_decl.var_type->inner &&
                init->type && init->type->kind == IR_PTR)
                vt = ir_ptr_type(b->arena, t_i8, 0);
            /* Coerce the init to the variable's type before storing:
               `int* p = 0;` must store a full-width null pointer, not
               a 4-byte i32 0 into an 8-byte `alloca ptr` (the load then
               read 4 garbage bytes -> `p == 0` was false).  Only scalar
               coercions are allowed here: an aggregate init whose type is
               an anonymous struct/union CLONE fails ir_type_eq (clones
               compare by pointer identity) and coerce_to would emit a
               self-bitcast on a value, which LLVM rejects.  An aggregate
               store is already valid as-is (value type drives the store). */
            if (init && init->type && vt &&
                !ir_type_eq(init->type, vt) &&
                !(init->type->kind == IR_STRUCT ||
                  init->type->kind == IR_UNION ||
                  init->type->kind == IR_ARRAY) &&
                !(vt->kind == IR_STRUCT ||
                  vt->kind == IR_UNION ||
                  vt->kind == IR_ARRAY))
                init = coerce_to(b, init, vt);
            if (init) ir_build_store(b, init, al);
        }
    }
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
