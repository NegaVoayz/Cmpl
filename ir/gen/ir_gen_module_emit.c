/* ir_gen_module_emit.c -- module emission passes: struct/array
 * resolution and global/function emission.
 *
 * These helpers run from ir_gen_module_ex (ir_gen_module.c) after the
 * typedef/enum/signature collection passes: resolve_struct_refs_all and
 * resolve_array_sizes_pass finish the AST type trees, then
 * upgrade_existing_global/emit_global lower globals and
 * gen_module_functions lowers function bodies.
 */

#include "ir.h"

#include "ast.h"
#include "ast_walk.h"
#include "ir_gen.h"

/* resolve TYPE_STRUCT references (with missing params) against the
 * module's struct/union definitions, including local defs in bodies. */
void
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
            ast_walk(decl->body.func_def.body,
                     resolve_sizeof_cast_type_cb, NULL, &struct_map);
        } else if (decl->type == AST_STATIC_ASSERT) {
            /* file-scope asserts: resolve sizeof/alignof/cast type refs
             * so sizeof(struct S) in a condition is not unsized */
            ast_walk(decl, resolve_sizeof_cast_type_cb, NULL, &struct_map);
        }
    }
}

/* resolve enum-sized dimensions on local arrays: the parser stores
 * `int arr[N]` as size_name (unresolved), and only top-level var decls
 * were walked — local arrays with enum sizes stayed [0 x N]. */
static int
resolve_local_arr_cb(AST_Node* n, void* ctx)
{
    if (n->type == AST_VAR_DECL)
        resolve_array_sizes(n->body.var_decl.var_type,
                            (TypedefEntry*)ctx);
    return 0;
}

void
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
            if (decl->body.func_def.body)
                ast_walk(decl->body.func_def.body,
                         resolve_local_arr_cb, NULL, enum_vals);
        }
    }
}

/* upgrade an extern global to a definition when this decl has an init
 * or is a tentative definition (linkage==0, no extern keyword). */
void
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
void
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

void
gen_module_functions(AST_Node* root, IR_Module* mod, int is_device,
                     HashMap* sig_map)
{
    for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
        if (decl->type == AST_FUNC_DEF && decl->body.func_def.body)
            ir_gen_function(mod, decl, is_device, sig_map);
    }
}
