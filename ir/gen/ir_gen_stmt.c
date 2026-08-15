/* ir_gen_stmt.c -- AST-to-IR statement generation */

#include "ir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "arena.h"
#include "ir_gen.h"

/* forward: defined below (used by gen_stmt_if/while/for) */
static void gen_stmt_switch(GenCtx* ctx, AST_Node* n);

/* check if an opcode is a terminator (nothing can follow it in a block) */
static int is_terminator(IR_Opcode op)
{
    return op == IROP_RET || op == IROP_BR ||
           op == IROP_COND_BR || op == IROP_UNREACHABLE;
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

static void gen_stmt_switch(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;
    IR_Value* cond = gen_expr(ctx, n->body.switch_stmt.condition);

    AST_Node* cases = n->body.switch_stmt.body ?
        n->body.switch_stmt.body->body.block.stmts : NULL;
    AST_Node* default_node = NULL;

    /* collect case nodes and default */
    int n_cases = 0;
    for (AST_Node* c = cases; c; c = c->next) {
        if (c->type == AST_CASE) n_cases++;
        else if (c->type == AST_DEFAULT) default_node = c;
    }

    /* create one test block + body block per case (dispatch order),
     * then the default block and merge block last. */
    IR_Block* test_blks[64];
    IR_Block* body_blks[64];
    AST_Node* case_nodes[64];
    int ci = 0;
    for (AST_Node* c = cases; c && ci < 64; c = c->next) {
        if (c->type != AST_CASE) continue;
        case_nodes[ci] = c;
        test_blks[ci] = ir_builder_new_block(b, "case.test");
        body_blks[ci] = ir_builder_new_block(b, "case");
        link_blocks(b->cur_func, test_blks[ci]);
        link_blocks(b->cur_func, body_blks[ci]);
        ci++;
    }
    IR_Block* def_blk = NULL;
    if (default_node) {
        def_blk = ir_builder_new_block(b, "default");
        link_blocks(b->cur_func, def_blk);
    }
    IR_Block* merge = ir_builder_new_block(b, "switch.end");
    link_blocks(b->cur_func, merge);
    b->cur_func->last_block = merge;

    /* branch from the current block into the first test (or default/merge) */
    if (ci > 0)
        ir_build_br(b, test_blks[0]);
    else if (def_blk)
        ir_build_br(b, def_blk);

    /* dispatch: chain of comparisons */
    for (int i = 0; i < ci; i++) {
        IR_Block* next = (i + 1 < ci) ? test_blks[i + 1] :
                         (def_blk ? def_blk : merge);
        ir_builder_set_block(b, test_blks[i]);
        IR_Value* case_val = gen_expr(ctx, case_nodes[i]->body.case_stmt.value);
        IR_Value* cmp = ir_build_icmp(b, IR_COND_EQ, cond, case_val);
        ir_build_cond_br(b, cmp, body_blks[i], next);
    }

    /* case bodies */
    IR_Block* save_brk = ctx->break_blk;
    ctx->break_blk = merge;
    for (int i = 0; i < ci; i++) {
        ir_builder_set_block(b, body_blks[i]);
        for (AST_Node* s = case_nodes[i]->body.case_stmt.stmt; s; s = s->next)
            gen_stmt(ctx, s);
        /* Fall through to the next case body (or default/merge) unless
         * the body already ended with a terminator (return/break/goto).
         * C fall-through means an empty body (e.g. `case A: case B:`)
         * must reach case B's statements, not jump to the merge block. */
        IR_Block* next = (i + 1 < ci) ? body_blks[i + 1] :
                         (def_blk ? def_blk : merge);
        if (!b->cur_block->last || !is_terminator(b->cur_block->last->opcode))
            ir_build_br(b, next);
    }

    /* default body */
    if (def_blk) {
        ir_builder_set_block(b, def_blk);
        for (AST_Node* s = default_node->body.case_stmt.stmt; s; s = s->next)
            gen_stmt(ctx, s);
        if (!b->cur_block->last || !is_terminator(b->cur_block->last->opcode))
            ir_build_br(b, merge);
    }
    /* break must target the switch's merge block in the default body too,
     * so restore the outer break target only after both case and default
     * bodies are emitted (a `default: break` would otherwise exit the
     * enclosing loop). */
    ctx->break_blk = save_brk;

    ir_builder_set_block(b, merge);
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

static void gen_stmt_do_while(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;
    IR_Block *bb = ir_builder_new_block(b, "do.body");
    IR_Block *cb = ir_builder_new_block(b, "do.cond");
    IR_Block *mb = ir_builder_new_block(b, "do.end");

    link_blocks(b->cur_func, bb); bb->next = cb; cb->next = mb;
    b->cur_func->last_block = mb;
    ir_build_br(b, bb);

    IR_Block *save_brk = ctx->break_blk, *save_cnt = ctx->cont_blk;
    ctx->break_blk = mb; ctx->cont_blk = cb;
    ir_builder_set_block(b, bb);
    gen_stmt(ctx, n->body.loop.body);
    if (!b->cur_block->last || !is_terminator(b->cur_block->last->opcode))
        ir_build_br(b, cb);
    ir_builder_set_block(b, cb);
    { IR_Value* c = coerce_to_i1(b, gen_expr(ctx, n->body.loop.condition));
      if (c) ir_build_cond_br(b, c, bb, mb); else ir_build_br(b, mb); }
    ctx->break_blk = save_brk; ctx->cont_blk = save_cnt;
    ir_builder_set_block(b, mb);
}

static void gen_stmt_for(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;
    if (n->body.for_stmt.init) {
        /* the parser stores a non-declaration init (for (x = e; ...)) as
         * a BARE expression; gen_stmt would silently drop it (no
         * expression case) and the loop would run from stale memory */
        if (n->body.for_stmt.init->type == AST_VAR_DECL)
            gen_stmt(ctx, n->body.for_stmt.init);
        else
            gen_expr(ctx, n->body.for_stmt.init);
    }

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
        sym_scope_push(ctx);
        for (AST_Node* s = n->body.block.stmts; s; s = s->next) gen_stmt(ctx, s);
        sym_scope_pop(ctx);
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
      ir_build_ret(b, rv); break; }
    case AST_IF: gen_stmt_if(ctx, n); break;
    case AST_WHILE: gen_stmt_while(ctx, n); break;
    case AST_DO_WHILE: gen_stmt_do_while(ctx, n); break;
    case AST_FOR: gen_stmt_for(ctx, n); break;
    case AST_BREAK: if (ctx->break_blk) ir_build_br(b, ctx->break_blk); break;
    case AST_CONTINUE: if (ctx->cont_blk) ir_build_br(b, ctx->cont_blk); break;
    case AST_VAR_DECL:
    { IR_Type* vt = ir_type_from_ast(b->arena, n->body.var_decl.var_type);
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
          ir_gen_init_one(ctx, al, n->body.var_decl.init, vt);
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
              if (init) ir_build_store(b, init, al);
          }
      }
      break; }
    case AST_SWITCH: gen_stmt_switch(ctx, n); break;
    case AST_CASE: case AST_DEFAULT:
        for (AST_Node* s = n->body.case_stmt.stmt; s; s = s->next)
            gen_stmt(ctx, s);
        break;
    default: break;
    }
}
