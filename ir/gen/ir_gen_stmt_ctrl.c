/* ir_gen_stmt_ctrl.c -- control-flow statement generation (if/switch/loops).
 *
 * gen_stmt (ir_gen_stmt.c) dispatches to these generators.  ir_is_terminator
 * and link_blocks are shared from ir_gen_stmt.c via ir_gen.h.
 * TODO(refactor): ~3% over the 200-line target.
 */

#include "ir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "arena.h"
#include "ir_gen.h"

void gen_stmt_if(GenCtx* ctx, AST_Node* n)
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
    if (!b->cur_block->last || !ir_is_terminator(b->cur_block->last->opcode)) ir_build_br(b, mb);

    if (eb) {
        ir_builder_set_block(b, eb);
        gen_stmt(ctx, n->body.if_stmt.else_branch);
        if (!b->cur_block->last || !ir_is_terminator(b->cur_block->last->opcode)) ir_build_br(b, mb);
    }
    ir_builder_set_block(b, mb);
}

static void emit_switch_bodies(GenCtx* ctx, int ci, AST_Node** case_nodes,
                               IR_Block** body_blks, IR_Block* def_blk,
                               AST_Node* default_node, IR_Block* merge)
{
    IR_Builder* b = ctx->b;

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
        if (!b->cur_block->last || !ir_is_terminator(b->cur_block->last->opcode))
            ir_build_br(b, next);
    }

    /* default body */
    if (def_blk) {
        ir_builder_set_block(b, def_blk);
        for (AST_Node* s = default_node->body.case_stmt.stmt; s; s = s->next)
            gen_stmt(ctx, s);
        if (!b->cur_block->last || !ir_is_terminator(b->cur_block->last->opcode))
            ir_build_br(b, merge);
    }
}

void gen_stmt_switch(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;
    IR_Value* cond = gen_expr(ctx, n->body.switch_stmt.condition);

    AST_Node* cases = n->body.switch_stmt.body ?
        n->body.switch_stmt.body->body.block.stmts : NULL;
    AST_Node* default_node = NULL;

    /* find the default label (cases are counted in the create loop below) */
    for (AST_Node* c = cases; c; c = c->next) {
        if (c->type == AST_DEFAULT) default_node = c;
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

    /* case + default bodies; break targets the switch's merge block */
    IR_Block* save_brk = ctx->break_blk;
    ctx->break_blk = merge;
    emit_switch_bodies(ctx, ci, case_nodes, body_blks, def_blk, default_node, merge);
    /* break must target the switch's merge block in the default body too,
     * so restore the outer break target only after both case and default
     * bodies are emitted (a `default: break` would otherwise exit the
     * enclosing loop). */
    ctx->break_blk = save_brk;

    ir_builder_set_block(b, merge);
}

void gen_stmt_while(GenCtx* ctx, AST_Node* n)
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
    if (!b->cur_block->last || !ir_is_terminator(b->cur_block->last->opcode)) ir_build_br(b, cb);
    ctx->break_blk = save_brk; ctx->cont_blk = save_cnt;
    ir_builder_set_block(b, mb);
}

void gen_stmt_do_while(GenCtx* ctx, AST_Node* n)
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
    if (!b->cur_block->last || !ir_is_terminator(b->cur_block->last->opcode))
        ir_build_br(b, cb);
    ir_builder_set_block(b, cb);
    { IR_Value* c = coerce_to_i1(b, gen_expr(ctx, n->body.loop.condition));
      if (c) ir_build_cond_br(b, c, bb, mb); else ir_build_br(b, mb); }
    ctx->break_blk = save_brk; ctx->cont_blk = save_cnt;
    ir_builder_set_block(b, mb);
}

void gen_stmt_for(GenCtx* ctx, AST_Node* n)
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
    if (!b->cur_block->last || !ir_is_terminator(b->cur_block->last->opcode)) ir_build_br(b, ub);
    ir_builder_set_block(b, ub);
    gen_expr(ctx, n->body.for_stmt.update);
    ir_build_br(b, cb);
    ctx->break_blk = save_brk; ctx->cont_blk = save_cnt;
    ir_builder_set_block(b, mb);
}
