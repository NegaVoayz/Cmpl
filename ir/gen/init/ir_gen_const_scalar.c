/* ir_gen_const_scalar.c -- scalar + string + union-aggregate constant
 * initializers (split out of ir_gen_const.c, B-8).
 *
 * gen_const_scalar builds a scalar constant of a target type from an
 * int/float value; gen_const_string lowers "..." and L"..." into a byte
 * array or a VAL_CONST_STRING; gen_const_union_aggregate places a member
 * value in the largest-member aggregate slot of a union; ir_const_ir_name
 * resolves the IR name of an address-constant root. */

#include "../ir_gen.h"
#include "ir_gen_init.h"

#include <string.h>

/* char a[N] = "s" at file scope: byte array constant, zero-padded /
 * truncated to N (C11 6.7.9p14/p21).  wchar_t a[N] = L"s" fills [N x i32]
 * elements (each source byte becomes one 4-byte wchar value).  Otherwise
 * a VAL_CONST_STRING (plain or wide). */
IR_Value*
gen_const_string(Arena* a, AST_Node* init, IR_Type* target_type)
{
    String st = init->body.literal.str_val;
    int wide = init->body.literal.wide;

    if (target_type && target_type->kind == IR_ARRAY &&
        target_type->size > 0) {
        int n = target_type->size;
        IR_Type* et = target_type->inner ? target_type->inner
                     : (wide ? t_i32 : t_i8);
        IR_Value** elems = arena_alloc(a, sizeof(IR_Value*) * n);
        for (int i = 0; i < n; i++) {
            long v = (i < (int)st.length)
                ? (unsigned char)st.data[i] : 0;
            IR_Value* ev = arena_alloc(a, sizeof(IR_Value));
            ev->kind = VAL_CONST_INT;
            ev->type = et;
            ev->body.int_val = v;
            elems[i] = ev;
        }
        return ir_const_aggregate(a, target_type, elems, n);
    }
    IR_Value* v = arena_alloc(a, sizeof(IR_Value));
    v->kind = VAL_CONST_STRING;
    v->type = target_type;
    v->is_wide = wide;
    v->body.str_val = init->body.literal.str_val;
    return v;
}

/* scalar constant of `ty` from an int/char value (iv) or a float value
 * (fv): the kind must follow the TARGET type, not the literal.  an int
 * literal initializing a float member must convert (C semantics) —
 * emitting VAL_CONST_INT with a float type would dump "{double 1}",
 * which clang rejects.  float -> int truncates toward zero (gcc parity). */
IR_Value*
gen_const_scalar(Arena* a, IR_Type* ty, long long iv, double fv)
{
    IR_Value* v = arena_alloc(a, sizeof(IR_Value));
    v->type = ty;
    if (ty && (ty->kind == IR_F32 || ty->kind == IR_F64)) {
        v->kind = VAL_CONST_FLOAT;
        v->body.float_val = fv;
    } else if (ty && ty->kind == IR_PTR) {
        /* a pointer constant: 0 dumps as `null`, nonzero must be
         * `inttoptr (i64 N to ptr)` (LLVM rejects a bare `ptr N`). */
        if (iv == 0) {
            v->kind = VAL_CONST_NULL;
        } else {
            IR_Value* i64v = arena_alloc(a, sizeof(IR_Value));
            i64v->kind = VAL_CONST_INT;
            i64v->type = t_i64;
            i64v->body.int_val = iv;
            v->kind = VAL_CONST_INTTOPTR;
            v->body.cast_val = i64v;
        }
    } else {
        v->kind = VAL_CONST_INT;
        v->body.int_val = iv;
    }
    return v;
}

/* the IR global name for an address-constant root: file-scope globals
 * use the source name; a function-scope static was registered in the
 * type table (gen_static_local) with its MANGLED name in the Type's
 * `name` field — source identifiers cannot contain '.', so a dot marks
 * a static entry. */
String
ir_const_ir_name(HashMap* globals, String src)
{
    Type* t = globals ? (Type*)hashmap_get(globals, src) : NULL;
    if (t && t->name.data && memchr(t->name.data, '.', t->name.length))
        return t->name;
    return src;
}

/* scalar-initialized union whose LARGEST member is an aggregate: place the
 * member's value-space bits in the aggregate's first element/field (the
 * member sits at offset 0), recursing into nested first-fields, and zero
 * the rest.  ir_const_reinterpret yields the exact first-slot constant
 * (e.g. bitcast(i64 5 to double) for {int a; double b[2]} {.a=5}). */
IR_Value*
gen_const_union_aggregate(Arena* a, IR_Type* agg, IR_Value* mv)
{
    int n = ir_agg_count(agg);
    if (n <= 0) return gen_const_zero(a, agg);

    IR_Value** elems = arena_alloc(a, n * sizeof(IR_Value*));
    IR_Type* first = gen_const_child_type(agg, 0);
    if (first->kind == IR_ARRAY || first->kind == IR_STRUCT)
        elems[0] = gen_const_union_aggregate(a, first, mv);
    else {
        IR_Value* cv = ir_const_reinterpret(a, mv, first);
        elems[0] = cv ? cv : gen_const_zero(a, first);
    }
    for (int i = 1; i < n; i++)
        elems[i] = gen_const_zero(a, gen_const_child_type(agg, i));
    return ir_const_aggregate(a, agg, elems, n);
}
