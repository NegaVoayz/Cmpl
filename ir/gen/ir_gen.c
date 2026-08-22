/* ir_gen.c -- AST-to-IR walker: symbol table ops.
 *
 * The module-generation entry (ir_gen_module_ex) lives in
 * ir_gen_module.c; this file holds the local-symbol table (shadowing
 * via SymSave markers) and the module-level lookups.
 */

#include "ir.h"

#include <stdio.h>
#include <string.h>

#include "ast.h"
#include "hash.h"
#include "ir_gen.h"

IR_Value* sym_lookup(GenCtx* ctx, String name)
{
    return hashmap_get(&ctx->syms, name);
}

IR_Value* global_lookup(IR_Module* mod, String name)
{
    if (!mod) return NULL;

    for (IR_Value* g = mod->globals; g; g = g->next) {
        if (g->name.length == name.length &&
            memcmp(g->name.data, name.data, name.length) == 0)
            return g;
    }
    return NULL;
}

IR_Type* func_type_lookup(HashMap* sig_map, String name)
{
    return hashmap_get(sig_map, name);
}

/* save old value for a shadowed variable so it can be restored
 * when the inner block scope exits. */
void sym_add(GenCtx* ctx, String name, IR_Value* alloca)
{
    /* save the previous binding (if any) so shadowing can be undone */
    SymSave* save = arena_alloc(ctx->b->arena, sizeof(SymSave));
    save->name = name;
    save->old_val = hashmap_get(&ctx->syms, name);
    save->had_old = (save->old_val != NULL);
    save->next = ctx->scope_top;
    ctx->scope_top = save;

    hashmap_put(&ctx->syms, name, alloca);
}

/* mark the current scope depth; pops restore symbols added since */
void sym_scope_push(GenCtx* ctx)
{
    SymSave* marker = arena_alloc(ctx->b->arena, sizeof(SymSave));
    marker->name.data = NULL; marker->name.length = 0;
    marker->had_old = 0; marker->old_val = NULL;
    marker->next = ctx->scope_top;
    ctx->scope_top = marker;
}

/* restore symbols shadowed since the matching sym_scope_push */
void sym_scope_pop(GenCtx* ctx)
{
    while (ctx->scope_top) {
        SymSave* save = ctx->scope_top;
        ctx->scope_top = save->next;
        if (save->name.data == NULL)
            break;  /* reached the scope marker */
        if (save->had_old) {
            String nm;
            nm.data = save->name.data;
            nm.length = save->name.length;
            hashmap_put(&ctx->syms, nm, save->old_val);
        }
    }
}

/* typed VAL_UNDEF placeholder value (error paths, aggregate ternary
 * coercion).  Shared by the ir/gen/*.c files that used to build one
 * inline. */
IR_Value*
gen_undef(IR_Builder* b, IR_Type* ty)
{
    IR_Value* v = arena_alloc(b->arena, sizeof(IR_Value));
    v->kind = VAL_UNDEF;
    v->type = ty;
    return v;
}
