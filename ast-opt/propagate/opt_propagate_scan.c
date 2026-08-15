/* opt_propagate_scan.c -- scan passes for constant propagation (phase 1) */

#include "../optimize.h"
#include "../ast_walk.h"

#include <string.h>

typedef struct { String name; long long value; int is_unsigned; int is_long; int active; } ConstEntry;

/* context passed through walker */
typedef struct {
    ConstEntry* map;
    int*       count;
} ScanCtx;

/* from opt_propagate.c */
extern void add_entry(ConstEntry* map, int* count, String name, long long value,
                      int is_unsigned, int is_long);
extern void kill_entry(ConstEntry* map, int count, String name);

/* ---------------------------------------------------------------
 *  Phase 1a: scan for constants and assignments (pre-order)
 * --------------------------------------------------------------- */

static int scan_pre(AST_Node* n, void* ctx)
{
    ScanCtx* c = (ScanCtx*)ctx;

    switch (n->type) {
    case AST_VAR_DECL:
        if (n->body.var_decl.init
            && is_int_literal_kind(n->body.var_decl.init->type))
            add_entry(c->map, c->count, n->body.var_decl.name,
                      n->body.var_decl.init->body.literal.int_val,
                      n->body.var_decl.init->body.literal.is_unsigned,
                      n->body.var_decl.init->type == AST_LONG_LIT);
        break;

    case AST_BINARY:
        if (n->body.binary.op >= TOK_EQ && n->body.binary.op <= TOK_GTGTEQ
            && n->body.binary.left
            && n->body.binary.left->type == AST_IDENT)
            kill_entry(c->map, *c->count, n->body.binary.left->body.ident.name);
        break;

    default: break;
    }

    return 0;
}

void scan_node(AST_Node* n, ConstEntry* map, int* count)
{
    ScanCtx ctx = {map, count};

    ast_walk(n, scan_pre, NULL, &ctx);
}

/* ---------------------------------------------------------------
 *  Phase 1b: invalidate inc/dec and address-of (pre-order)
 * --------------------------------------------------------------- */

static int invalidate_pre(AST_Node* n, void* ctx)
{
    ScanCtx* c = (ScanCtx*)ctx;

    /* inc/dec on tracked names */
    if (n->type == AST_UNARY || n->type == AST_POSTFIX) {
        TokenKind op = (n->type == AST_UNARY)
            ? n->body.unary.op : n->body.postfix.op;

        if (op == TOK_PLUSPLUS || op == TOK_MINUSMINUS) {
            AST_Node* operand = (n->type == AST_UNARY)
                ? n->body.unary.operand : n->body.postfix.operand;

            if (operand && operand->type == AST_IDENT)
                kill_entry(c->map, *c->count, operand->body.ident.name);
        }
    }

    /* address-of kills constant-ness */
    if (n->type == AST_UNARY && n->body.unary.op == TOK_AMP) {
        AST_Node* operand = n->body.unary.operand;
        if (operand && operand->type == AST_IDENT)
            kill_entry(c->map, *c->count, operand->body.ident.name);
    }

    return 0;
}

void scan_invalidate(AST_Node* n, ConstEntry* map, int count)
{
    ScanCtx ctx = {map, &count};

    ast_walk(n, invalidate_pre, NULL, &ctx);
}
