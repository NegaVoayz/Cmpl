/* ir_gen_stmt.c -- AST-to-IR statement generation */

#include "ir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "arena.h"

/* duplicated from ir_gen.c (C99 pattern for intra-module sharing) */
typedef struct { IR_Builder* b; HashMap syms; HashMap* sig_map; IR_Block *break_blk, *cont_blk; IR_Type* ret_type; IR_Module* mod; int is_device; } GenCtx;

/* from ir_gen.c and ir_gen_expr.c */
extern IR_Value* gen_expr(GenCtx* ctx, AST_Node* n);
extern IR_Value* sym_lookup(GenCtx* ctx, String name);
extern void      sym_add(GenCtx* ctx, String name, IR_Value* alloca);

/* forward: defined below (used by gen_stmt_if/while/for) */
void gen_stmt(GenCtx* ctx, AST_Node* n);

/* check if an opcode is a terminator (nothing can follow it in a block) */
static int is_terminator(IR_Opcode op)
{
    return op == IROP_RET || op == IROP_BR ||
           op == IROP_COND_BR || op == IROP_UNREACHABLE;
}

/* coerce a value to i1 for use as branch condition */
static IR_Value*
coerce_to_i1(IR_Builder* b, IR_Value* v)
{
    if (!v) return NULL;

    if (v->type && v->type->kind == IR_I1) return v;

    /* pointer → icmp ne ptr %v, null */
    if (v->type && v->type->kind == IR_PTR) {
        IR_Value* nv = arena_alloc(b->arena, sizeof(IR_Value));
        nv->kind = VAL_CONST_NULL; nv->type = v->type;
        return ir_build_icmp(b, IR_COND_NE, v, nv);
    }

    /* float/double → fcmp one ty %v, 0.0 */
    if (v->type && (v->type->kind == IR_F32 || v->type->kind == IR_F64)) {
        IR_Value* zero = ir_const_float(b->arena, v->type, 0.0);
        return ir_build_fcmp(b, IR_COND_NE, v, zero);
    }

    /* integer/other → icmp ne ty %v, 0 */
    IR_Value* zero = ir_const_int(b, v->type ? v->type : t_i32, 0);
    return ir_build_icmp(b, IR_COND_NE, v, zero);
}

/* helper: link new blocks after existing ones */
static void link_blocks(IR_Func* f, IR_Block* b)
{
    if (f->last_block)
        f->last_block->next = b;
    else
        f->blocks = b;
    f->last_block = b;
}

/* ---------------------------------------------------------------
 *  Statement helpers
 * --------------------------------------------------------------- */

static void gen_stmt_if(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;
    IR_Value* cond = coerce_to_i1(b, gen_expr(ctx, n->body.if_stmt.condition));
    IR_Block *tb = ir_builder_new_block(b, "then"), *mb = ir_builder_new_block(b, "merge");
    IR_Block *eb = n->body.if_stmt.else_branch ? ir_builder_new_block(b, "else") : NULL;

    link_blocks(b->cur_func, tb);
    if (eb) { tb->next = eb; eb->next = mb; }
    else tb->next = mb;
    b->cur_func->last_block = mb;

    ir_build_cond_br(b, cond, tb, eb ? eb : mb);
    ir_builder_set_block(b, tb);
    gen_stmt(ctx, n->body.if_stmt.then_branch);
    if (!b->cur_block->last || !is_terminator(b->cur_block->last->opcode)) ir_build_br(b, mb);

    if (eb) {
        ir_builder_set_block(b, eb);
        gen_stmt(ctx, n->body.if_stmt.else_branch);
        if (!b->cur_block->last || !is_terminator(b->cur_block->last->opcode)) ir_build_br(b, mb);
    }
    ir_builder_set_block(b, mb);
}

static void gen_stmt_while(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;
    IR_Block *cb = ir_builder_new_block(b, "while.cond"), *bb = ir_builder_new_block(b, "while.body");
    IR_Block *mb = ir_builder_new_block(b, "while.end");

    link_blocks(b->cur_func, cb); cb->next = bb; bb->next = mb;
    b->cur_func->last_block = mb;
    ir_build_br(b, cb);
    ir_builder_set_block(b, cb);
    ir_build_cond_br(b, coerce_to_i1(b, gen_expr(ctx, n->body.loop.condition)), bb, mb);

    IR_Block *save_brk = ctx->break_blk, *save_cnt = ctx->cont_blk;
    ctx->break_blk = mb; ctx->cont_blk = cb;
    ir_builder_set_block(b, bb);
    gen_stmt(ctx, n->body.loop.body);
    if (!b->cur_block->last || !is_terminator(b->cur_block->last->opcode)) ir_build_br(b, cb);
    ctx->break_blk = save_brk; ctx->cont_blk = save_cnt;
    ir_builder_set_block(b, mb);
}

static void gen_stmt_for(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;
    if (n->body.for_stmt.init) gen_stmt(ctx, n->body.for_stmt.init);

    IR_Block *cb = ir_builder_new_block(b, "for.cond"), *bb = ir_builder_new_block(b, "for.body");
    IR_Block *ub = ir_builder_new_block(b, "for.update"), *mb = ir_builder_new_block(b, "for.end");

    link_blocks(b->cur_func, cb); cb->next = bb; bb->next = ub; ub->next = mb;
    b->cur_func->last_block = mb;
    ir_build_br(b, cb);
    ir_builder_set_block(b, cb);
    { IR_Value* c = coerce_to_i1(b, gen_expr(ctx, n->body.for_stmt.condition));
      if (c) ir_build_cond_br(b, c, bb, mb); else ir_build_br(b, bb); }

    IR_Block *save_brk = ctx->break_blk, *save_cnt = ctx->cont_blk;
    ctx->break_blk = mb; ctx->cont_blk = ub;
    ir_builder_set_block(b, bb);
    gen_stmt(ctx, n->body.for_stmt.body);
    if (!b->cur_block->last || !is_terminator(b->cur_block->last->opcode)) ir_build_br(b, ub);
    ir_builder_set_block(b, ub);
    gen_expr(ctx, n->body.for_stmt.update);
    ir_build_br(b, cb);
    ctx->break_blk = save_brk; ctx->cont_blk = save_cnt;
    ir_builder_set_block(b, mb);
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
        for (AST_Node* s = n->body.block.stmts; s; s = s->next) gen_stmt(ctx, s);
        break;
    case AST_EXPR_STMT: gen_expr(ctx, n->body.expr_stmt.expr); break;
    case AST_RETURN:
    { IR_Value* rv = gen_expr(ctx, n->body.ret.expr);
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
              /* i1 → wider int */
              else if (rk == IR_I1 && fk != IR_I1)
                  rv = ir_build_zext(b, rv, ctx->ret_type);
              /* ptr → int or int→int: try trunc/zext/bitcast */
              else if (rk != fk)
                  rv = ir_build_bitcast(b, rv, ctx->ret_type);
          }
      }
      ir_build_ret(b, rv); break; }
    case AST_IF: gen_stmt_if(ctx, n); break;
    case AST_WHILE: gen_stmt_while(ctx, n); break;
    case AST_FOR: gen_stmt_for(ctx, n); break;
    case AST_BREAK: if (ctx->break_blk) ir_build_br(b, ctx->break_blk); break;
    case AST_CONTINUE: if (ctx->cont_blk) ir_build_br(b, ctx->cont_blk); break;
    case AST_VAR_DECL:
    { IR_Type* vt = ir_type_from_ast(b->arena, n->body.var_decl.var_type, ctx->is_device);
      if (!vt || vt->kind == IR_VOID) vt = t_i8;
      /* evaluate init before creating alloca — init type may reveal
       * that an unresolved typedef is actually a fn ptr */
      IR_Value* init = NULL;
      if (n->body.var_decl.init)
          init = gen_expr(ctx, n->body.var_decl.init);
      if (init && vt == t_i32 &&
          n->body.var_decl.var_type &&
          n->body.var_decl.var_type->kind == TYPE_NAMED &&
          !n->body.var_decl.var_type->inner &&
          init->type && init->type->kind == IR_PTR)
          vt = ir_ptr_type(b->arena, t_i8, 0);
      IR_Value* al = ir_build_alloca(b, vt);
      sym_add(ctx, n->body.var_decl.name, al);
      if (init) ir_build_store(b, init, al);
      break; }
    case AST_SWITCH: gen_stmt(ctx, n->body.switch_stmt.body); break;
    case AST_CASE: case AST_DEFAULT: gen_stmt(ctx, n->body.case_stmt.stmt); break;
    default: break;
    }
}
