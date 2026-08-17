/* ir_gen_sa.c -- C11 _Static_assert evaluation.
 *
 * _Static_assert(integer-constant-expression, string-literal) is parsed
 * into an AST_STATIC_ASSERT node (parser/ll/ll_sa.c) and emits no code.
 * This walker runs during module IR gen (after typedef/struct/enum
 * resolution, so TYPE_NAMED/struct refs inside sizeof/_Alignof/casts are
 * resolved) and evaluates the condition with C integer-constant-expression
 * semantics: literals, unary + - ~ !, binary arith/shift/cmp/logical,
 * ternary, casts, sizeof and _Alignof.  A false or non-constant condition
 * prints a diagnostic and sets mod->had_error so ir_gen_program returns
 * NULL and the compile exits nonzero (gcc parity).
 */

#include "ir.h"

#include <stdio.h>
#include <string.h>

#include "ast.h"
#include "ast_walk.h"
#include "ir_gen.h"

/* one evaluated integer constant: value + type width/signedness.  The
 * width matters because C ICEs are computed in the operand's type: int
 * arithmetic wraps at 32 bits (0x7fffffff + 1 == -2147483648), while
 * long/sizeof expressions stay 64-bit.  Signed values are stored
 * sign-extended to 64 bits, so widening is a no-op and unsigned
 * reinterprets are a mask. */
typedef struct {
    long long v;        /* value (sign-extended to 64 for signed) */
    int       bits;     /* 1, 8, 16, 32, or 64 */
    int       uns;      /* unsigned type? */
    int       is_float; /* float literal (valid only as cast operand) */
    double    f;        /* float literal value */
} ICEVal;

/* truncate v to (bits, uns): mask, then sign-extend when signed */
static void
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
static void
ice_convert(ICEVal* v, int bits)
{
    if (v->bits < bits)
        v->bits = bits;
}

/* ------------------------------------------------------------------
 *  Recursive evaluator.  Returns 0 on success; -1 on a non-constant
 *  or invalid condition, with *why (if non-NULL) set to a reason.
 * ------------------------------------------------------------------ */

static int
ice_eval(Arena* a, AST_Node* e, ICEVal* out, const char** why)
{
    if (!e) { if (why) *why = "empty condition"; return -1; }

    out->is_float = 0;   /* every non-float path leaves this at 0 */

    switch (e->type) {
    case AST_INT_LIT:
    case AST_LONG_LIT:
        out->v = e->body.literal.int_val;
        out->is_float = 0;
        if (e->type == AST_LONG_LIT) {
            out->bits = 64;
            out->uns = e->body.literal.is_unsigned;
        } else {
            /* C decimal constant typing: int if it fits, else long
             * (the lexer does not promote a too-large decimal) */
            if (e->body.literal.is_unsigned)
                out->bits = (out->v > 4294967295LL) ? 64 : 32;
            else
                out->bits = (out->v > 2147483647LL || out->v < -2147483648LL)
                            ? 64 : 32;
            out->uns = e->body.literal.is_unsigned;
        }
        return 0;

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
    { ICEVal op;

      if (ice_eval(a, e->body.unary.operand, &op, why)) return -1;

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

    case AST_BINARY:
    { TokenKind op = e->body.binary.op;
      ICEVal l, r;

      /* && / || : every operand of an ICE must be constant (no
       * side effects to skip with short-circuiting) */
      if (op == TOK_AMPAMP || op == TOK_PIPEPIPE) {
          if (ice_eval(a, e->body.binary.left, &l, why)) return -1;
          if (ice_eval(a, e->body.binary.right, &r, why)) return -1;
          if (l.is_float || r.is_float) {
              if (why) *why = "non-integer operand in static assertion";
              return -1;
          }
          out->v = (op == TOK_AMPAMP) ? (l.v && r.v) : (l.v || r.v);
          ice_trunc(out, 32, 0);
          return 0;
      }

      if (ice_eval(a, e->body.binary.left, &l, why)) return -1;
      if (ice_eval(a, e->body.binary.right, &r, why)) return -1;
      if (l.is_float || r.is_float) {
          if (why) *why = "non-integer operand in static assertion";
          return -1;
      }

      switch (op) {
      case TOK_EQEQ: case TOK_BANGEQ:
      case TOK_LT:   case TOK_GT:   case TOK_LTEQ: case TOK_GTEQ:
      { int cbits = (l.bits == 64 || r.bits == 64) ? 64 : 32;
        int cuns  = l.uns || r.uns;
        unsigned long long mask = (cbits == 64) ? ~0ULL
                                                : ((1ULL << cbits) - 1);
        int res;

        ice_convert(&l, cbits);
        ice_convert(&r, cbits);

        if (cuns) {
            unsigned long long ul = (unsigned long long)(l.v & mask);
            unsigned long long ur = (unsigned long long)(r.v & mask);

            switch (op) {
            case TOK_EQEQ:  res = (ul == ur); break;
            case TOK_BANGEQ:res = (ul != ur); break;
            case TOK_LT:    res = (ul < ur);  break;
            case TOK_GT:    res = (ul > ur);  break;
            case TOK_LTEQ:  res = (ul <= ur); break;
            default:        res = (ul >= ur); break;
            }
        } else {
            switch (op) {
            case TOK_EQEQ:  res = (l.v == r.v); break;
            case TOK_BANGEQ:res = (l.v != r.v); break;
            case TOK_LT:    res = (l.v < r.v);  break;
            case TOK_GT:    res = (l.v > r.v);  break;
            case TOK_LTEQ:  res = (l.v <= r.v); break;
            default:        res = (l.v >= r.v); break;
            }
        }
        out->v = res;
        ice_trunc(out, 32, 0);
        return 0;
      }

      case TOK_PLUS: case TOK_MINUS: case TOK_STAR:
      case TOK_SLASH: case TOK_PERCENT:
      { int cbits = (l.bits == 64 || r.bits == 64) ? 64 : 32;
        int cuns  = l.uns || r.uns;
        long long res;

        ice_convert(&l, cbits);
        ice_convert(&r, cbits);

        switch (op) {
        case TOK_PLUS:    res = l.v + r.v; break;
        case TOK_MINUS:   res = l.v - r.v; break;
        case TOK_STAR:    res = l.v * r.v; break;
        case TOK_SLASH:
            if (r.v == 0) {
                if (why) *why = "division by zero in static assertion";
                return -1;
            }
            res = l.v / r.v;
            break;
        case TOK_PERCENT:
            if (r.v == 0) {
                if (why) *why = "division by zero in static assertion";
                return -1;
            }
            res = l.v % r.v;
            break;
        default: res = 0; break;
        }
        out->v = res;
        ice_trunc(out, cbits, cuns);
        return 0;
      }

      case TOK_LTLT: case TOK_GTGT:
      { long long res;

        if (r.v < 0) {
            if (why) *why = "negative shift count in static assertion";
            return -1;
        }
        if (r.v >= 64)
            res = (op == TOK_LTLT) ? 0 : (l.v < 0 ? -1 : 0);
        else
            res = (op == TOK_LTLT) ? (l.v << r.v) : (l.v >> r.v);

        out->v = res;
        ice_trunc(out, l.bits, l.uns);
        return 0;
      }

      case TOK_AMP: case TOK_PIPE: case TOK_CARET:
      { int cbits = (l.bits == 64 || r.bits == 64) ? 64 : 32;
        int cuns  = l.uns || r.uns;
        long long res;

        ice_convert(&l, cbits);
        ice_convert(&r, cbits);

        switch (op) {
        case TOK_AMP:   res = l.v & r.v; break;
        case TOK_PIPE:  res = l.v | r.v; break;
        default:        res = l.v ^ r.v; break;
        }
        out->v = res;
        ice_trunc(out, cbits, cuns);
        return 0;
      }

      default:
        if (why) *why = "unsupported operator in static assertion";
        return -1;
      }
    }

    case AST_TERNARY:
    { ICEVal c;

      if (ice_eval(a, e->body.ternary.cond, &c, why)) return -1;
      if (c.is_float) {
          if (why) *why = "non-integer condition in static assertion";
          return -1;
      }
      return ice_eval(a, c.v ? e->body.ternary.then_expr
                             : e->body.ternary.else_expr, out, why);
    }

    case AST_CAST:
    { IR_Type* t = ir_type_from_ast(a, e->body.cast.type_expr);
      ICEVal op;

      if (ice_eval(a, e->body.cast.cast_expr, &op, why)) return -1;

      if (!t) { if (why) *why = "cast target type is unknown"; return -1; }
      if (op.is_float) {
          op.v = (long long)op.f;   /* C truncation toward zero */
          op.is_float = 0;          /* the cast result is an integer */
      }

      switch (t->kind) {
      case IR_I1:  ice_trunc(&op, 1, 1);  *out = op; return 0;
      case IR_I8:  ice_trunc(&op, 8, t->is_unsigned);  *out = op; return 0;
      case IR_I16: ice_trunc(&op, 16, t->is_unsigned); *out = op; return 0;
      case IR_I32: ice_trunc(&op, 32, t->is_unsigned); *out = op; return 0;
      case IR_I64: ice_trunc(&op, 64, t->is_unsigned); *out = op; return 0;
      default:
          if (why) *why = "cast target is not an integer type in static assertion";
          return -1;
      }
    }

    case AST_SIZEOF_TYPE:
    { IR_Type* t = ir_type_from_ast(a, e->body.sizeof_type.type_expr);

      if (!t || t->kind == IR_VOID) {
          if (why) *why = "sizeof incomplete type in static assertion";
          return -1;
      }
      out->v = ir_type_size(t);
      out->bits = 64;
      out->uns = 1;       /* size_t */
      out->is_float = 0;
      return 0;
    }

    case AST_ALIGNOF_TYPE:
    { IR_Type* t = ir_type_from_ast(a, e->body.sizeof_type.type_expr);

      if (!t || t->kind == IR_VOID) {
          if (why) *why = "_Alignof incomplete type in static assertion";
          return -1;
      }
      out->v = ir_type_align(t);
      out->bits = 64;
      out->uns = 1;       /* size_t */
      out->is_float = 0;
      return 0;
    }

    case AST_SIZEOF_EXPR:
    case AST_ALIGNOF_EXPR:
    { AST_Node* op = e->body.sizeof_expr.expr;
      int sz, al;

      if (!op) { if (why) *why = "empty sizeof operand"; return -1; }
      if (op->type == AST_IDENT) {
          if (why) *why = "sizeof variable is not an integer constant expression";
          return -1;
      }

      switch (op->type) {
      case AST_INT_LIT: case AST_LONG_LIT: case AST_CHAR_LIT:
          sz = 4; al = 4; break;                     /* int */
      case AST_FLOAT_LIT: sz = 4; al = 4; break;
      case AST_DOUBLE_LIT: sz = 8; al = 8; break;
      case AST_STRING_LIT:
          sz = op->body.literal.str_val.length + 1;  /* char array w/ NUL */
          al = 1;
          break;
      default:
          if (why) *why = "sizeof operand is not constant in static assertion";
          return -1;
      }
      out->v = (e->type == AST_SIZEOF_EXPR) ? sz : al;
      out->bits = 64;
      out->uns = 1;
      out->is_float = 0;
      return 0;
    }

    default: break;
    }

    if (why) *why = "condition is not an integer constant expression";
    return -1;
}

/* ------------------------------------------------------------------
 *  Module walker: check every _Static_assert, fail the compile on a
 *  false or non-constant condition (gcc parity).
 * ------------------------------------------------------------------ */

typedef struct { IR_Module* mod; Arena* a; } SACtx;

static int
sa_check_cb(AST_Node* n, void* ctx)
{
    if (!n || n->type != AST_STATIC_ASSERT) return 0;

    SACtx* c = (SACtx*)ctx;
    const char* why = NULL;
    ICEVal val;

    if (ice_eval(c->a, n->body.static_assert.expr, &val, &why) != 0 ||
        val.is_float) {
        fprintf(stderr, "cmpl: error: static assertion at line %d col %d: %s\n",
                n->loc.line, n->loc.col,
                why ? why : "condition is not an integer constant expression");
        c->mod->had_error = 1;
        return 0;
    }

    if (val.v == 0) {
        fprintf(stderr, "cmpl: error: static assertion failed: \"%.*s\"\n",
                (int)n->body.static_assert.message.length,
                n->body.static_assert.message.data);
        c->mod->had_error = 1;
    }
    return 0;
}

void
ir_check_static_asserts(Arena* a, IR_Module* mod, AST_Node* root)
{
    SACtx c = { mod, a };

    for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next)
        ast_walk(decl, sa_check_cb, NULL, &c);
}
