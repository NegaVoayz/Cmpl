/* ir_gen_stmt.c -- AST-to-IR statement generation */

#include "ir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"

/* duplicated from ir_gen.c (C99 pattern for intra-module sharing) */
typedef struct SymEntry { String name; IR_Value* alloca; struct SymEntry* next; } SymEntry;
typedef struct { IR_Builder* b; SymEntry* syms; IR_Block *break_blk, *cont_blk; int is_device; } GenCtx;

/* from ir_gen.c and ir_gen_expr.c */
extern IR_Value* gen_expr(GenCtx* ctx, AST_Node* n);
extern IR_Value* sym_lookup(GenCtx* ctx, String name);
extern void      sym_add(GenCtx* ctx, String name, IR_Value* alloca);

/* forward: defined below (used by gen_stmt_if/while/for) */
void gen_stmt(GenCtx* ctx, AST_Node* n);

/* helper: link new blocks after existing ones */
static void link_blocks(IR_Func* f, IR_Block* b)
{
    IR_Block** tail = &f->blocks;
    while (*tail) tail = &(*tail)->next;
    *tail = b;
}

/* ---------------------------------------------------------------
 *  Statement helpers
 * --------------------------------------------------------------- */

static void gen_stmt_if(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;
    IR_Value* cond = gen_expr(ctx, n->body.if_stmt.condition);
    IR_Block *tb = ir_builder_new_block(b, "then"), *mb = ir_builder_new_block(b, "merge");
    IR_Block *eb = n->body.if_stmt.else_branch ? ir_builder_new_block(b, "else") : NULL;

    link_blocks(b->cur_func, tb);
    if (eb) { tb->next = eb; eb->next = mb; }
    else tb->next = mb;

    ir_build_cond_br(b, cond, tb, eb ? eb : mb);
    ir_builder_set_block(b, tb);
    gen_stmt(ctx, n->body.if_stmt.then_branch);
    if (!b->cur_block->last || b->cur_block->last->opcode != IROP_RET) ir_build_br(b, mb);

    if (eb) {
        ir_builder_set_block(b, eb);
        gen_stmt(ctx, n->body.if_stmt.else_branch);
        if (!b->cur_block->last || b->cur_block->last->opcode != IROP_RET) ir_build_br(b, mb);
    }
    ir_builder_set_block(b, mb);
}

static void gen_stmt_while(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;
    IR_Block *cb = ir_builder_new_block(b, "while.cond"), *bb = ir_builder_new_block(b, "while.body");
    IR_Block *mb = ir_builder_new_block(b, "while.end");

    link_blocks(b->cur_func, cb); cb->next = bb; bb->next = mb;
    ir_build_br(b, cb);
    ir_builder_set_block(b, cb);
    ir_build_cond_br(b, gen_expr(ctx, n->body.loop.condition), bb, mb);

    IR_Block *save_brk = ctx->break_blk, *save_cnt = ctx->cont_blk;
    ctx->break_blk = mb; ctx->cont_blk = cb;
    ir_builder_set_block(b, bb);
    gen_stmt(ctx, n->body.loop.body);
    if (!b->cur_block->last || b->cur_block->last->opcode != IROP_RET) ir_build_br(b, cb);
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
    ir_build_br(b, cb);
    ir_builder_set_block(b, cb);
    { IR_Value* c = gen_expr(ctx, n->body.for_stmt.condition);
      if (c) ir_build_cond_br(b, c, bb, mb); else ir_build_br(b, bb); }

    IR_Block *save_brk = ctx->break_blk, *save_cnt = ctx->cont_blk;
    ctx->break_blk = mb; ctx->cont_blk = ub;
    ir_builder_set_block(b, bb);
    gen_stmt(ctx, n->body.for_stmt.body);
    if (!b->cur_block->last || b->cur_block->last->opcode != IROP_RET) ir_build_br(b, ub);
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
    case AST_RETURN: ir_build_ret(b, gen_expr(ctx, n->body.ret.expr)); break;
    case AST_IF: gen_stmt_if(ctx, n); break;
    case AST_WHILE: gen_stmt_while(ctx, n); break;
    case AST_FOR: gen_stmt_for(ctx, n); break;
    case AST_BREAK: if (ctx->break_blk) ir_build_br(b, ctx->break_blk); break;
    case AST_CONTINUE: if (ctx->cont_blk) ir_build_br(b, ctx->cont_blk); break;
    case AST_VAR_DECL:
    { IR_Type* vt = ir_type_from_ast(n->body.var_decl.var_type);
      IR_Value* al = ir_build_alloca(b, vt ? vt : t_i32);
      sym_add(ctx, n->body.var_decl.name, al);
      if (n->body.var_decl.init) {
          IR_Value* init = gen_expr(ctx, n->body.var_decl.init);
          if (init) ir_build_store(b, init, al); }
      break; }
    case AST_SWITCH: gen_stmt(ctx, n->body.switch_stmt.body); break;
    case AST_CASE: case AST_DEFAULT: gen_stmt(ctx, n->body.case_stmt.stmt); break;
    default: break;
    }
}
