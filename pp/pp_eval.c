#include "pp.h"

#include <ctype.h>
#include <stdlib.h>

typedef enum {
    TK_INT, TK_PLUS, TK_MINUS, TK_STAR, TK_SLASH, TK_PERCENT,
    TK_EQEQ, TK_BANGEQ, TK_LT, TK_GT, TK_LTEQ, TK_GTEQ,
    TK_AMPAMP, TK_PIPEPIPE, TK_BANG, TK_TILDE,
    TK_LTLT, TK_GTGT, TK_AMP, TK_PIPE, TK_CARET,
    TK_LPAREN, TK_RPAREN, TK_QUESTION, TK_COLON, TK_EOF, TK_ERROR
} TK;

typedef struct { TK kind; long val; const char* p; const char* end; } Lex;

static void
next(Lex* l)
{
    while (l->p < l->end && (*l->p == ' ' || *l->p == '\t')) l->p++;
    if (l->p >= l->end) { l->kind = TK_EOF; return; }

    char c = *l->p;

    if (isdigit((unsigned char)c)) {
        char* ep;
        l->val = strtol(l->p, &ep, 0);
        l->kind = TK_INT; l->p = ep; return;
    }
    if (isalpha((unsigned char)c) || c == '_') {
        while (l->p < l->end && (isalnum((unsigned char)*l->p) || *l->p == '_'))
            l->p++;
        l->kind = TK_INT; l->val = 0; return;
    }

    l->p++;
    switch (c) {
    case '+': l->kind = TK_PLUS; break;
    case '-': l->kind = TK_MINUS; break;
    case '*': l->kind = TK_STAR; break;
    case '/': l->kind = TK_SLASH; break;
    case '%': l->kind = TK_PERCENT; break;
    case '!':
        l->kind = (*l->p == '=') ? (l->p++, TK_BANGEQ) : TK_BANG; break;
    case '=':
        l->kind = (*l->p == '=') ? (l->p++, TK_EQEQ) : TK_ERROR; break;
    case '<':
        if (*l->p == '=')      { l->p++; l->kind = TK_LTEQ; }
        else if (*l->p == '<') { l->p++; l->kind = TK_LTLT; }
        else                     l->kind = TK_LT;
        break;
    case '>':
        if (*l->p == '=')      { l->p++; l->kind = TK_GTEQ; }
        else if (*l->p == '>') { l->p++; l->kind = TK_GTGT; }
        else                     l->kind = TK_GT;
        break;
    case '&':
        l->kind = (*l->p == '&') ? (l->p++, TK_AMPAMP) : TK_AMP; break;
    case '|':
        l->kind = (*l->p == '|') ? (l->p++, TK_PIPEPIPE) : TK_PIPE; break;
    case '^': l->kind = TK_CARET; break;
    case '~': l->kind = TK_TILDE; break;
    case '(': l->kind = TK_LPAREN; break;
    case ')': l->kind = TK_RPAREN; break;
    case '?': l->kind = TK_QUESTION; break;
    case ':': l->kind = TK_COLON; break;
    default:  l->kind = TK_ERROR; break;
    }
}

typedef enum { PREC_NONE, PREC_LOGOR, PREC_LOGAND, PREC_OR, PREC_XOR,
               PREC_AND, PREC_EQ, PREC_REL, PREC_SHIFT, PREC_ADD,
               PREC_MUL, PREC_UNARY } Prec;

static Prec
binop_prec(TK kind)
{
    switch (kind) {
    case TK_PIPEPIPE: return PREC_LOGOR;
    case TK_AMPAMP:   return PREC_LOGAND;
    case TK_PIPE:     return PREC_OR;
    case TK_CARET:    return PREC_XOR;
    case TK_AMP:      return PREC_AND;
    case TK_EQEQ: case TK_BANGEQ: return PREC_EQ;
    case TK_LT: case TK_GT: case TK_LTEQ: case TK_GTEQ: return PREC_REL;
    case TK_LTLT: case TK_GTGT: return PREC_SHIFT;
    case TK_PLUS: case TK_MINUS: return PREC_ADD;
    case TK_STAR: case TK_SLASH: case TK_PERCENT: return PREC_MUL;
    default: return PREC_NONE;
    }
}

static long
apply_binop(TK op, long a, long b)
{
    switch (op) {
    case TK_PIPEPIPE: return a || b;
    case TK_AMPAMP:   return a && b;
    case TK_PIPE:     return a | b;
    case TK_CARET:    return a ^ b;
    case TK_AMP:      return a & b;
    case TK_EQEQ:     return a == b;
    case TK_BANGEQ:   return a != b;
    case TK_LT:       return a < b;
    case TK_GT:       return a > b;
    case TK_LTEQ:     return a <= b;
    case TK_GTEQ:     return a >= b;
    case TK_LTLT:     return a << b;
    case TK_GTGT:     return a >> b;
    case TK_PLUS:     return a + b;
    case TK_MINUS:    return a - b;
    case TK_STAR:     return a * b;
    case TK_SLASH:    return b ? a / b : 0;
    case TK_PERCENT:  return b ? a % b : 0;
    default:          return 0;
    }
}

static int parse_prefix(Lex* l, long* result);

static int
parse_expr(Lex* l, long* result, Prec min_prec)
{
    if (parse_prefix(l, result) != 0) return -1;

    while (1) {
        TK   op = l->kind;
        Prec p = binop_prec(op);

        if (p == PREC_NONE || p < min_prec) break;

        next(l);

        long rhs;
        if (parse_expr(l, &rhs, (int)p + 1) != 0) return -1;
        *result = apply_binop(op, *result, rhs);
    }

    if (l->kind == TK_QUESTION) {
        long tval, fval;
        next(l);
        if (parse_expr(l, &tval, PREC_NONE) != 0) return -1;
        if (l->kind != TK_COLON) return -1;
        next(l);
        if (parse_expr(l, &fval, PREC_NONE) != 0) return -1;
        *result = *result ? tval : fval;
    }

    return 0;
}

static int
parse_prefix(Lex* l, long* result)
{
    if (l->kind == TK_INT) {
        *result = l->val; next(l); return 0;
    }
    if (l->kind == TK_LPAREN) {
        next(l);
        if (parse_expr(l, result, PREC_NONE) != 0) return -1;
        if (l->kind != TK_RPAREN) return -1;
        next(l); return 0;
    }
    if (l->kind == TK_PLUS)  { next(l); return parse_prefix(l, result); }
    if (l->kind == TK_MINUS) {
        next(l);
        if (parse_prefix(l, result) != 0) return -1;
        *result = -*result; return 0;
    }
    if (l->kind == TK_BANG) {
        next(l);
        if (parse_prefix(l, result) != 0) return -1;
        *result = !*result; return 0;
    }
    if (l->kind == TK_TILDE) {
        next(l);
        if (parse_prefix(l, result) != 0) return -1;
        *result = ~*result; return 0;
    }
    return -1;
}

int
expr_eval(const char* src, const char* end, long* result)
{
    Lex l;

    l.p = src; l.end = end;
    next(&l);

    if (l.kind == TK_EOF) { *result = 0; return 0; }
    if (parse_expr(&l, result, PREC_NONE) != 0) return -1;
    if (l.kind != TK_EOF) return -1;

    return 0;
}
