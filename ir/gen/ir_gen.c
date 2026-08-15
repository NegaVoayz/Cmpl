/* ir_gen.c -- AST-to-IR walker: symbol table, function/module generation */

#include "ir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "hash.h"
#include "ast_walk.h"
#include "ir_gen.h"

/* ---------------------------------------------------------------
 *  Symbol table ops
 * --------------------------------------------------------------- */

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

/* ---------------------------------------------------------------
 *  Module generation (top-level entry)
 * --------------------------------------------------------------- */

IR_Module*
ir_gen_module_ex(AST_Node* root, int is_device)
{
    if (!root || root->type != AST_PROGRAM)
        return NULL;

    Arena*     a = arena_new();
    IR_Module* mod = arena_alloc(a, sizeof(IR_Module));
    mod->arena = a;
    mod->addr_space = is_device ? 1 : 0;
    mod->target_triple = is_device ? "spir64-unknown-unknown"
                                   : "x86_64-unknown-linux-gnu";
    mod->data_layout = "e-i64:64-v16:16-v24:32-v32:32-v48:64-v96:128-v192:256-v256:256-v512:512-v1024:1024";

    /* reset static caches so no IR_Type* from a previous module's arena
     * leaks into this one (critical for CUDA host→device dual gen) */
    ir_reset_type_caches();

    /* pass 0.5: collect typedefs + enum constants, resolve throughout AST */
    TypedefEntry *typedefs = NULL, *enum_vals = NULL;
    {

        /* collect typedefs */
        for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
            if (decl->type != AST_TYPEDEF) continue;
            TypedefEntry* te = arena_alloc(a, sizeof(TypedefEntry));
            te->name = decl->body.typedef_decl.name;
            te->aliased_type = decl->body.typedef_decl.aliased_type;
            te->next = typedefs; typedefs = te;
        }

        /* update opaque typedefs from struct/union definitions */
        for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
            if (decl->type != AST_STRUCT_DEF && decl->type != AST_UNION_DEF) continue;
            if (!decl->body.struct_def.name.data || !decl->body.struct_def.fields) continue;
            for (TypedefEntry* te = typedefs; te; te = te->next) {
                if (!te->aliased_type) continue;
                if (te->aliased_type->kind != TYPE_STRUCT &&
                    te->aliased_type->kind != TYPE_UNION) continue;
                if (te->aliased_type->name.length != decl->body.struct_def.name.length) continue;
                if (memcmp(te->aliased_type->name.data, decl->body.struct_def.name.data,
                           te->aliased_type->name.length) != 0) continue;
                te->aliased_type->params = decl->body.struct_def.fields; break;
            }
        }

        /* build enum constant table */
        for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
            if (decl->type != AST_ENUM_DEF) continue;
            int val = 0;
            for (AST_Node* en = decl->body.enum_def.enumerators;
                 en && en->type == AST_ENUMERATOR; en = en->next) {
                if (en->body.enumerator.value &&
                    en->body.enumerator.value->type == AST_INT_LIT)
                    val = (int)en->body.enumerator.value->body.literal.int_val;
                TypedefEntry* ev = arena_alloc(a, sizeof(TypedefEntry));
                ev->name = en->body.enumerator.name;
                ev->aliased_type = (Type*)(intptr_t)val;
                ev->next = enum_vals; enum_vals = ev;
                val++;
            }
        }

        /* resolve typedefs in all decl type trees */
        for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next)
            resolve_ast_node(decl, typedefs);

        /* resolve struct references: TYPE_STRUCT with name but no params
         * needs to find the AST_STRUCT_DEF and attach fields */
        {
            HashMap struct_map;

            hashmap_init(&struct_map, a, 32);

            for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
                if (decl->type != AST_STRUCT_DEF && decl->type != AST_UNION_DEF) continue;
                if (!decl->body.struct_def.name.data) continue;
                StructDefEntry* se = arena_alloc(a, sizeof(StructDefEntry));
                se->name = decl->body.struct_def.name;
                se->fields = decl->body.struct_def.fields;
                se->is_union = (decl->type == AST_UNION_DEF);
                hashmap_put(&struct_map, se->name, se);
            }

            /* resolve TYPE_STRUCT with missing params */
            for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
                if (decl->type == AST_VAR_DECL)
                    resolve_struct_refs_type(decl->body.var_decl.var_type, &struct_map);
                else if (decl->type == AST_FUNC_DEF) {
                    resolve_struct_refs_type(decl->body.func_def.ret_type, &struct_map);
                    for (AST_Node* p = decl->body.func_def.params;
                         p && p->type == AST_PARAM_DECL; p = p->next)
                        resolve_struct_refs_type(p->body.param_decl.param_type, &struct_map);
                } else if (decl->type == AST_TYPEDEF)
                    resolve_struct_refs_type(decl->body.typedef_decl.aliased_type, &struct_map);
            }

            /* local variable declarations inside function bodies
             * (struct X v; without a typedef) */
            for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
                if (decl->type == AST_FUNC_DEF && decl->body.func_def.body) {
                    /* first register standalone local struct/union defs
                     * so refs below resolve their fields */
                    LocalDefCtx lc = { &struct_map, a };
                    ast_walk(decl->body.func_def.body,
                             collect_local_struct_def_cb, NULL, &lc);
                }
            }
            for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
                if (decl->type == AST_FUNC_DEF && decl->body.func_def.body) {
                    resolve_struct_refs_stmt(decl->body.func_def.body,
                                             &struct_map);
                    ast_walk(decl->body.func_def.body,
                             resolve_compound_lit_type_cb, NULL,
                             &struct_map);
                }
            }
        }

        /* resolve array sizes from enum constants */
        for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
            if (decl->type == AST_VAR_DECL)
                resolve_array_sizes(decl->body.var_decl.var_type, enum_vals);
            else if (decl->type == AST_FUNC_DEF) {
                resolve_array_sizes(decl->body.func_def.ret_type, enum_vals);
                for (AST_Node* p = decl->body.func_def.params;
                     p && p->type == AST_PARAM_DECL; p = p->next)
                    resolve_array_sizes(p->body.param_decl.param_type, enum_vals);
            }
        }
    }

    /* enable struct type dedup cache — typedefs are now resolved,
     * so subsequent ir_type_from_ast() calls get consistent IR_Type* */
    ir_clear_struct_cache();

    /* collect function signatures for call return type + param type lookup.
     * MUST run after typedef resolution so struct return types resolve.
     * Stores IR_FUNC type (ret type + param list) for each function. */
    HashMap sig_map;

    hashmap_init(&sig_map, a, 64);
    for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
        if (decl->type != AST_FUNC_DEF) continue;

        IR_Type* rt = ir_type_from_ast(a, decl->body.func_def.ret_type);

        /* collect parameter types with TYPE_NAMED→ptr fixup.
         * if a param type is unresolved typedef → i32 fallback,
         * default to ptr — same fixup as ir_gen_function lines 366-374. */
        IR_Type *params = NULL, **ptail = &params;

        for (AST_Node* p = decl->body.func_def.params;
             p && p->type == AST_PARAM_DECL; p = p->next) {
            IR_Type* pty = ir_type_from_ast(a, p->body.param_decl.param_type);

            if (pty && pty->kind == IR_I32) {
                Type* ast = p->body.param_decl.param_type;
                if (ast && ast->kind == TYPE_NAMED && !ast->inner)
                    pty = ir_ptr_type(a, t_i8, 0);
            }
            /* clone to avoid corrupting singletons when chaining */
            IR_Type* cp = arena_alloc(a, sizeof(IR_Type));
            memcpy(cp, pty ? pty : t_i32, sizeof(IR_Type));
            cp->next = NULL;
            *ptail = cp;
            ptail = &cp->next;
        }

        IR_Type* func_ty = ir_func_type(a, rt ? rt : t_void, params,
                                         decl->body.func_def.is_variadic);
        hashmap_put(&sig_map, decl->body.func_def.name, func_ty);
    }

    /* first pass: collect global variables (both extern decls and definitions) */
    {
        HashMap global_map;

        hashmap_init(&global_map, a, 64);

        for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
            if (decl->type != AST_VAR_DECL) continue;

            /* dedup by name using HashMap (O(1) vs O(n) list scan) */
            IR_Value* existing = hashmap_get(&global_map,
                                             decl->body.var_decl.name);
            if (existing) {
                /* upgrade extern → definition if init available
                 * or tentative definition (linkage==0, no extern keyword) */
                if (!existing->body.init_val &&
                    (decl->body.var_decl.init || decl->body.var_decl.linkage == 0)) {
                    if (decl->body.var_decl.init)
                        existing->body.init_val = gen_const_init(a,
                            decl->body.var_decl.init, existing->type, enum_vals);
                    else {
                        IR_Value* init = arena_alloc(a, sizeof(IR_Value));
                        init->kind = (existing->type->kind == IR_PTR) ?
                            VAL_CONST_NULL : VAL_CONST_INT;
                        init->type = existing->type;
                        existing->body.init_val = init;
                    }
                }
                continue;
            }

        IR_Value* gv = arena_alloc(a, sizeof(IR_Value));
        gv->kind = VAL_GLOBAL;
        gv->name = decl->body.var_decl.name;
        gv->type = ir_type_from_ast(a, decl->body.var_decl.var_type);

        /* fix up: if the IR type is an array-of-i32 but the AST element
         * type is a named typedef (likely fn ptr), use ptr elements */
        if (gv->type && gv->type->kind == IR_ARRAY) {
            Type* ast = decl->body.var_decl.var_type;
            Type* inner = ast;
            int depth = 0;
            while (inner && inner->kind == TYPE_ARRAY) {
                depth++;
                inner = inner->inner;
            }
            if (inner && inner->kind == TYPE_NAMED && !inner->inner) {
                /* typedef not resolved — assume pointer-sized element,
                 * rebuild the array type chain with ptr as leaf */
                IR_Type* leaf = ir_ptr_type(a, t_i8, 0);
                IR_Type* arr = leaf;
                for (int d = 0; d < depth; d++)
                    arr = ir_array_type(a, arr, 0);
                gv->type = arr;
            }
        }

        if (!gv->type || gv->type->kind == IR_VOID)
            gv->type = t_i8;

        /* set linkage for global: 0=internal(static), 1=external */
        gv->linkage = (decl->body.var_decl.linkage == 4) ? 0 : 1;

        if (decl->body.var_decl.init) {
            gv->body.init_val = gen_const_init(a, decl->body.var_decl.init,
                                               gv->type, enum_vals);
        } else if (decl->body.var_decl.linkage != 5) {
            /* not extern: tentative definition or static → zero-initialize */
            IR_Value* init = arena_alloc(a, sizeof(IR_Value));
            if (gv->type->kind == IR_PTR) {
                init->kind = VAL_CONST_NULL;
            } else {
                init->kind = VAL_CONST_INT;
            }
            init->type = gv->type;
            gv->body.init_val = init;
        }
        /* else: extern decl → init_val stays NULL → emitted as external */

        gv->next = mod->globals;
        mod->globals = gv;
        hashmap_put(&global_map, gv->name, gv);
    }
    }

    /* second pass: function definitions */
    for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
        if (decl->type == AST_FUNC_DEF && decl->body.func_def.body)
            ir_gen_function(mod, decl, is_device, &sig_map);
    }

    return mod;
}

IR_Module*
ir_gen_module(AST_Node* root)
{
    return ir_gen_module_ex(root, 0);
}

IR_Module*
ir_gen_program(AST_Node* root)
{
    return ir_gen_module_ex(root, 0);
}
