/* ir_gen_module.c -- AST-to-IR module generation (top-level entry).
 *
 * ir_gen_module_ex drives the passes over a whole AST_PROGRAM: typedefs,
 * enum constants, function signatures, then globals and function bodies.
 * The struct/array resolution and global/function emission helpers live
 * in ir_gen_module_emit.c.
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

/* register one enumerator list into enum_vals (prepended, head = last). */
static void
register_enum_def(Arena* a, AST_Node* def, TypedefEntry** enum_vals)
{
    int val = 0;
    for (AST_Node* en = def->body.enum_def.enumerators;
         en && en->type == AST_ENUMERATOR; en = en->next) {
        if (en->body.enumerator.value &&
            (en->body.enumerator.value->type == AST_INT_LIT ||
             en->body.enumerator.value->type == AST_LONG_LIT))
            val = (int)en->body.enumerator.value->body.literal.int_val;
        TypedefEntry* ev = arena_alloc(a, sizeof(TypedefEntry));
        ev->name = en->body.enumerator.name;
        ev->aliased_type = (Type*)(intptr_t)val;
        ev->next = *enum_vals; *enum_vals = ev;
        val++;
    }
}

typedef struct {
    Arena*        a;
    TypedefEntry** enum_vals;
} EnumValCtx;

/* walker: register every enum definition found — top-level or nested in
 * a function body — so array sizes like `int arr[N]` (size_name lookup)
 * resolve for function-scope enum constants too. */
static int
collect_enum_cb(AST_Node* n, void* ctx)
{
    if (n->type == AST_ENUM_DEF) {
        EnumValCtx* c = (EnumValCtx*)ctx;
        register_enum_def(c->a, n, c->enum_vals);
    }
    return 0;
}

static TypedefEntry*
collect_enum_vals(Arena* a, AST_Node* root)
{
    TypedefEntry* enum_vals = NULL;
    EnumValCtx ctx = { a, &enum_vals };

    for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next)
        ast_walk(decl, collect_enum_cb, NULL, &ctx);
    return enum_vals;
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
