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

/* ICEVal + ice_eval are declared in ir_gen.h: the _Static_assert
 * evaluator is also the constant-expression evaluator for const-init
 * values, enum values and array bounds (ir_gen_const_ice.c,
 * ir_gen_module.c, ir_gen_resolve.c). */

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
 *  Address-of in a constant expression: &g, &arr[i], &s.f, &arr[i].f,
 *  and nested member/index chains rooted at a file-scope global.  The
 *  result is is_ptr with ptr_name + ptr_off (byte offset) + ptr_elem
 *  (pointee byte size, used to scale later pointer arithmetic).
 *  Pointer-deref chains (p->f, p[i], *p) are rejected: they need the
 *  pointer's runtime value.
 * ------------------------------------------------------------------ */

static int
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
    {   if (e->body.member.op == TOK_ARROW) {
            if (why) *why = "address constant through a pointer is not "
                            "supported";
            return -1;
        }
        IR_Type* rec = ice_expr_type(a, e->body.member.record, globals);
        if (!rec || (rec->kind != IR_STRUCT && rec->kind != IR_UNION)) {
            if (why) *why = "member of non-struct in address constant";
            return -1;
        }
        if (ice_addr_of(a, e->body.member.record, out, why, globals))
            return -1;
        Type* ast = ir_struct_ast_lookup(rec);
        int fidx = ast ? ir_struct_field_index(ast,
                                               e->body.member.member) : -1;
        if (fidx < 0) {
            if (why) *why = "no such field in address constant";
            return -1;
        }
        int foff;
        if (ir_has_bitfields(rec)) {
            IR_FieldInfo* fi = ir_field_info(rec, fidx);
            if (!fi || fi->width != 0) {
                if (why) *why = "address of a bit-field is not a constant";
                return -1;
            }
            foff = fi->byte_off;
        } else {
            foff = ir_struct_field_offset(rec, fidx);
        }
        if (foff < 0) {
            if (why) *why = "cannot place field in address constant";
            return -1;
        }
        out->ptr_off += foff;
        { IR_Type* ft = ice_expr_type(a, e, globals);
          out->ptr_elem = ft ? ir_type_size(ft) : 0; }
        return 0;
    }

    default:
        if (why) *why = "unsupported address constant in static assertion";
        return -1;
    }
}

/* ------------------------------------------------------------------
 *  Recursive evaluator.  Returns 0 on success; -1 on a non-constant
 *  or invalid condition, with *why (if non-NULL) set to a reason.
 * ------------------------------------------------------------------ */

int
ice_eval(Arena* a, AST_Node* e, ICEVal* out, const char** why,
         HashMap* globals)
{
    if (!e) { if (why) *why = "empty condition"; return -1; }

    out->is_float = 0;   /* every non-float path leaves this at 0 */
    out->is_ptr = 0;
    out->ptr_off = 0;
    out->ptr_elem = 0;

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

      /* address constant: &g, &garr[i], &s.f, &arr[i].f and nested
       * member/index chains rooted at a file-scope global (gcc parity).
       * A nonzero element/field offset is carried as a byte offset and
       * dumped as getelementptr (i8, ptr @name, i64 off). */
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

    case AST_BINARY:
    { TokenKind op = e->body.binary.op;
      ICEVal l, r;

      /* && / || : every operand of an ICE must be constant (no
       * side effects to skip with short-circuiting) */
      if (op == TOK_AMPAMP || op == TOK_PIPEPIPE) {
          if (ice_eval(a, e->body.binary.left, &l, why, globals)) return -1;
          if (ice_eval(a, e->body.binary.right, &r, why, globals)) return -1;
          if (l.is_float || r.is_float || l.is_ptr || r.is_ptr) {
              if (why) *why = "non-integer operand in static assertion";
              return -1;
          }
          out->v = (op == TOK_AMPAMP) ? (l.v && r.v) : (l.v || r.v);
          ice_trunc(out, 32, 0);
          return 0;
      }

      if (ice_eval(a, e->body.binary.left, &l, why, globals)) return -1;
      if (ice_eval(a, e->body.binary.right, &r, why, globals)) return -1;

      /* address-constant arithmetic: &g + k / k + &g / &g - k add k
       * pointees to the byte offset (gcc parity: &g + 2 on an int is
       * 8 bytes).  ptr - ptr and other pointer ops stay rejected. */
      if (l.is_ptr || r.is_ptr) {
          ICEVal* p = l.is_ptr ? &l : &r;
          ICEVal* k = l.is_ptr ? &r : &l;
          int ok_op = (op == TOK_PLUS) ||
                      (op == TOK_MINUS && l.is_ptr);
          if (k->is_float || k->is_ptr || p->ptr_elem <= 0 || !ok_op) {
              if (why) *why = "invalid address constant arithmetic";
              return -1;
          }
          long long delta = k->v * (long long)p->ptr_elem;
          p->ptr_off += (op == TOK_PLUS) ? delta : -delta;
          *out = *p;
          return 0;
      }

      /* C usual arithmetic conversions: a float operand makes the op
       * float; the int operand converts to the float type (computed in
       * double, converted once by the caller — used for const-init
       * values like `double g = 2 * 1.5;`).  Comparisons with a float
       * operand are still rejected: an ICE must be integer. */
      if (l.is_float || r.is_float) {
          double lf = l.is_float ? l.f : (double)l.v;
          double rf = r.is_float ? r.f : (double)r.v;
          double res;

          switch (op) {
          case TOK_PLUS:  res = lf + rf; break;
          case TOK_MINUS: res = lf - rf; break;
          case TOK_STAR:  res = lf * rf; break;
          case TOK_SLASH:
              if (rf == 0.0) {
                  if (why) *why = "division by zero in constant expression";
                  return -1;
              }
              res = lf / rf;
              break;
          default:
              if (why) *why = "non-integer operand in constant expression";
              return -1;
          }
          out->is_float = 1;
          out->f = res;
          out->v = 0;
          out->bits = 64;
          out->uns = 0;
          return 0;
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

      if (ice_eval(a, e->body.ternary.cond, &c, why, globals)) return -1;
      if (c.is_float) {
          if (why) *why = "non-integer condition in static assertion";
          return -1;
      }
      return ice_eval(a, c.v ? e->body.ternary.then_expr
                             : e->body.ternary.else_expr, out, why, globals);
    }

    case AST_CAST:
    { IR_Type* t = ir_type_from_ast(a, e->body.cast.type_expr);
      ICEVal op;

      if (ice_eval(a, e->body.cast.cast_expr, &op, why, globals)) return -1;

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
      IR_Type* t;

      /* C11 6.5.3.4p2: the operand is never evaluated — only its type
       * matters.  ice_expr_type infers the type of any typed expression
       * (globals via the file-scope table, literals, casts, binary
       * arithmetic, index, member); opt_fold has already folded
       * sizeof(5+3) to sizeof(8), so single-literal operands never
       * regress. */
      if (!op) { if (why) *why = "empty sizeof operand"; return -1; }
      t = ice_expr_type(a, op, globals);
      if (!t || t->kind == IR_VOID) {
          if (why) *why = "sizeof operand is not constant in static assertion";
          return -1;
      }
      out->v = (e->type == AST_SIZEOF_EXPR) ? ir_type_size(t)
                                            : ir_type_align(t);
      out->bits = 64;
      out->uns = 1;       /* size_t */
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
 *  false or non-constant condition (gcc parity).  The walk tracks
 *  function-scope declarations (params, block locals, for-init decls)
 *  in a frame stack, so a block-scope assert can use sizeof of a local
 *  (gcc accepts sizeof(local) in an assert; the type is known even
 *  though the value is not evaluated).
 * ------------------------------------------------------------------ */

#define SA_MAX_FRAMES 128

typedef struct { IR_Module* mod; Arena* a; TypedefEntry* enum_vals;
                 HashMap* globals; HashMap frames[SA_MAX_FRAMES];
                 int n_frames; } SACtx;

/* eval one assert: merge the local frames over the file-scope globals
 * (locals shadow globals) and run the ICE evaluator on the result. */
static void
sa_eval_assert(SACtx* c, AST_Node* n)
{
    HashMap merged;
    hashmap_init(&merged, c->a, 64);

    if (c->globals) {
        for (int i = 0; i < c->globals->cap; i++)
            if (c->globals->entries[i].used)
                hashmap_put(&merged, c->globals->entries[i].key,
                            c->globals->entries[i].value);
    }
    for (int f = c->n_frames - 1; f >= 0; f--)
        for (int i = 0; i < c->frames[f].cap; i++)
            if (c->frames[f].entries[i].used)
                hashmap_put(&merged, c->frames[f].entries[i].key,
                            c->frames[f].entries[i].value);

    const char* why = NULL;
    ICEVal val;

    /* enumerators whose value opt_enum could not fold (sizeof-based)
     * stay AST_IDENT; resolve them against enum_vals first */
    resolve_enum_idents(n->body.static_assert.expr, c->enum_vals);

    if (ice_eval(c->a, n->body.static_assert.expr, &val, &why,
                 &merged) != 0 ||
        val.is_float) {
        fprintf(stderr, "cmpl: error: static assertion at line %d col %d: %s\n",
                n->loc.line, n->loc.col,
                why ? why : "condition is not an integer constant expression");
        c->mod->had_error = 1;
        return;
    }

    if (val.v == 0) {
        fprintf(stderr, "cmpl: error: static assertion failed: \"%.*s\"\n",
                (int)n->body.static_assert.message.length,
                n->body.static_assert.message.data);
        c->mod->had_error = 1;
    }
}

static int
sa_check_cb(AST_Node* n, void* ctx)
{
    if (!n) return 0;
    SACtx* c = (SACtx*)ctx;

    if (n->type == AST_STATIC_ASSERT) {
        sa_eval_assert(c, n);
        return 0;
    }

    /* a function's parameters are in scope for the whole body */
    if (n->type == AST_FUNC_DEF) {
        if (c->n_frames < SA_MAX_FRAMES) {
            hashmap_init(&c->frames[c->n_frames], c->a, 8);
            for (AST_Node* p = n->body.func_def.params;
                 p && p->type == AST_PARAM_DECL; p = p->next)
                hashmap_put(&c->frames[c->n_frames],
                            p->body.param_decl.name,
                            p->body.param_decl.param_type);
            c->n_frames++;
        }
        return 0;
    }

    /* a block or a for statement opens a scope: block locals and
     * for-init declarations live in the new frame */
    if (n->type == AST_BLOCK || n->type == AST_FOR) {
        if (c->n_frames < SA_MAX_FRAMES) {
            hashmap_init(&c->frames[c->n_frames], c->a, 8);
            c->n_frames++;
        }
        return 0;
    }

    if (n->type == AST_VAR_DECL && c->n_frames > 0)
        hashmap_put(&c->frames[c->n_frames - 1],
                    n->body.var_decl.name, n->body.var_decl.var_type);
    return 0;
}

/* pop the frame pushed by the matching pre callback */
static int
sa_block_post_cb(AST_Node* n, void* ctx)
{
    if (n && (n->type == AST_BLOCK || n->type == AST_FOR ||
              n->type == AST_FUNC_DEF) &&
        ((SACtx*)ctx)->n_frames > 0)
        ((SACtx*)ctx)->n_frames--;
    return 0;
}

void
ir_check_static_asserts(Arena* a, IR_Module* mod, AST_Node* root,
                        TypedefEntry* enum_vals, HashMap* globals)
{
    SACtx c = { mod, a, enum_vals, globals, {{0}}, 0 };

    for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
        c.n_frames = 0;
        ast_walk(decl, sa_check_cb, sa_block_post_cb, &c);
    }
}
