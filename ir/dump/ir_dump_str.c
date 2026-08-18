/* ir_dump_str.c -- string constant table and global emission
 *
 * Collects VAL_CONST_STRING values from the IR module and emits
 * them as LLVM 19 opaque-pointer-style global constants before
 * function definitions.  Wide (L"..." wchar_t) strings are emitted
 * as [N x i32] constants; plain/u8 strings as [N x i8].
 */

#include "ir.h"

#include <stdio.h>
#include <string.h>

/* the compiler's own dumpers exceed 64 distinct string literals
 * (ir_dump_instr 169, dump_ast 130, ir_dump 108, main 85) — a table
 * smaller than that silently dropped later strings to "null" in the
 * self-built compiler. */
#define MAX_STR_CONSTS 256

typedef struct {
    String s;
    int    wide;   /* 1 = wchar (i32) elements, 0 = bytes */
} StrEntry;

static StrEntry str_table[MAX_STR_CONSTS];
static int      str_count = 0;
static int      str_emitted_count = 0;  /* strings emitted as globals so far */

/* --- forward --- */

static int  str_index_of(String s, int wide);

/* --- public API (called from ir_dump_func.c + ir_dump.c) --- */

void
dump_str_reset(void)
{
    str_count = 0;
    str_emitted_count = 0;
}

static void
dump_str_bytes(FILE* out, const char* data, int len)
{
    for (int j = 0; j < len; j++) {
        char c = data[j];

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
}

void
dump_str_globals(FILE* out)
{
    for (int i = 0; i < str_count; i++) {
        StrEntry* e = &str_table[i];

        if (e->wide) {
            fprintf(out, "@.wstr.%d = private unnamed_addr constant [%d x i32] [",
                    i, e->s.length + 1);
            for (int j = 0; j < e->s.length; j++)
                fprintf(out, "%si32 %u", j ? ", " : "",
                        (unsigned char)e->s.data[j]);
            fprintf(out, "%si32 0], align 4\n", e->s.length ? ", " : "");
        } else {
            fprintf(out, "@.str.%d = private unnamed_addr constant [%d x i8] c\"",
                    i, e->s.length + 1);
            dump_str_bytes(out, e->s.data, e->s.length);
            fprintf(out, "\\00\", align 1\n");
        }
    }
    str_emitted_count = str_count;
}

static void collect_from_value(IR_Value* val)
{
    if (!val) return;
    if (val->kind == VAL_CONST_STRING)
        str_index_of(val->body.str_val, val->is_wide);
    else if (val->kind == VAL_CONST_AGGREGATE) {
        for (int i = 0; i < val->body.aggregate.count; i++)
            collect_from_value(val->body.aggregate.elems[i]);
    }
    else if (val->kind == VAL_CONST_BITCAST)
        collect_from_value(val->body.cast_val);
    else if (val->kind == VAL_CONST_INTTOPTR)
        collect_from_value(val->body.cast_val);
}

void
dump_str_collect_module(IR_Module* mod)
{
    str_count = 0;

    /* collect strings from global initializers */
    for (IR_Value* gv = mod->globals; gv; gv = gv->next) {
        if (gv->body.init_val)
            collect_from_value(gv->body.init_val);
    }

    for (IR_Func* f = mod->funcs; f; f = f->next) {
        if (!f->blocks) continue;

        for (IR_Block* blk = f->blocks; blk; blk = blk->next) {
            for (IR_Instr* inst = blk->first; inst; inst = inst->next) {
                /* collect from call args (recursing into aggregates) */
                if (inst->opcode == IROP_CALL) {
                    for (int i = 0; i < inst->n_call_args; i++)
                        collect_from_value(inst->call_args[i]);
                }
                /* collect from fixed operands (store/return/select/etc.),
                 * recursing into VAL_CONST_AGGREGATE so a string nested in
                 * a stored/returned struct literal is not missed */
                for (int i = 0; i < 3; i++)
                    collect_from_value(inst->operands[i]);
                /* collect from phi incoming values (ternary merges) */
                if (inst->opcode == IROP_PHI) {
                    for (int i = 0; i < inst->n_incoming; i++)
                        collect_from_value(inst->in_vals[i]);
                }
            }
        }
    }
}

int
dump_str_index(String s, int wide)
{
    int idx = str_index_of(s, wide);

    /* a string appended after the globals were emitted means the collector
     * missed it — the reference would be a dangling @.str.N */
    if (idx >= str_emitted_count)
        fprintf(stderr, "cmpl: internal error: string constant not collected"
                " before emission\n");
    return idx;
}

/* --- internal --- */

static int
str_index_of(String s, int wide)
{
    for (int i = 0; i < str_count; i++) {
        if (str_table[i].s.length == s.length &&
            str_table[i].wide == wide &&
            memcmp(str_table[i].s.data, s.data, s.length) == 0)
            return i;
    }
    if (str_count >= MAX_STR_CONSTS) {
        static int warned = 0;
        if (!warned) {
            fprintf(stderr, "cmpl: error: string constant table overflow"
                    " (>%d distinct strings); later strings emitted as null\n",
                    MAX_STR_CONSTS);
            warned = 1;
        }
        return -1;
    }

    str_table[str_count].s = s;
    str_table[str_count].wide = wide;
    str_count++;
    return str_count - 1;
}
