/* ir_gen_resolve_arrays.c -- array-size resolution for the AST type
 * tree before IR gen (split out of ir_gen_resolve.c).  Resolves
 * enum-sized dimensions on local arrays and fails loudly on VLA bounds
 * instead of silently emitting `alloca [0 x i32]` (OOB writes).
 * resolve_array_sizes is the public entry (declared in ir_gen.h);
 * resolve_array_sizes_ex is the recursion, guarded against
 * self-/mutually-referential structs. */

#include "ir.h"

#include <stdio.h>

#include "ast.h"
#include "../ir_gen.h"

/* Resolve enum-sized dimensions on local arrays: the parser stores
 * `int arr[N]` as size_name (unresolved), and only top-level var decls
 * were walked — local arrays with enum sizes stayed [0 x N].
 *
 * Also the fail-loudly gate for VLA bounds: the parser marks a
 * non-constant `[expr]` bound with arr_size == -1 and a runtime-variable
 * `[ident]` bound with size_name; both must reject the declaration
 * instead of silently emitting `alloca [0 x i32]` (OOB writes).
 * Returns 1 if a VLA bound was found. */
/* one visited struct/union Type node whose fields were already recursed
 * (self-/mutually-referential structs would otherwise cycle forever:
 * `struct S* next` fields reach the same field list through resolve_struct
 * refs — the visited guard keeps the walk finite) */
typedef struct VNode { Type* t; struct VNode* next; } VNode;

static int
resolve_array_sizes_ex(Arena* a, Type* t, TypedefEntry* enum_vals,
                       HashMap* globals, VNode* visited)
{
    if (!t) return 0;

    int bad = resolve_array_sizes_ex(a, t->inner, enum_vals, globals,
                                     visited);
    bad |= resolve_array_sizes_ex(a, t->next, enum_vals, globals, visited);

    /* struct/union member arrays (fields are AST_VAR_DECL); function
     * params (AST_PARAM_DECL) are skipped — they decay to pointers
     * before this pass, so VLA params stay legal.  Each struct Type
     * node's fields are recursed once. */
    if (t->kind == TYPE_STRUCT || t->kind == TYPE_UNION) {
        for (VNode* v = visited; v; v = v->next)
            if (v->t == t) return bad;
        VNode vn = { t, visited };
        for (AST_Node* p = t->params; p; p = p->next)
            if (p->type == AST_VAR_DECL)
                bad |= resolve_array_sizes_ex(a, p->body.var_decl.var_type,
                                              enum_vals, globals, &vn);
    }

    if (t->kind != TYPE_ARRAY) return bad;

    if (t->arr_size == -1) {
        fprintf(stderr, "ir: array bound is not a constant expression (VLA not supported)\n");
        return 1;
    }

    if (t->arr_size == 0 && t->size_name.data) {
        Type* found = enum_vals ? typedef_lookup(enum_vals, t->size_name) : NULL;

        if (found)
            t->arr_size = (int)(intptr_t)found;
        else {
            fprintf(stderr, "ir: array bound '%.*s' is not a constant expression (VLA not supported)\n",
                    (int)t->size_name.length, t->size_name.data);
            bad = 1;
        }
    } else if (t->arr_size == 0 && t->arr_expr) {
        /* `[sizeof(int)*2]`-style constant bound: the parser kept the
         * AST expr (it only folds a single literal-binary); evaluate it
         * here with ICE semantics so valid constant bounds compile and
         * genuine VLAs still fail loudly. */
        ICEVal val;
        const char* why = NULL;

        resolve_enum_idents(t->arr_expr, enum_vals);

        if (ice_eval(a, t->arr_expr, &val, &why, globals)) {
            fprintf(stderr, "ir: array bound is not a constant expression (VLA not supported)\n");
            bad = 1;
        } else if (val.is_float || val.v <= 0) {
            fprintf(stderr, "ir: array bound must be a positive integer constant\n");
            bad = 1;
        } else {
            t->arr_size = (int)val.v;
        }
    }
    return bad;
}

int resolve_array_sizes(Arena* a, Type* t, TypedefEntry* enum_vals,
                        HashMap* globals)
{
    return resolve_array_sizes_ex(a, t, enum_vals, globals, NULL);
}
