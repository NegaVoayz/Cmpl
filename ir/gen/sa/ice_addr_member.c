/* ir/gen/sa/ice_addr_member.c -- member-access address constants for the
 * ICE evaluator (split out of ice_addr.c): resolve the record/base of a
 * member access (dot form recurses into ice_addr_of; arrow form is the
 * offsetof idiom with a constant base) and finish the member address by
 * looking up the field offset.  ice_addr_member is called only by the
 * ice_addr_of dispatcher in ice_addr.c (declared in sa.h). */

#include "ir.h"

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
int
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
