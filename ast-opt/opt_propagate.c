/* opt_propagate.c -- constant propagation: scan, invalidate, replace in-place */

#include "optimize.h"

#include <string.h>

#define MAX_CONSTANTS 32

typedef struct { String name; long value; int is_unsigned; int active; } ConstEntry;

/* from sub-files */
extern void scan_node(AST_Node* n, ConstEntry* map, int* count);
extern void scan_invalidate(AST_Node* n, ConstEntry* map, int count);
extern int  replace_node(AST_Node* n, ConstEntry* map, int count);

/* ---------------------------------------------------------------
 *  String compare
 * --------------------------------------------------------------- */

static int str_eq(String a, String b)
{
    if (a.length != b.length) return 0;
    return memcmp(a.data, b.data, a.length) == 0;
}

/* ---------------------------------------------------------------
 *  ConstEntry map helpers
 * --------------------------------------------------------------- */

ConstEntry* find_entry(ConstEntry* map, int count, String name)
{
    for (int i = 0; i < count; i++)
        if (map[i].active && str_eq(map[i].name, name))
            return &map[i];
    return NULL;
}

void add_entry(ConstEntry* map, int* count, String name, long value,
               int is_unsigned)
{
    if (*count >= MAX_CONSTANTS) return;
    map[*count].name = name;
    map[*count].value = value;
    map[*count].is_unsigned = is_unsigned;
    map[*count].active = 1;
    (*count)++;
}

void kill_entry(ConstEntry* map, int count, String name)
{
    ConstEntry* e = find_entry(map, count, name);
    if (e) e->active = 0;
}

/* ---------------------------------------------------------------
 *  Process one function definition
 * --------------------------------------------------------------- */

static int propagate_func(AST_Node* func)
{
    AST_Node* body = func->body.func_def.body;
    if (!body) return 0;

    ConstEntry map[MAX_CONSTANTS];
    int count = 0;

    scan_node(body, map, &count);
    if (count == 0) return 0;

    scan_invalidate(body, map, count);
    return replace_node(body, map, count);
}

/* ---------------------------------------------------------------
 *  Recursive walker -- finds functions and processes them
 * --------------------------------------------------------------- */

static int walk_and_propagate(AST_Node* n)
{
    int changed = 0;
    if (!n) return 0;

    if (n->type == AST_FUNC_DEF)
        changed |= propagate_func(n);

    switch (n->type) {
    case AST_BLOCK:
        changed |= walk_and_propagate(n->body.block.stmts); break;
    case AST_PROGRAM:
        changed |= walk_and_propagate(n->body.program.decls); break;
    case AST_IF:
        changed |= walk_and_propagate(n->body.if_stmt.then_branch);
        if (n->body.if_stmt.else_branch)
            changed |= walk_and_propagate(n->body.if_stmt.else_branch);
        break;
    default: break;
    }

    if (n->next) changed |= walk_and_propagate(n->next);
    return changed;
}

/* ---------------------------------------------------------------
 *  Public entry
 * --------------------------------------------------------------- */

int opt_propagate(AST_Node* root)
{
    return walk_and_propagate(root);
}
