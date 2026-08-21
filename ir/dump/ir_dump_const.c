/* ir_dump_const.c -- constant-value printers for the IR text dumper.
 *
 * Split out of ir_dump.c in B-20.  sign_extend_int, dump_const_float and
 * dump_const_aggregate are called from dump_value (ir_dump.c); they are
 * non-static and declared in ir_dump.h.
 */

#include "ir.h"

#include <stdio.h>
#include <string.h>

#include "ir_dump.h"

/* print an integer constant as two's-complement within its type width:
 * a u32 value >= 2^31 is stored as a positive long long but must dump as
 * a negative i32 (LLVM integer literals are signed decimal). */
long long
sign_extend_int(long long v, IR_Type* ty)
{
    int bits = ty ? ir_type_size(ty) * 8 : 64;
    if (bits >= 64) return v;

    long long mask = (1LL << bits) - 1;
    v &= mask;
    if (v & (1LL << (bits - 1)))
        v -= (1LL << bits);
    return v;
}

void
dump_const_float(FILE* out, IR_Value* val)
{
    /* LLVM requires a decimal point or exponent in float literals.
     * %.17g round-trips any double (17 significant digits); the default
     * %g only keeps 6 and silently truncates precision.  LLVM's IR reader
     * also requires an f32 literal to downcast EXACTLY, so a f32-typed
     * constant must be rounded to float precision before printing
     * ("0.10000000000000001" would be rejected for type float).  %g
     * prints integral values like 7.0 as "7", which clang rejects as an
     * integer constant — append ".0" when needed.  %g also prints 4e+09
     * for large values — clang's IR reader rejects an exponent without a
     * decimal point ("4e+09" is lexed as an integer), so insert ".0"
     * before the exponent. */
    double d = val->body.float_val;
    if (val->type && ir_type_size(val->type) == 4)
        d = (double)(float)d;
    char buf[64];
    snprintf(buf, sizeof(buf), "%.17g", d);
    char* e = strpbrk(buf, "eE");
    if (e && !strchr(buf, '.')) {
        size_t n = (size_t)(e - buf);
        memmove(e + 2, e, strlen(e) + 1);
        buf[n] = '.';
        buf[n + 1] = '0';
    }
    fprintf(out, "%s", buf);
    if (!strchr(buf, '.') && !strchr(buf, 'e') &&
        !strchr(buf, 'E') && !strchr(buf, 'i') &&
        !strchr(buf, 'n'))
        fprintf(out, ".0");
}

void
dump_const_aggregate(FILE* out, IR_Value* val)
{
    /* emit values with per-element types (no outer type).
     * arrays: [T v0, T v1, ...]
     * structs: {T v0, T v1, ...}
     * the type is provided by the caller (global line or parent). */
    if (val->body.aggregate.count == 0) {
        fprintf(out, "zeroinitializer");
    } else {
        fprintf(out, val->type->kind == IR_STRUCT ||
                val->type->kind == IR_UNION ? "{" : "[");
        for (int i = 0; i < val->body.aggregate.count; i++) {
            if (i > 0) fprintf(out, ", ");
            dump_type(out, val->body.aggregate.elems[i]->type);
            fprintf(out, " ");
            dump_value(out, val->body.aggregate.elems[i]);
        }
        fprintf(out, val->type->kind == IR_STRUCT ||
                val->type->kind == IR_UNION ? "}" : "]");
    }
}
