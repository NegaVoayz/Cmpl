/* ir_gen_module.c -- AST-to-IR module generation (top-level entry).
 *
 * ir_gen_module_ex drives the passes over a whole AST_PROGRAM: typedefs,
 * enum constants, struct/union resolution, array sizes, function
 * signatures, globals, then function bodies.
 */

#include "ir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "hash.h"
#include "ast_walk.h"
#include "ir_gen.h"

static TypedefEntry*
collect_typedefs(Arena* a, AST_Node* root)
{
    TypedefEntry* typedefs = NULL;
    for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
        if (decl->type != AST_TYPEDEF) continue;
        TypedefEntry* te = arena_alloc(a, sizeof(TypedefEntry));
        te->name = decl->body.typedef_decl.name;
        te->aliased_type = decl->body.typedef_decl.aliased_type;
        te->next = typedefs; typedefs = te;
    }
    return typedefs;
}

static void
update_opaque_typedefs(AST_Node* root, TypedefEntry* typedefs)
{
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
}

static TypedefEntry*
collect_enum_vals(Arena* a, AST_Node* root)
{
    TypedefEntry* enum_vals = NULL;
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
    return enum_vals;
}

/* resolve TYPE_STRUCT references (with missing params) against the
 * module's struct/union definitions, including local defs in bodies. */
static void
resolve_struct_refs_all(Arena* a, AST_Node* root)
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

    /* register standalone local struct/union defs first so refs resolve */
    for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
        if (decl->type == AST_FUNC_DEF && decl->body.func_def.body) {
            LocalDefCtx lc = { &struct_map, a };
            ast_walk(decl->body.func_def.body,
                     collect_local_struct_def_cb, NULL, &lc);
        }
    }
    for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
        if (decl->type == AST_FUNC_DEF && decl->body.func_def.body) {
            resolve_struct_refs_stmt(decl->body.func_def.body, &struct_map);
            ast_walk(decl->body.func_def.body,
                     resolve_compound_lit_type_cb, NULL, &struct_map);
        }
    }
}

static void
resolve_array_sizes_pass(AST_Node* root, TypedefEntry* enum_vals)
{
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

/* collect function signatures (ret + param list) for call lookup.  MUST
 * run after typedef resolution so struct return types resolve. */
static void
collect_func_sigs(Arena* a, AST_Node* root, HashMap* sig_map)
{
    for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
        if (decl->type != AST_FUNC_DEF) continue;

        IR_Type* rt = ir_type_from_ast(a, decl->body.func_def.ret_type);
        IR_Type *params = NULL, **ptail = &params;

        for (AST_Node* p = decl->body.func_def.params;
             p && p->type == AST_PARAM_DECL; p = p->next) {
            IR_Type* pty = ir_type_from_ast(a, p->body.param_decl.param_type);
            /* unresolved typedef param → ptr fallback (same fixup as
             * ir_gen_function's param handling) */
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
        hashmap_put(sig_map, decl->body.func_def.name, func_ty);
    }
}

/* upgrade an extern global to a definition when this decl has an init
 * or is a tentative definition (linkage==0, no extern keyword). */
static void
upgrade_existing_global(Arena* a, AST_Node* decl, IR_Value* existing,
                        TypedefEntry* enum_vals)
{
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
}

/* emit one global variable (VAL_GLOBAL) and register it in both the
 * module list and the dedup map. */
static void
emit_global(Arena* a, AST_Node* decl, IR_Module* mod, HashMap* global_map,
            TypedefEntry* enum_vals)
{
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
    hashmap_put(global_map, gv->name, gv);
}

static void
gen_module_functions(AST_Node* root, IR_Module* mod, int is_device,
                     HashMap* sig_map)
{
    for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
        if (decl->type == AST_FUNC_DEF && decl->body.func_def.body)
            ir_gen_function(mod, decl, is_device, sig_map);
    }
}

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

    TypedefEntry* typedefs = collect_typedefs(a, root);
    update_opaque_typedefs(root, typedefs);
    TypedefEntry* enum_vals = collect_enum_vals(a, root);

    for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next)
        resolve_ast_node(decl, typedefs);

    resolve_struct_refs_all(a, root);
    resolve_array_sizes_pass(root, enum_vals);

    /* enable struct type dedup cache — typedefs are now resolved, so
     * subsequent ir_type_from_ast() calls get consistent IR_Type* */
    ir_clear_struct_cache();

    HashMap sig_map;
    hashmap_init(&sig_map, a, 64);
    collect_func_sigs(a, root, &sig_map);

    {
        HashMap global_map;
        hashmap_init(&global_map, a, 64);

        for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
            if (decl->type != AST_VAR_DECL) continue;
            IR_Value* existing = hashmap_get(&global_map,
                                             decl->body.var_decl.name);
            if (existing) {
                upgrade_existing_global(a, decl, existing, enum_vals);
                continue;
            }
            emit_global(a, decl, mod, &global_map, enum_vals);
        }
    }

    gen_module_functions(root, mod, is_device, &sig_map);

    return mod;
}

IR_Module*
ir_gen_program(AST_Node* root)
{
    return ir_gen_module_ex(root, 0);
}
