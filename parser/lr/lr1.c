/* lr1.c -- LR(1) expression parser lifecycle + main loop.  Cast / sizeof /
 * compound-literal handling lives in lr1_cast.c and lr1_cast_apply.c. */

#include "lr1.h"
#include "arena.h"
#include "ast_walk.h"
#include "../ll/ll.h"

#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------
 *  Typedef-name registry
 *
 *  Names introduced by `typedef` are recorded as they are parsed, so
 *  the cast lookahead can tell (size_t)(x) [cast] from (f)(x) [call].
 *  No scope-exit removal: a leaked local typedef name may over-classify
 *  a later (name)( — same permissiveness the existing type heuristics
 *  already have.
 * --------------------------------------------------------------- */

struct TypedefName {
    String name;
    struct TypedefName* next;
};

void
parser_add_typedef(LR1_Parser* p, String name)
{
    TypedefName* tn = arena_alloc(p->arena, sizeof(TypedefName));

    tn->name = name;
    tn->next = p->typedefs;
    p->typedefs = tn;
}

int
parser_is_typedef(LR1_Parser* p, String name)
{
    for (TypedefName* tn = p->typedefs; tn; tn = tn->next)
        if (tn->name.length == name.length &&
            memcmp(tn->name.data, name.data, name.length) == 0)
            return 1;
    return 0;
}

LR1_Parser* lr1_parser_new(Token* first_tok, Arena* a)
{
    LR1_Parser* p = arena_alloc(a, sizeof(LR1_Parser));

    p->tok = first_tok;
    p->sp = 0;
    p->error = 0;
    p->pending_cast = 0;
    p->cast_count = 0;
    p->cast_chain = NULL;
    p->cast_sp = 0;
    p->typedefs = NULL;
    p->arena = a;
    p->stack[0].state = S_ENTRY;
    p->stack[0].token = NULL;
    p->stack[0].node = NULL;

    lr1_table_init();

    return p;
}

void goto_push(LR1_Parser* p, AST_Node* node, int lhs_sym)
{
    int target = goto_table[p->stack[p->sp].state][lhs_sym];

    if (p->sp + 1 >= MAX_STACK) {
        fprintf(stderr, "lr1: stack overflow\n");
        p->error = 1;
        return;
    }

    p->sp++;
    p->stack[p->sp].state = target;
    p->stack[p->sp].token = NULL;
    p->stack[p->sp].node = node;
}

void goto_passthru(LR1_Parser* p, int lhs_sym)
{
    /* For passthrough reductions (pop 1, push same node with new sym).
     * Instead of pop+push, just update the state in-place. */
    int target = goto_table[p->stack[p->sp - 1].state][lhs_sym];

    p->stack[p->sp].state = target;
}

static AST_Node* lr1_parse_expr_inner(LR1_Parser* p)
{
    /* reset for a fresh expression */
    p->sp = 0;
    p->stack[0].state = S_ENTRY;
    p->stack[0].token = NULL;
    p->stack[0].node = NULL;
    p->pending_cast = 0;
    p->cast_count = 0;
    p->cast_chain = NULL;
    p->paren_depth = 0;
    p->cast_sp = 0;

    while (1) {
        TokenKind next = p->tok->kind;
        LR1_State state = (LR1_State)p->stack[p->sp].state;
        LR1_Func  func = action_table[state][next];

        AST_Node* stopped = lr1_stop_at_comma(p, next);
        if (stopped)
            return stopped;

        /* Cast / sizeof(type) / compound literal at '(' */
        if (try_parse_cast(p, state))
            continue;

        /* _Generic(...) — the whole selection is parsed wholesale at
         * its '(' so the type-name colons never reach the LR loop */
        if (state == S_GENERIC && p->tok->kind == TOK_LPAREN) {
            if (!lr1_parse_generic(p)) {
                p->error = 1;
                return NULL;
            }
            continue;
        }

        LR_Action action = func(p);

        switch (action) {
        case LR_ACCEPT: {
            AST_Node* result = p->stack[p->sp].node;

            if (p->pending_cast && result)
                return apply_pending_casts(p, result);

            return result;
        }

        case LR_SHIFT:
            continue;

        case LR_REDUCE:
            apply_pending_cast_at_reduce(p);
            continue;

        case LR_ERROR:
            fprintf(stderr, "lr1: syntax error at line %d col %d, token %d\n",
                    p->tok->loc.line, p->tok->loc.col, next);
            p->error = 1;
            return NULL;
        }
    }
}

/* ---------------------------------------------------------------
 *  Compound-literal initializers (deferred parse)
 *
 *  The LR loop skips (type){...} and records the '{' token because
 *  parse_init_list() re-enters lr1_parse_expr() (per element), which
 *  resets the LR stack.  Once the outer LR expression is complete the
 *  stack no longer matters, so we replay the initializer here.
 * --------------------------------------------------------------- */

static int resolve_compound_lit_cb(AST_Node* n, void* ctx)
{
    if (n && n->type == AST_COMPOUND_LIT &&
        !n->body.compound_lit.init &&
        n->body.compound_lit.init_start) {
        LR1_Parser* p = ctx;
        Token* save = p->tok;

        p->tok = n->body.compound_lit.init_start;
        n->body.compound_lit.init = parse_init_list(p);
        p->tok = save;
    }
    return 0;
}

static void resolve_compound_lits(AST_Node* root, LR1_Parser* p)
{
    if (p->error || !root) return;

    ast_walk(root, resolve_compound_lit_cb, NULL, p);
}

AST_Node* lr1_parse_expr(LR1_Parser* p)
{
    AST_Node* result = lr1_parse_expr_inner(p);

    resolve_compound_lits(result, p);
    return result;
}
