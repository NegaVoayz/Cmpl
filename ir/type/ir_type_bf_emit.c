/* ir_type_bf_emit.c -- bit-field layout helpers + member/field_info emission.
 *
 * Pass 2 tiles the record with alignment-sized natural members and pass 3
 * builds the parallel IR_FieldInfo list (split out of ir_type_bf.c, B-6).
 * The shared field-base-size / constant-width / round-up helpers live here
 * so the pass-1 placement walk in ir_type_bf.c stays under 200 lines. */

#include "ir.h"
#include "ast.h"
#include "ir_type.h"

#include <string.h>

static IR_Type*
chain_clone(Arena* a, IR_Type* src)
{
    IR_Type* cp = arena_alloc(a, sizeof(IR_Type));
    memcpy(cp, src, sizeof(IR_Type));
    cp->next = NULL;
    return cp;
}

/* byte size + signedness of a field's base type */
void
field_base_info(Arena* a, Type* var_type, int* size, int* is_signed)
{
    IR_Type* it = ast_to_ir_type(a, var_type);
    int sz = (it && it->kind != IR_VOID) ? ir_type_size(it) : 4;
    if (sz < 1 || sz > 8) sz = 4;
    *size = sz;
    /* _Bool bit-fields hold 0/1 (never sign-extended) */
    *is_signed = (it && it->kind != IR_VOID && it->kind != IR_I1)
        ? !it->is_unsigned : 0;
}

/* read a folded constant int expression (bit-field widths) */
long long
eval_width(AST_Node* e)
{
    if (!e) return 0;
    if (e->type == AST_INT_LIT || e->type == AST_LONG_LIT)
        return e->body.literal.int_val;
    if (e->type == AST_UNARY && e->body.unary.op == TOK_MINUS)
        return -eval_width(e->body.unary.operand);
    if (e->type == AST_BINARY) {
        long long l = eval_width(e->body.binary.left);
        long long r = eval_width(e->body.binary.right);
        switch (e->body.binary.op) {
        case TOK_PLUS:  return l + r;
        case TOK_MINUS: return l - r;
        case TOK_STAR:  return l * r;
        case TOK_SLASH: return r ? l / r : 0;
        default:        return l;
        }
    }
    return 0;
}

int
round_up(int v, int mul)
{
    return (v + mul - 1) / mul * mul;
}

/* pass 2: emit members tiling the record at its own alignment so the
 * .ll struct type's NATURAL LLVM layout (size = record, align = A)
 * matches gcc exactly.  Bit-field struct bytes are only ever touched
 * through byte pointers (bf_byte_ptr), so member granularity is
 * irrelevant to access/init code — only size + alignment matter for
 * embedding the struct in other structs/arrays. */
void
ir_build_bitfield_members(Arena* a, IR_Type* t, int record_bytes, int align)
{
    IR_Type* members = NULL;
    IR_Type** tail = &members;
    IR_Type* mt = (align == 8) ? t_i64 : (align == 4) ? t_i32
                  : (align == 2) ? t_i16 : t_i8;
    for (int i = 0; i < record_bytes / align; i++) {
        *tail = chain_clone(a, mt);
        tail = &(*tail)->next;
    }
    t->members = members;
}

/* pass 3: IR_FieldInfo list parallel to the AST fields */
void
ir_build_field_info(Arena* a, IR_Type* t, Type* ast, Lay* lays, int n_lays)
{
    IR_FieldInfo* fi = NULL;
    IR_FieldInfo** fit = &fi;
    int li = 0;

    for (AST_Node* f = ast->params; f && f->type == AST_VAR_DECL;
         f = f->next) {
        IR_FieldInfo* n = arena_alloc(a, sizeof(IR_FieldInfo));
        memset(n, 0, sizeof *n);
        n->ty = ast_to_ir_type(a, f->body.var_decl.var_type);
        if (li < n_lays) {
            n->byte_off = lays[li].byte_off;
            n->bit = lays[li].bit;
            n->width = lays[li].width;
            n->is_signed = lays[li].is_signed;
        }
        li++;
        *fit = n;
        fit = &n->next;
    }
    t->field_info = fi;
}
