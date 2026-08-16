/* ir_gen_stmt_jump.c -- C goto / label codegen.
 *
 * goto L and `L:` share a function-scope label -> IR_Block map (lazily
 * populated), so forward and backward gotos both resolve to the same
 * block.  gen_stmt (ir_gen_stmt.c) dispatches AST_GOTO/AST_LABEL here;
 * ir_is_terminator and link_blocks are shared from ir_gen_stmt.c.
 */

#include "ir.h"

#include <string.h>

#include "ast.h"
#include "ir_gen.h"

/* get-or-create the IR block for a label; a forward goto creates it
 * before the label is seen, and the later gen_stmt_label reuses it.
 * link_blocks chains it into func->blocks so gen_func_terminators visits
 * it. */
static IR_Block* label_block(GenCtx* ctx, String name)
{
    IR_Builder* b = ctx->b;
    IR_Block* blk = (IR_Block*) hashmap_get(&ctx->labels, name);

    if (!blk) {
        char buf[128];
        int len = name.length < (int)sizeof(buf) - 1
                ? name.length : (int)sizeof(buf) - 1;

        memcpy(buf, name.data, len);
        buf[len] = '\0';
        blk = ir_builder_new_block(b, buf);
        link_blocks(b->cur_func, blk);
        hashmap_put(&ctx->labels, name, blk);
    }
    return blk;
}

void gen_stmt_goto(GenCtx* ctx, AST_Node* n)
{
    ir_build_br(ctx->b, label_block(ctx, n->body.jump.label));
}

void gen_stmt_label(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;
    IR_Block* lbl = label_block(ctx, n->body.label.name);

    /* if the current block is unterminated, fall through into the label
     * (mirrors the gen_stmt_if merge pattern) */
    if (!b->cur_block->last || !ir_is_terminator(b->cur_block->last->opcode))
        ir_build_br(b, lbl);

    ir_builder_set_block(b, lbl);
    gen_stmt(ctx, n->body.label.stmt);
}
