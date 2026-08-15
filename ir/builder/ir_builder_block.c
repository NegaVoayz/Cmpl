/* ir_builder_block.c -- IR builder: block management + instruction append.
 *
 * Split out of ir_builder.c: ir_builder_new_block, ir_builder_set_block
 * and append_instr.  The first two are declared in ir_api.h; append_instr
 * is in ir_builder.h.
 */

#include "ir.h"

#include <stdio.h>
#include <string.h>

#include "arena.h"

#include "ir_builder.h"

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
