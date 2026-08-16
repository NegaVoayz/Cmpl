/* ir_type_ast.c -- AST type -> IR type conversion, plus the named/anonymous
 * struct dedup cache.  Singletons, the composite interning cache, and the
 * constructors live in ir_type.c; the IR->AST struct map in ir_type_struct.c. */

#include "ir.h"
#include "ir_type.h"

#include <string.h>

/* named struct type cache — deduplicates IR_Type objects for the same struct.
 * Disabled during pass 0 (pre-typedef-resolution) to avoid caching incomplete types. */
#define MAX_STRUCT_CACHE 64
static IR_Type* struct_cache[MAX_STRUCT_CACHE];
static Type*    struct_cache_ast[MAX_STRUCT_CACHE]; /* AST ptr for anonymous dedup */
static int n_struct_cache = 0;
static int cache_enabled = 0;

/* clone a type for use in a linked list (params/members chain).
 * shallow copy shares inner/members/name; only next is independent.
 * prevents corrupting singletons like t_i32 when two members
 * have the same type. */
static IR_Type* clone_type_for_chain(Arena* a, IR_Type* src)
{
    if (!src) return NULL;
    IR_Type* cp = arena_alloc(a, sizeof(IR_Type));
    memcpy(cp, src, sizeof(IR_Type));
    cp->next = NULL;
    return cp;
}

/* signed -> unsigned singleton counterpart (identity for non-integer kinds) */
static IR_Type*
unsigned_of(IR_Type* t)
{
    switch (t->kind) {
    case IR_I8:  return t_u8;
    case IR_I16: return t_u16;
    case IR_I32: return t_u32;
    case IR_I64: return t_u64;
    default:     return t;
    }
}

/* ---------------------------------------------------------------
 *  AST-to-IR type conversion
 * --------------------------------------------------------------- */

static IR_Type* ast_to_func_type(Arena* a, Type* ast);
static IR_Type* ast_to_struct_type(Arena* a, Type* ast);

IR_Type*
ast_to_ir_type(Arena* a, Type* ast)
{
    if (!ast) return t_void;

    switch (ast->kind) {
    case TYPE_VOID:   return t_void;
    case TYPE_CHAR:   return t_i8;
    case TYPE_SHORT:  return t_i16;
    case TYPE_INT:    return t_i32;
    case TYPE_LONG:   return t_i64;
    case TYPE_FLOAT:  return t_f32;
    case TYPE_DOUBLE: return t_f64;
    case TYPE_ENUM:   return t_i32;
    case TYPE_BOOL:   return t_i1;

    case TYPE_SIGNED:
        if (ast->next) return ast_to_ir_type(a, ast->next);
        return t_i32;
    case TYPE_UNSIGNED:
        return ast->next ? unsigned_of(ast_to_ir_type(a, ast->next)) : t_u32;
    case TYPE_PTR:
    { IR_Type* inner = ast_to_ir_type(a, ast->inner); int as = 0;
      return ir_ptr_type(a, inner, as); }
    case TYPE_ARRAY:
    { IR_Type* inner = ast_to_ir_type(a, ast->inner);
      return ir_array_type(a, inner, ast->arr_size > 0 ? ast->arr_size : 0); }
    case TYPE_FUNC:
        return ast_to_func_type(a, ast);
    case TYPE_STRUCT:
    case TYPE_UNION:
        return ast_to_struct_type(a, ast);
    case TYPE_NAMED:
        if (ast->inner) return ast_to_ir_type(a, ast->inner);
        return t_i32;
    default:
        return t_i32;
    }
}

/* pointer-to-function: the declarator parser produces TYPE_FUNC ->
 * TYPE_PTR -> ret_ty for (*f)(args).  Lift the pointer layers outside
 * the function type so IR is PTR -> FUNC rather than FUNC -> PTR -> ret. */
static IR_Type*
ast_to_func_type(Arena* a, Type* ast)
{
    Type* inner = ast->inner;
    int n_ptr = 0;
    while (inner && inner->kind == TYPE_PTR) {
        n_ptr++;
        inner = inner->inner;
    }
    IR_Type* ret = ast_to_ir_type(a, inner);
    IR_Type *params = NULL, **tail = &params;

    for (AST_Node* p = ast->params; p; p = p->next) {
        IR_Type* pt = ast_to_ir_type(a, p->body.param_decl.param_type);
        *tail = clone_type_for_chain(a, pt);
        register_clone_ast(*tail, pt);
        tail = &(*tail)->next;
    }
    IR_Type* ft = ir_func_type(a, ret, params, ast->is_variadic);
    while (n_ptr-- > 0)
        ft = ir_ptr_type(a, ft, 0);
    return ft;
}

/* struct/union: dedup by name (named) or by AST pointer (anonymous).
 * Anonymous structs must also be cached so every ast_to_ir_type call
 * for the same typedef returns the same IR_Type — otherwise
 * ir_struct_ast_lookup fails on clones in member chains. */
static IR_Type*
ast_to_struct_type(Arena* a, Type* ast)
{
    if (cache_enabled) {
        if (ast->name.data) {
            for (int i = 0; i < n_struct_cache; i++) {
                IR_Type* sc = struct_cache[i];
                if (sc->name.length == ast->name.length &&
                    memcmp(sc->name.data, ast->name.data,
                           ast->name.length) == 0) {
                    /* A forward-declared (incomplete) struct must not
                     * shadow its later complete definition of the same
                     * tag: keep scanning for a complete cached entry. */
                    if (sc->members || !ast->params)
                        return sc;
                }
            }
        } else {
            for (int i = 0; i < n_struct_cache; i++)
                if (!struct_cache[i]->name.data &&
                    struct_cache_ast[i] == ast)
                    return struct_cache[i];
        }
    }
    IR_Type* t = ir_type_new(a,
        (ast->kind == TYPE_UNION) ? IR_UNION : IR_STRUCT);
    t->name = ast->name;
    /* Cache BEFORE building members so self-referencing fields
     * (e.g. Arena* prev inside struct Arena) hit the cache and
     * avoid infinite recursion -> stack overflow. */
    if (cache_enabled && n_struct_cache < MAX_STRUCT_CACHE) {
        struct_cache_ast[n_struct_cache] = ast;
        struct_cache[n_struct_cache] = t;
        n_struct_cache++;
    }
    register_struct_ast(t, ast);
    if (ast->params) {
        IR_Type** tail = &t->members;
        for (AST_Node* f = ast->params; f && f->type == AST_VAR_DECL; f = f->next) {
            IR_Type* ft = ast_to_ir_type(a, f->body.var_decl.var_type);
            if (!ft || ft->kind == IR_VOID) ft = t_i8;
            *tail = clone_type_for_chain(a, ft);
            register_clone_ast(*tail, ft);
            tail = &(*tail)->next;
        }
    }
    return t;
}

void
ir_clear_struct_cache(void)
{
    cache_enabled = 1;
}

void
ir_reset_struct_caches(void)
{
    n_struct_cache = 0;
    cache_enabled = 0;
}
