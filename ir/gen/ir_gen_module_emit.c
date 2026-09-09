/* ir_gen_module_emit.c -- module emission passes: struct/array
 * resolution and global/function emission.
 *
 * These helpers run from ir_gen_module_ex (ir_gen_module.c) after the
 * typedef/enum/signature collection passes: resolve_array_sizes_pass
 * finishes the AST type trees, then upgrade_existing_global/emit_global
 * lower globals and gen_module_functions lowers function bodies.
 * resolve_struct_refs_all lives in resolve/ir_gen_resolve_refs.c.
 */

#include "ir.h"

#include "ast.h"
#include "ast_walk.h"
#include "ir_gen.h"

/* resolve enum-sized dimensions on local arrays: the parser stores
 * `int arr[N]` as size_name (unresolved), and only top-level var decls
 * were walked — local arrays with enum sizes stayed [0 x N].
 * Returns 1 if a VLA bound was found (resolve_array_sizes printed it). */
typedef struct { Arena* a; TypedefEntry* enum_vals; HashMap* globals; }
    LocalArrCtx;

static int
resolve_local_arr_cb(AST_Node* n, void* ctx)
{
    if (n->type == AST_VAR_DECL) {
        LocalArrCtx* c = (LocalArrCtx*)ctx;
        return resolve_array_sizes(c->a, n->body.var_decl.var_type,
                                   c->enum_vals, c->globals);
    }
    return 0;
}

int
resolve_array_sizes_pass(Arena* a, AST_Node* root, TypedefEntry* enum_vals,
                         HashMap* globals)
{
    int bad = 0;
    LocalArrCtx lctx = { a, enum_vals, globals };

    for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
        if (decl->type == AST_VAR_DECL)
            bad |= resolve_array_sizes(a, decl->body.var_decl.var_type,
                                       enum_vals, globals);
        else if (decl->type == AST_STRUCT_DEF ||
                 decl->type == AST_UNION_DEF) {
            /* member array bounds (int a[sizeof(int)*2];) must resolve
             * before the struct's IR type is laid out */
            for (AST_Node* f = decl->body.struct_def.fields;
                 f && f->type == AST_VAR_DECL; f = f->next)
                bad |= resolve_array_sizes(a, f->body.var_decl.var_type,
                                           enum_vals, globals);
        } else if (decl->type == AST_FUNC_DEF) {
            bad |= resolve_array_sizes(a, decl->body.func_def.ret_type,
                                       enum_vals, globals);
            for (AST_Node* p = decl->body.func_def.params;
                 p && p->type == AST_PARAM_DECL; p = p->next)
                bad |= resolve_array_sizes(a, p->body.param_decl.param_type,
                                           enum_vals, globals);
            if (decl->body.func_def.body)
                bad |= (ast_walk(decl->body.func_def.body,
                                 resolve_local_arr_cb, NULL, &lctx) > 0);
        }
    }
    return bad;
}

/* upgrade an extern global to a definition when this decl has an init
 * or is a tentative definition (LINK_HOST, no extern keyword). */
void
upgrade_existing_global(Arena* a, AST_Node* decl, IR_Value* existing,
                        TypedefEntry* enum_vals, IR_Module* mod)
{
    if (!existing->body.init_val &&
        (decl->body.var_decl.init ||
         decl->body.var_decl.linkage == LINK_HOST)) {
        if (decl->body.var_decl.init) {
            int err = 0;
            existing->body.init_val = gen_const_init(a,
                decl->body.var_decl.init, existing->type, enum_vals,
                (HashMap*)mod->global_types, &err);
            if (err) mod->had_error = 1;
        } else {
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
    /* address space of the module-scope object: 0 host/private, 1 device
     * global (__device__), 2 shared, 3 constant.  The SPIR-V emitter picks
     * the storage class from it. */
    gv->addrspace = decl->body.var_decl.addr_space;

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
    gv->linkage = (decl->body.var_decl.linkage == LINK_STATIC)
                  ? IR_LINK_INTERNAL : IR_LINK_EXTERNAL;

    if (decl->body.var_decl.init) {
        int err = 0;
        gv->body.init_val = gen_const_init(a, decl->body.var_decl.init,
                                           gv->type, enum_vals,
                                           (HashMap*)mod->global_types, &err);
        if (err) mod->had_error = 1;
    } else if (decl->body.var_decl.linkage != LINK_EXTERN) {
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
