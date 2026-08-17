/* ir_type_bfq.c -- bit-field struct queries shared by access + init gen.
 *
 * Field indices are RAW (parallel to the AST field list, anonymous
 * fields included).  Positional initializer cursors advance over NAMED
 * fields only (gcc skips unnamed members), so the named helpers map
 * between the two.
 */

#include "ir.h"
#include "ast.h"

int
ir_has_bitfields(IR_Type* t)
{
    return t && (t->kind == IR_STRUCT || t->kind == IR_UNION) &&
           t->has_bitfields;
}

/* per-field layout node at raw field index (NULL when out of range or
 * the struct has no bit-fields) */
IR_FieldInfo*
ir_field_info(IR_Type* t, int raw_idx)
{
    if (!ir_has_bitfields(t) || raw_idx < 0) return NULL;
    IR_FieldInfo* fi = t->field_info;
    for (int i = 0; fi && i < raw_idx; i++) fi = fi->next;
    return fi;
}

/* total field count (unnamed included) */
int
ir_struct_field_count(IR_Type* t)
{
    if (!ir_has_bitfields(t)) return 0;
    int n = 0;
    for (IR_FieldInfo* fi = t->field_info; fi; fi = fi->next) n++;
    return n;
}

int
ir_struct_named_count(IR_Type* t)
{
    if (!ir_has_bitfields(t)) return 0;
    int n = 0;
    AST_Node* f = ir_struct_ast_lookup(t)->params;
    for (; f && f->type == AST_VAR_DECL; f = f->next)
        if (f->body.var_decl.name.data) n++;
    return n;
}

/* raw field index of the named_idx-th named field (skipping unnamed);
 * -1 when out of range */
int
ir_struct_named_at(IR_Type* t, int named_idx)
{
    if (!ir_has_bitfields(t)) return -1;
    AST_Node* f = ir_struct_ast_lookup(t)->params;
    int named = 0;
    for (int i = 0; f && f->type == AST_VAR_DECL; f = f->next, i++) {
        if (f->body.var_decl.name.data) {
            if (named == named_idx) return i;
            named++;
        }
    }
    return -1;
}

/* first named field at or after raw_idx; total field count when none */
int
ir_struct_next_named(IR_Type* t, int raw_idx)
{
    if (!ir_has_bitfields(t)) return raw_idx;
    AST_Node* f = ir_struct_ast_lookup(t)->params;
    for (int i = 0; f && f->type == AST_VAR_DECL; f = f->next, i++) {
        if (i >= raw_idx && f->body.var_decl.name.data) return i;
    }
    return ir_struct_field_count(t);
}

/* number of NAMED fields before raw index (positional cursor math) */
int
ir_struct_named_before(IR_Type* t, int raw_idx)
{
    if (!ir_has_bitfields(t)) return raw_idx < 0 ? 0 : raw_idx;
    int n = 0;
    AST_Node* f = ir_struct_ast_lookup(t)->params;
    for (int i = 0; f && f->type == AST_VAR_DECL && i < raw_idx;
         f = f->next, i++)
        if (f->body.var_decl.name.data) n++;
    return n;
}

/* member index whose natural byte range contains `byte_off`; -1 when
 * none.  *mstart receives the member's natural byte offset. */
int
ir_struct_member_at(IR_Type* t, int byte_off, int* mstart)
{
    if (!t || (t->kind != IR_STRUCT && t->kind != IR_UNION) ||
        !t->members) return -1;
    int off = 0;
    int mi = 0;
    for (IR_Type* m = t->members; m; m = m->next, mi++) {
        int al = ir_type_align(m);
        int sz = ir_type_size(m);
        off = (off + al - 1) / al * al;
        if (byte_off >= off && byte_off < off + sz) {
            if (mstart) *mstart = off;
            return mi;
        }
        off += sz;
    }
    return -1;
}
