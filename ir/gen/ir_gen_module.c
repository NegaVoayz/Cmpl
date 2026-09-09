/* ir_gen_module.c -- AST-to-IR module generation (top-level entry).
 *
 * ir_gen_module_ex drives the passes over a whole AST_PROGRAM: the
 * typedef/enum/signature collection walks live in ir_gen_module_collect.c,
 * struct/array resolution and global/function emission in
 * ir_gen_module_emit.c.  module_prepare and emit_module_globals are the
 * two phase helpers below.
 */

#include "ir.h"

#include "ast.h"
#include "hash.h"
#include "ir_gen.h"

/* Module-wide type/const preparation: reset the static type caches,
 * resolve typedefs / struct refs / enum values / array sizes, evaluate
 * static asserts, and collect the function-signature map that
 * gen_module_functions needs.  Returns a heap (arena) sig_map.  The
 * global_types table is kept on the arena too — both must outlive this
 * frame because emit_module_globals and function gen read them through
 * the module (a stack HashMap would dangle after module_prepare returns). */
static HashMap*
module_prepare(Arena* a, AST_Node* root, IR_Module* mod)
{
    /* reset static caches so no IR_Type* from a previous module's arena
     * leaks into this one (critical for GPU host→device dual gen) */
    ir_reset_type_caches();

    TypedefEntry* typedefs = collect_typedefs(a, root);
    update_opaque_typedefs(root, typedefs);

    for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next)
        resolve_ast_node(decl, typedefs);

    resolve_struct_refs_all(a, root);

    /* file-scope var name -> Type* table for sizeof(garr)/&g inside
     * constant expressions (array bounds, static asserts, const-inits).
     * Built after struct refs resolve and before resolve_array_sizes_pass
     * (which evaluates bounds containing sizeof of globals). */
    HashMap* gtypes = arena_alloc(a, sizeof(HashMap));
    hashmap_init(gtypes, a, 64);
    collect_global_types(a, root, gtypes);
    mod->global_types = gtypes;

    TypedefEntry* enum_vals = collect_enum_vals(a, root, gtypes);
    mod->enum_vals = enum_vals;

    /* VLA bounds are unsupported: resolve_array_sizes marks a non-constant
     * `[expr]` bound with arr_size == -1 and an unresolvable `[ident]`
     * bound with size_name; both fail the compile loudly here instead of
     * silently emitting `alloca [0 x i32]` (OOB writes at runtime). */
    if (resolve_array_sizes_pass(a, root, enum_vals, gtypes))
        mod->had_error = 1;

    /* enable struct type dedup cache — typedefs are now resolved, so
     * subsequent ir_type_from_ast() calls get consistent IR_Type* */
    ir_clear_struct_cache();

    /* C11 _Static_assert: evaluate every condition (file + block scope);
     * a false or non-constant one sets mod->had_error so the compile
     * fails with a nonzero exit (gcc parity) */
    ir_check_static_asserts(a, mod, root, enum_vals, gtypes);

    HashMap* sig_map = arena_alloc(a, sizeof(HashMap));
    hashmap_init(sig_map, a, 64);
    collect_func_sigs(a, root, sig_map);
    return sig_map;
}

/* lower file-scope globals, deduping through global_map: a redeclaration
 * upgrades the existing entry (upgrade_existing_global) instead of
 * emitting a duplicate. */
static void
emit_module_globals(Arena* a, AST_Node* root, IR_Module* mod,
                    TypedefEntry* enum_vals)
{
    HashMap global_map;
    hashmap_init(&global_map, a, 64);

    for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
        if (decl->type != AST_VAR_DECL) continue;
        IR_Value* existing = hashmap_get(&global_map,
                                         decl->body.var_decl.name);
        if (existing) {
            upgrade_existing_global(a, decl, existing, enum_vals, mod);
            continue;
        }
        emit_global(a, decl, mod, &global_map, enum_vals);
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

    HashMap* sig_map = module_prepare(a, root, mod);
    emit_module_globals(a, root, mod, mod->enum_vals);
    gen_module_functions(root, mod, is_device, sig_map);

    /* a semantic error during IR gen (e.g. &bit-field) must fail the
     * compile, not silently ship a garbage module (gcc parity) */
    if (mod->had_error) {
        arena_free(mod->arena);
        return NULL;
    }

    return mod;
}

IR_Module*
ir_gen_program(AST_Node* root)
{
    return ir_gen_module_ex(root, 0);
}
