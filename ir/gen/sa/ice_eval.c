/* ir/gen/sa/ice_eval.c -- the ICE constant-expression dispatcher.
 *
 * ice_eval recursively evaluates a C integer constant expression.  The
 * heavy cases are call-outs to the sibling sa/ files (binary operators
 * to ice_binary.c, casts and sizeof/_Alignof to ice_cast_sizeof.c, the
 * address-of operator to ice_addr.c); the light cases (literals,
 * identifiers, unary, ternary, subscripts) stay inline here.  Returns 0
 * on success; -1 on a non-constant or invalid condition, with *why (if
 * non-NULL) set to a reason.
 */

#include "ir.h"

#include <stdio.h>
#include <string.h>

#include "ast.h"
#include "../ir_gen.h"
#include "sa.h"

/* AST_IDENT: a global array in a value context decays to a pointer to
 * its first element. */
static int
ice_eval_ident(Arena* a, AST_Node* e, ICEVal* out, const char** why,
               HashMap* globals)
{
    /* ptr_elem is the ELEMENT size, so mat + 1 on int mat[2][3] steps
     * a whole row (12 bytes), matching gcc.  Scalar globals are not
     * constant expressions.  A typedef'd array (typedef int A4[4];
     * A4 garr) is TYPE_NAMED wrapping TYPE_ARRAY — unwrap before the
     * kind test. */
    Type* t = globals ? (Type*)hashmap_get(globals,
                                           e->body.ident.name) : NULL;
    if (!t) {
        if (why) *why = "unknown identifier in constant expression";
        return -1;
    }
    while (t->kind == TYPE_NAMED && t->inner) t = t->inner;
    if (t->kind != TYPE_ARRAY) {
        if (why) *why = "identifier is not a constant array";
        return -1;
    }
    IR_Type* at = ir_type_from_ast(a, t);
    if (!at || at->kind != IR_ARRAY || !at->inner) {
        if (why) *why = "array element type is unknown";
        return -1;
    }
    int esz = ir_type_size(at->inner);
    if (esz <= 0) {
        if (why) *why = "array element is incomplete";
        return -1;
    }
    out->is_ptr = 1;
    out->ptr_name = e->body.ident.name;
    out->ptr_off = 0;
    out->ptr_elem = esz;
    out->bits = 64;
    out->uns = 0;
    return 0;
}

/* AST_INDEX: mat[0] is an array lvalue (int[3]) that decays to a
 * pointer to its first element. */
static int
ice_eval_index(Arena* a, AST_Node* e, ICEVal* out, const char** why,
               HashMap* globals)
{
    /* the array operand must be an address constant (bare ident decay
     * or &-rooted chain) and the indexed element must be an ARRAY — a
     * scalar element (garr[0]) is a load, not a constant. */
    IR_Type* at = ice_expr_type(a, e->body.subscript.array, globals);
    if (!at || at->kind != IR_ARRAY || !at->inner) {
        if (why) *why = "subscript of a non-array is not constant";
        return -1;
    }
    ICEVal iv;
    if (ice_eval(a, e->body.subscript.index, &iv, why, globals) ||
        iv.is_float || iv.is_ptr) {
        if (why) *why = "non-constant index in constant expression";
        return -1;
    }
    ICEVal base;
    if (ice_eval(a, e->body.subscript.array, &base, why, globals) ||
        !base.is_ptr || base.ptr_elem <= 0) {
        if (why) *why = "array operand is not an address constant";
        return -1;
    }
    if (at->inner->kind != IR_ARRAY) {
        if (why) *why = "array element is not a constant array";
        return -1;
    }
    out->is_ptr = 1;
    out->ptr_name = base.ptr_name;
    out->ptr_off = base.ptr_off + iv.v * (long long)base.ptr_elem;
    out->ptr_elem = ir_type_size(at->inner->inner);
    out->bits = 64;
    out->uns = 0;
    return 0;
}

/* AST_INT_LIT / AST_LONG_LIT: C decimal constant typing — int if it
 * fits, else long (the lexer does not promote a too-large decimal). */
static int
ice_eval_int_lit(AST_Node* e, ICEVal* out)
{
    out->v = e->body.literal.int_val;
    out->is_float = 0;
    if (e->type == AST_LONG_LIT) {
        out->bits = 64;
        out->uns = e->body.literal.is_unsigned;
    } else {
        if (e->body.literal.is_unsigned)
            out->bits = (out->v > 4294967295LL) ? 64 : 32;
        else
            out->bits = (out->v > 2147483647LL || out->v < -2147483648LL)
                        ? 64 : 32;
        out->uns = e->body.literal.is_unsigned;
    }
    return 0;
}

/* AST_UNARY: address constants (&g, &garr[i], &s.f, nested chains) and
 * arithmetic negation/complement/logical-not. */
static int
ice_eval_unary(Arena* a, AST_Node* e, ICEVal* out, const char** why,
               HashMap* globals)
{
    ICEVal op;

    /* the address-constant path carries a nonzero element/field offset
     * as a byte offset and dumps as getelementptr (i8, ptr @name,
     * i64 off). */
    if (e->body.unary.op == TOK_AMP)
        return ice_addr_of(a, e->body.unary.operand, out, why, globals);

    if (ice_eval(a, e->body.unary.operand, &op, why, globals)) return -1;

    if (op.is_float) {
        if (e->body.unary.op == TOK_MINUS) { op.f = -op.f; *out = op; return 0; }
        if (e->body.unary.op == TOK_PLUS)  { *out = op; return 0; }
        if (why) *why = "non-integer operand in static assertion";
        return -1;
    }

    switch (e->body.unary.op) {
    case TOK_MINUS: op.v = -op.v; ice_trunc(&op, op.bits, op.uns); *out = op; return 0;
    case TOK_PLUS:  *out = op; return 0;
    case TOK_TILDE: op.v = ~op.v; ice_trunc(&op, op.bits, op.uns); *out = op; return 0;
    case TOK_BANG:
        op.v = (op.v == 0);
        ice_trunc(&op, 32, 0);
        *out = op;
        return 0;
    default: break;
    }
    if (why) *why = "unsupported operator in static assertion";
    return -1;
}

int
ice_eval(Arena* a, AST_Node* e, ICEVal* out, const char** why,
         HashMap* globals)
{
    if (!e) { if (why) *why = "empty condition"; return -1; }

    out->is_float = 0;   /* every non-float path leaves this at 0 */
    out->is_ptr = 0;
    out->ptr_off = 0;
    out->ptr_elem = 0;
    out->v = 0;

    switch (e->type) {
    case AST_IDENT:
        return ice_eval_ident(a, e, out, why, globals);

    case AST_INDEX:
        return ice_eval_index(a, e, out, why, globals);

    case AST_INT_LIT:
    case AST_LONG_LIT:
        return ice_eval_int_lit(e, out);

    case AST_CHAR_LIT:
        /* character constants have type int in C */
        out->v = (signed char)e->body.literal.char_val;
        out->bits = 32;
        out->uns = 0;
        out->is_float = 0;
        return 0;

    case AST_FLOAT_LIT:
    case AST_DOUBLE_LIT:
        /* only valid as the immediate operand of a cast */
        out->is_float = 1;
        out->f = e->body.literal.float_val;
        out->v = 0;
        out->bits = 64;
        out->uns = 0;
        return 0;

    case AST_UNARY:
        return ice_eval_unary(a, e, out, why, globals);

    case AST_BINARY:
        return ice_eval_binary(a, e, out, why, globals);

    case AST_TERNARY:
    { ICEVal c;

      if (ice_eval(a, e->body.ternary.cond, &c, why, globals)) return -1;
      if (c.is_float) {
          if (why) *why = "non-integer condition in static assertion";
          return -1;
      }
      return ice_eval(a, c.v ? e->body.ternary.then_expr
                             : e->body.ternary.else_expr, out, why, globals);
    }

    case AST_CAST:
        return ice_eval_cast(a, e, out, why, globals);

    case AST_SIZEOF_TYPE:
    case AST_ALIGNOF_TYPE:
    case AST_SIZEOF_EXPR:
    case AST_ALIGNOF_EXPR:
        return ice_eval_sizeof(a, e, out, why, globals);

    default: break;
    }

    if (why) *why = "condition is not an integer constant expression";
    return -1;
}
