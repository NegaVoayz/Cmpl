/* ir/gen/sa/ice_addr.c -- C11 _Static_assert evaluation: address-of.
 *
 * _Static_assert(integer-constant-expression, string-literal) is parsed
 * into an AST_STATIC_ASSERT node (parser/ll/ll_sa.c) and emits no code.
 * The evaluator runs during module IR gen (after typedef/struct/enum
 * resolution, so TYPE_NAMED/struct refs inside sizeof/_Alignof/casts are
 * resolved) and evaluates the condition with C integer-constant-expression
 * semantics: literals, unary + - ~ !, binary arith/shift/cmp/logical,
 * ternary, casts, sizeof and _Alignof.  A false or non-constant condition
 * prints a diagnostic and sets mod->had_error so ir_gen_program returns
 * NULL and the compile exits nonzero (gcc parity).
 *
 * This file holds the address-constant machinery (ice_addr_of) and the
 * shared truncate/widen helpers; the evaluator itself lives in the other
 * sa/ files (ice_eval.c, ice_binary.c, ice_cast_sizeof.c, ice_type.c,
 * sa_walk.c).
 */

#include "ir.h"

#include <stdio.h>
#include <string.h>

#include "ast.h"
#include "../ir_gen.h"
#include "sa.h"

/* ptr_name sentinel for an address constant with NO named root: the
 * offsetof idiom &((struct S*)C)->m has a known byte address (base +
 * member offset) but no object name.  Such an address may convert to an
 * integer (the offsetof value) and to a pointer (inttoptr); it can never
 * dump as getelementptr @name. */
static const char ice_no_base_data[] = "";

static const String ICE_NO_BASE = { ice_no_base_data, 0 };

/* truncate v to (bits, uns): mask, then sign-extend when signed */
void
ice_trunc(ICEVal* v, int bits, int uns)
{
    v->bits = bits;
    v->uns = uns;
    if (bits >= 64) return;

    long long mask = (1LL << bits) - 1;

    v->v &= mask;
    if (!uns && (v->v & (1LL << (bits - 1))))
        v->v |= ~mask;
}

/* widen to `bits` (the stored value is already sign/zero-extended) */
void
ice_convert(ICEVal* v, int bits)
{
    if (v->bits < bits)
        v->bits = bits;
}

/* ------------------------------------------------------------------
 *  Address-of in a constant expression: &g, &arr[i], &s.f, &arr[i].f,
 *  and nested member/index chains rooted at a file-scope global.  The
 *  result is is_ptr with ptr_name + ptr_off (byte offset) + ptr_elem
 *  (pointee byte size, used to scale later pointer arithmetic).
 *  Pointer-deref chains (p->f, p[i], *p) are rejected: they need the
 *  pointer's runtime value.
 * ------------------------------------------------------------------ */

/* resolved base of a member access: the record type plus the base
 * address (name + byte offset) and the AST struct type for field lookup
 * (the arrow path keeps the cast's type_expr in hand). */
typedef struct {
    IR_Type* rec;
    String   base_name;
    long long base_off;
    Type*    arrow_ast;
} IceAddrBase;

/* resolve the record/base of a member access: the arrow form is the
 * offsetof idiom (constant base C + member offset), the dot form
 * recurses into ice_addr_of on the record. */
static int
ice_addr_member_base(Arena* a, AST_Node* e, ICEVal* out,
                     const char** why, HashMap* globals, IceAddrBase* b)
{
    if (e->body.member.op == TOK_ARROW) {
        /* offsetof idiom: &((struct S*)C)->m with a CONSTANT base C
         * (usually 0).  The address is base + member offset — a
         * constant even though the base is not a named object; the
         * result carries an EMPTY ptr_name (no named root).  The
         * record must be a pointer-targeted cast chain whose
         * innermost operand is an integer constant; p->m with a
         * runtime pointer stays rejected. */
        AST_Node* rn = e->body.member.record;

        if (rn->type != AST_CAST) {
            if (why) *why = "address constant through a pointer is not "
                            "supported";
            return -1;
        }
        IR_Type* rt = ir_type_from_ast(a, rn->body.cast.type_expr);

        if (!rt || rt->kind != IR_PTR) {
            if (why) *why = "address constant through a pointer is not "
                            "supported";
            return -1;
        }
        AST_Node* bn = rn;
        while (bn->type == AST_CAST)
            bn = bn->body.cast.cast_expr;
        ICEVal bv;

        if (ice_eval(a, bn, &bv, why, globals) ||
            bv.is_float || bv.is_ptr) {
            if (why) *why = "address constant through a pointer is not "
                            "supported";
            return -1;
        }
        b->rec = rt->inner;
        b->base_name = ICE_NO_BASE;
        b->base_off = bv.v;
        /* the struct AST type for field lookup: the cast's type_expr
         * is in hand, so no ir_struct_ast_lookup needed — the ast_map
         * is bounded and cache-off churn during enum-value evaluation
         * can fill it, silently dropping later registrations */
        b->arrow_ast = rn->body.cast.type_expr;
        while (b->arrow_ast && (b->arrow_ast->kind == TYPE_PTR ||
                                b->arrow_ast->kind == TYPE_NAMED))
            b->arrow_ast = b->arrow_ast->inner;
        return 0;
    }

    b->rec = ice_expr_type(a, e->body.member.record, globals);
    if (!b->rec || (b->rec->kind != IR_STRUCT && b->rec->kind != IR_UNION)) {
        if (why) *why = "member of non-struct in address constant";
        return -1;
    }
    if (ice_addr_of(a, e->body.member.record, out, why, globals))
        return -1;
    b->base_name = out->ptr_name;
    b->base_off = out->ptr_off;
    b->arrow_ast = NULL;
    return 0;
}

/* finish a member address: look up the field offset and fill `out` */
static int
ice_addr_member(Arena* a, AST_Node* e, ICEVal* out, const char** why,
                HashMap* globals)
{
    IceAddrBase b;
    if (ice_addr_member_base(a, e, out, why, globals, &b)) return -1;

    Type* ast = b.arrow_ast ? b.arrow_ast : ir_struct_ast_lookup(b.rec);
    int fidx = ast ? ir_struct_field_index(ast,
                                           e->body.member.member) : -1;
    if (fidx < 0) {
        if (why) *why = "no such field in address constant";
        return -1;
    }
    int foff;
    if (ir_has_bitfields(b.rec)) {
        IR_FieldInfo* fi = ir_field_info(b.rec, fidx);
        if (!fi || fi->width != 0) {
            if (why) *why = "address of a bit-field is not a constant";
            return -1;
        }
        foff = fi->byte_off;
    } else {
        foff = ir_struct_field_offset(b.rec, fidx);
    }
    if (foff < 0) {
        if (why) *why = "cannot place field in address constant";
        return -1;
    }
    out->is_ptr = 1;
    out->ptr_name = b.base_name;
    out->ptr_off = b.base_off + foff;
    { IR_Type* ft = ice_expr_type(a, e, globals);
      out->ptr_elem = ft ? ir_type_size(ft) : 0; }
    return 0;
}

int
ice_addr_of(Arena* a, AST_Node* e, ICEVal* out, const char** why,
            HashMap* globals)
{
    switch (e->type) {
    case AST_IDENT:
    {   Type* t = globals ? (Type*)hashmap_get(globals,
                                               e->body.ident.name) : NULL;
        if (!t) { if (why) *why = "unknown identifier in address constant";
                  return -1; }
        out->is_ptr = 1;
        out->ptr_name = e->body.ident.name;
        out->ptr_off = 0;
        out->bits = 64;
        out->uns = 0;
        { IR_Type* it = ir_type_from_ast(a, t);
          out->ptr_elem = it ? ir_type_size(it) : 0; }
        return 0;
    }

    case AST_INDEX:
    {   /* the array operand must be an ARRAY, not a pointer (p[i]
         * depends on p's runtime value) */
        IR_Type* at = ice_expr_type(a, e->body.subscript.array, globals);
        if (!at || at->kind != IR_ARRAY) {
            if (why) *why = "address constant through a pointer is not "
                            "supported";
            return -1;
        }
        ICEVal iv;
        if (ice_eval(a, e->body.subscript.index, &iv, why, globals) ||
            iv.is_float || iv.is_ptr) {
            if (why) *why = "non-constant index in address constant";
            return -1;
        }
        if (ice_addr_of(a, e->body.subscript.array, out, why, globals))
            return -1;
        int esz = ir_type_size(at->inner);
        if (esz <= 0) {
            if (why) *why = "incomplete element in address constant";
            return -1;
        }
        out->ptr_off += iv.v * (long long)esz;
        out->ptr_elem = esz;
        return 0;
    }

    case AST_MEMBER:
        return ice_addr_member(a, e, out, why, globals);

    default:
        if (why) *why = "unsupported address constant in static assertion";
        return -1;
    }
}
