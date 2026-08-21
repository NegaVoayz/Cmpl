/* lr1_cast.c -- cast / sizeof(type) / compound-literal detection: the
 * lookahead heuristics that decide whether '(' starts a cast (rather than a
 * paren-expr or call) and the type-name parser they share. */

#include "lr1.h"
#include "../ll/ll.h"

/* type keywords that can begin a cast/sizeof type */
static int is_type_keyword(TokenKind k)
{
    return k == TOK_INT    || k == TOK_CHAR   || k == TOK_VOID ||
           k == TOK_SHORT  || k == TOK_LONG   || k == TOK_FLOAT ||
           k == TOK_DOUBLE || k == TOK_SIGNED || k == TOK_UNSIGNED ||
           k == TOK_BOOL   || k == TOK_STRUCT || k == TOK_UNION  || k == TOK_ENUM ||
           k == TOK_ATOMIC || k == TOK_COMPLEX || k == TOK_IMAGINARY;
}

/* skip const/volatile qualifiers (they only appear in types, never at the
 * start of a parenthesised expression) */
static Token*
skip_qualifiers(Token* t)
{
    while (t && (t->kind == TOK_CONST || t->kind == TOK_VOLATILE))
        t = t->next;
    return t;
}

/* a token that can start the expression a cast applies to */
static int
is_cast_target_start(TokenKind k)
{
    return k == TOK_IDENT || k == TOK_INT_LIT || k == TOK_LONG_LIT ||
           k == TOK_FLOAT_LIT || k == TOK_DOUBLE_LIT || k == TOK_CHAR_LIT ||
           k == TOK_STRING_LIT || k == TOK_LPAREN || k == TOK_PLUSPLUS ||
           k == TOK_MINUSMINUS || k == TOK_BANG || k == TOK_TILDE ||
           k == TOK_SIZEOF || k == TOK_LBRACE ||
           /* unary ops: (T)&x, (T)*p, (T)-x, (T)+x — ambiguous with
            * (a) & b / (a) * b / (a) - b / (a) + b, resolved by the
            * typedef gate in try_parse_cast */
           k == TOK_AMP || k == TOK_STAR || k == TOK_MINUS || k == TOK_PLUS;
}

/* (TypeName*) is a cast ONLY if the type ends at ')'.
 * (ident * ident) is a parenthesized multiply — treating it
 * as a cast broke every `(a * b)` expression.  Scan past
 * the star chain and array dimensions: (T*)x, (T**)x and
 * (T*[2])x end with ')', (a * b) does not. */
static int
ident_star_cast(Token* s)
{
    while (s && (s->kind == TOK_STAR ||
                 s->kind == TOK_CONST ||
                 s->kind == TOK_VOLATILE))
        s = s->next;
    while (s && s->kind == TOK_LBRACKET) {
        int depth = 1;

        s = s->next;
        while (s && depth > 0) {
            if (s->kind == TOK_LBRACKET) depth++;
            if (s->kind == TOK_RBRACKET) depth--;
            if (depth > 0) s = s->next;
        }
        if (s && s->kind == TOK_RBRACKET) s = s->next;
    }
    return s && s->kind == TOK_RPAREN;
}

/* (TypeName) — find closing ) and peek at what follows */
static int
paren_expr_looks_like_cast(Token* after)
{
    return after && is_cast_target_start(after->kind);
}

/* typedef name: (TypeName*) or (TypeName **) is a cast;
 * (TypeName) without * is ambiguous — check if what follows
 * ')' looks like a cast target (expr start). */
static int
ident_cast_lookahead(Token* tok)
{
    Token* next = skip_qualifiers(tok->next);

    if (next && next->kind == TOK_STAR)
        return ident_star_cast(next);

    if (next && next->kind == TOK_RPAREN)
        return paren_expr_looks_like_cast(next->next);

    return 0;
}

/* Check if a token can start a type specifier inside a cast:
 *   (type_keyword...)  e.g. (int*), (unsigned long)
 *   (const ...)        e.g. (const int*), (const Keyword*)
 *   (volatile ...)     e.g. (volatile int*)
 *   (IDENT *)          e.g. (Keyword*)  -- typedef name with pointer
 *   (IDENT)            e.g. (Keyword)   -- usable as a cast when
 *                        followed by a unary expression (heuristic).
 * Only called when the next token after '(' needs disambiguation. */
int
is_cast_start(Token* tok)
{
    if (!tok) return 0;

    TokenKind k = tok->kind;

    /* type keywords and struct/union/enum -- always a cast */
    if (is_type_keyword(k)) return 1;

    /* const / volatile -- qualifiers only appear in types, never
     * at the start of a parenthesised expression. */
    if (k == TOK_CONST || k == TOK_VOLATILE) return 1;

    if (k == TOK_IDENT)
        return ident_cast_lookahead(tok);

    return 0;
}

/* check if token kind is a postfix operator (binds tighter than cast) */
int is_postfix_token(TokenKind k)
{
    return k == TOK_LPAREN    /* func(args) */
        || k == TOK_LBRACKET  /* arr[idx]   */
        || k == TOK_DOT       /* obj.member */
        || k == TOK_ARROW     /* ptr->member*/
        || k == TOK_PLUSPLUS  /* expr++     */
        || k == TOK_MINUSMINUS;/* expr--     */
}

/* states at or below cast-expr level -- where a pending cast should wrap */
int is_cast_level(int s)
{
    /* HS_POSTFIX / S_BINRHS_POSTFIX are deliberately excluded —
     * postfix operators bind tighter than casts.  If we wrap at
     * HS_POSTFIX, (unsigned char)key.data[i] becomes
     * ((unsigned char)key.data)[i] instead of the correct
     * (unsigned char)(key.data[i]). */
    return s == HS_UNARY ||
           s == HS_CAST_EXPR ||
           s == S_BINRHS_UNARY;
}

/* states where '(' starts a function call, not a cast/paren-expr */
int is_have_expr_state(LR1_State s)
{
    return (s >= HS_PRIMARY && s <= HS_EXPR) ||
           s == S_BINRHS_PRIMARY || s == S_BINRHS_POSTFIX ||
           s == S_BINRHS_UNARY ||
           s == S_UNARY_RHS || s == S_ASSIGN_RHS || s == S_TERNARY_RHS;
}

/* Parse a type name inside a cast or sizeof: type specifiers followed by
 * pointer/array declarator suffixes.  Leaves p->tok just past the type
 * (before the closing ')') and returns the wrapped Type tree. */
Type* ll_parse_type_name(LR1_Parser* p)
{
    Type* ct = ll_parse_type_specs(p);

    /* consume pointer declarator: (int*), (void**), etc. */
    while (p->tok->kind == TOK_STAR ||
           p->tok->kind == TOK_CONST ||
           p->tok->kind == TOK_VOLATILE) {
        if (p->tok->kind == TOK_STAR) {
            Type* ptr = type_new(p->arena, TYPE_PTR);
            ptr->inner = ct;
            ct = ptr;
        }
        p->tok = p->tok->next;
    }

    /* consume array declarator: (int[]), (int[N]), (IR_Value*[]), etc. */
    while (p->tok->kind == TOK_LBRACKET) {
        p->tok = p->tok->next;

        Type* arr = type_new(p->arena, TYPE_ARRAY);
        arr->arr_size = 0;

        if (p->tok->kind == TOK_INT_LIT) {
            arr->arr_size = (int)p->tok->body.int_val;
            p->tok = p->tok->next;
        } else if (p->tok->kind == TOK_IDENT) {
            arr->size_name = p->tok->body.ident;
            p->tok = p->tok->next;
        }

        if (p->tok->kind == TOK_RBRACKET)
            p->tok = p->tok->next;

        arr->inner = ct;
        ct = arr;
    }

    /* abstract function-pointer declarator: (int (*)(int,int)) — the
     * declarator parser tolerates a missing name, so route any `(`
     * through it; (*)(args) yields FUNC->PTR->base like (*f)(args) */
    if (p->tok->kind == TOK_LPAREN) {
        String anon = {NULL, 0};
        ct = ll_parse_declarator(p, ct, &anon, 0);
    }

    return ct;
}
