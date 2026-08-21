/* ll_type.c -- C type specifier parser.
 *
 * Parses specifier chains (int, unsigned long, const, ...) plus
 * qualifiers; typedef names and struct/union/enum tag specs are in
 * ll_type_tag.c.  Declarators are parsed in decl/ll_declarator.c. */

#include "ll.h"

/* ---------------------------------------------------------------
 *  Helpers
 * --------------------------------------------------------------- */

static int is_type_keyword(TokenKind k)
{
    return k == TOK_INT    || k == TOK_CHAR   || k == TOK_VOID ||
           k == TOK_SHORT  || k == TOK_LONG   || k == TOK_FLOAT ||
           k == TOK_DOUBLE || k == TOK_SIGNED || k == TOK_UNSIGNED ||
           k == TOK_BOOL   || k == TOK_COMPLEX || k == TOK_IMAGINARY;
}

static int is_qualifier(TokenKind k)
{
    return k == TOK_CONST || k == TOK_VOLATILE || k == TOK_RESTRICT ||
           k == TOK_ATOMIC;
}

static TypeKind kw_to_typekind(TokenKind k)
{
    switch (k) {
    case TOK_VOID:      return TYPE_VOID;
    case TOK_CHAR:      return TYPE_CHAR;
    case TOK_INT:       return TYPE_INT;
    case TOK_LONG:      return TYPE_LONG;
    case TOK_FLOAT:     return TYPE_FLOAT;
    case TOK_DOUBLE:    return TYPE_DOUBLE;
    case TOK_SHORT:     return TYPE_SHORT;
    case TOK_SIGNED:    return TYPE_SIGNED;
    case TOK_UNSIGNED:  return TYPE_UNSIGNED;
    case TOK_BOOL:      return TYPE_BOOL;
    default:            return TYPE_INT;
    }
}

/* skip the balanced parenthesized group whose opening '(' is the current
 * token, consuming through the matching ')'.  Stops at EOF on an
 * unterminated group.  Used for _Alignas(expr) / _Alignas(type-name). */
static void
skip_paren_group(LR1_Parser* p)
{
    int depth = 0;

    while (p->tok->kind != TOK_EOF) {
        if (p->tok->kind == TOK_LPAREN)
            depth++;
        else if (p->tok->kind == TOK_RPAREN)
            depth--;

        p->tok = p->tok->next;

        if (depth == 0)
            break;
    }
}

/* append a specifier Type to the multi-word specifier chain (linked via
 * ->next), creating the chain head on the first append. */
static void
append_spec(Type** head, Type** tail, Type* t)
{
    if (!*head)
        *head = *tail = t;
    else {
        (*tail)->next = t;
        *tail = t;
    }
}

/* consume one qualifier or hint token (const/volatile/restrict/_Atomic/
 * _Alignas/_Complex/_Imaginary/inline/_Noreturn) and return 1: qualifiers
 * and hints just advance, a parenthesized _Atomic appends its inner type,
 * _Complex/_Imaginary append a double unless a float/double follows.
 * Returns 0 when the current token is a plain type keyword (the caller
 * appends its specifier). */
static int
parse_qualifier_or_spec(LR1_Parser* p, int* is_const, int* is_volatile,
                        Type** head, Type** tail)
{
    if (p->tok->kind == TOK_CONST) {
        *is_const = 1;
        p->tok = p->tok->next;
        return 1;
    }

    if (p->tok->kind == TOK_VOLATILE) {
        *is_volatile = 1;
        p->tok = p->tok->next;
        return 1;
    }

    /* restrict is a type-qualifier (C99 6.7.3); accept and ignore —
     * the IR has no aliasing model, so it is semantically inert. */
    if (p->tok->kind == TOK_RESTRICT) {
        p->tok = p->tok->next;
        return 1;
    }

    /* _Atomic — C11 6.7.2.4: either a type-qualifier (_Atomic int) or a
     * parenthesized type-name (_Atomic(int)).  Accept and ignore — the IR
     * has no atomics, so the type is just its inner type. */
    if (p->tok->kind == TOK_ATOMIC) {
        p->tok = p->tok->next;

        if (p->tok->kind == TOK_LPAREN) {
            p->tok = p->tok->next;

            Type* inner = ll_parse_type_name(p);

            if (inner)
                append_spec(head, tail, inner);

            if (p->tok->kind == TOK_RPAREN)
                p->tok = p->tok->next;
        }
        return 1;
    }

    /* _Alignas — C11 6.7.5 alignment-specifier: _Alignas(type-name) or
     * _Alignas(constant-expression).  Accept and ignore — the IR uses
     * natural alignment, so the requested alignment is a hint we drop. */
    if (p->tok->kind == TOK_ALIGNAS) {
        p->tok = p->tok->next;

        if (p->tok->kind == TOK_LPAREN)
            skip_paren_group(p);
        return 1;
    }

    /* _Complex / _Imaginary — C11 6.7.2p2: complex/imaginary variant of a
     * real floating type.  The IR has no complex arithmetic, so map to the
     * underlying float/double: _Complex double -> double.  A bare _Complex
     * (no following float/double) means _Complex double. */
    if (p->tok->kind == TOK_COMPLEX || p->tok->kind == TOK_IMAGINARY) {
        TokenKind nk = p->tok->next->kind;

        p->tok = p->tok->next;

        if (nk != TOK_FLOAT && nk != TOK_DOUBLE) {
            Type* t = type_new(p->arena, TYPE_DOUBLE);
            append_spec(head, tail, t);
        }
        return 1;
    }

    /* inline / _Noreturn may legally interleave with type specifiers
     * (C99 6.7: declaration-specifiers in any order); accept and
     * ignore — they are hints, not part of the type. */
    if (p->tok->kind == TOK_INLINE || p->tok->kind == TOK_NORETURN) {
        p->tok = p->tok->next;
        return 1;
    }

    return 0;
}

/* ---------------------------------------------------------------
 *  Type specifiers
 *
 *  Consumes a chain of type keywords (int, unsigned long, ...)
 *  plus qualifiers (const, volatile).  Returns a Type tree where
 *  multi-word specifiers are linked via ->next.
 * --------------------------------------------------------------- */

Type* ll_parse_type_specs(LR1_Parser* p)
{
    Type* head = NULL;
    Type* tail = NULL;
    int   is_const = 0;
    int   is_volatile = 0;

    while (is_type_keyword(p->tok->kind) || is_qualifier(p->tok->kind) ||
           p->tok->kind == TOK_INLINE || p->tok->kind == TOK_NORETURN ||
           p->tok->kind == TOK_ALIGNAS) {
        if (parse_qualifier_or_spec(p, &is_const, &is_volatile, &head, &tail))
            continue;

        /* a plain type keyword: append its specifier to the chain */
        Type* t = type_new(p->arena, kw_to_typekind(p->tok->kind));
        append_spec(&head, &tail, t);
        p->tok = p->tok->next;
    }

    /* no specifier keyword matched: a typedef name, struct/union tag
     * reference or inline definition, or an enum tag reference */
    if (!head) {
        Type* t = ll_parse_tag_spec(p);
        if (t) head = t;
    }

    if (!head)
        return NULL;

    head->is_const = is_const;
    head->is_volatile = is_volatile;

    return head;
}
