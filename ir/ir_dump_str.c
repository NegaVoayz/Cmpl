/* ir_dump_str.c -- string constant table and global emission
 *
 * Collects VAL_CONST_STRING values from the IR module and emits
 * them as LLVM 19 opaque-pointer-style global constants before
 * function definitions.
 */

#include "ir.h"

#include <stdio.h>
#include <string.h>

#define MAX_STR_CONSTS 64

static String  str_table[MAX_STR_CONSTS];
static int     str_count = 0;

/* --- forward --- */

static int  str_index_of(String s);

/* --- public API (called from ir_dump_func.c + ir_dump.c) --- */

void
dump_str_reset(void)
{
    str_count = 0;
}

void
dump_str_globals(FILE* out)
{
    for (int i = 0; i < str_count; i++) {
        String* s = &str_table[i];

        fprintf(out, "@.str.%d = private unnamed_addr constant [%d x i8] c\"",
                i, s->length + 1);

        for (int j = 0; j < s->length; j++) {
            char c = s->data[j];
            if (c == '\n')
                fprintf(out, "\\0A");
            else if (c == '\t')
                fprintf(out, "\\09");
            else if (c == '\r')
                fprintf(out, "\\0D");
            else if (c == '"')
                fprintf(out, "\\22");
            else if (c == '\\')
                fprintf(out, "\\\\");
            else if (c >= 32 && c < 127)
                fputc(c, out);
            else
                fprintf(out, "\\%02X", (unsigned char)c);
        }
        fprintf(out, "\\00\", align 1\n");
    }
}

void
dump_str_collect_module(IR_Module* mod)
{
    str_count = 0;

    for (IR_Func* f = mod->funcs; f; f = f->next) {
        if (!f->blocks) continue;

        for (IR_Block* blk = f->blocks; blk; blk = blk->next) {
            for (IR_Instr* inst = blk->first; inst; inst = inst->next) {
                if (inst->opcode == IROP_CALL) {
                    for (int i = 0; i < inst->n_call_args; i++) {
                        IR_Value* arg = inst->call_args[i];
                        if (arg && arg->kind == VAL_CONST_STRING)
                            str_index_of(arg->body.str_val);
                    }
                }
            }
        }
    }
}

int
dump_str_index(String s)
{
    return str_index_of(s);
}

/* --- internal --- */

static int
str_index_of(String s)
{
    for (int i = 0; i < str_count; i++) {
        if (str_table[i].length == s.length &&
            memcmp(str_table[i].data, s.data, s.length) == 0)
            return i;
    }
    if (str_count >= MAX_STR_CONSTS)
        return -1;

    str_table[str_count++] = s;
    return str_count - 1;
}
