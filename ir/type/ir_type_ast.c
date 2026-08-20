/* ir_type_ast.c -- AST type -> IR type conversion, plus the named/anonymous
 * struct dedup cache.  Singletons, the composite interning cache, and the
 * constructors live in ir_type.c; the IR->AST struct map in ir_type_struct.c. */

#include "ir.h"
#include "ir_type.h"

#include <string.h>

/* struct type cache — deduplicates IR_Type objects for the same struct.
 * Unbounded (arena-linked): modules with many distinct struct types
 * (the compiler's own sources exceed any fixed cap) must not evict a
 * self-referential struct — a bounded cache lets structs past the cap
 * recurse forever through their pointer cycles.
 * Disabled during pass 0 (pre-typedef-resolution) to avoid caching
 * incomplete types. */
typedef struct StructCacheEntry {
    IR_Type* ty;
    Type*    ast;   /* AST ptr for anonymous dedup */
    struct StructCacheEntry* next;
} StructCacheEntry;
static StructCacheEntry* struct_cache = NULL;
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
 *
 *  A TYPE_FUNC node is ambiguous between "pointer to function" (fnptr
 *  type: FUNC(A, PTR(B)) lifts its PTR/ARRAY layers OUTSIDE the
 *  function, so void (*f)(void) -> PTR(FUNC(void->void))) and a
 *  function whose OWN return is a pointer (the pointee context: the
 *  layers are the return type, FUNC(A, PTR(B)) -> FUNC(A->PTR(B))).
 *  The context flag `pointee` selects the second reading for FUNC
 *  nodes reached through a pointer/array layer.
 * --------------------------------------------------------------- */

static IR_Type* ast_to_func_type(Arena* a, Type* ast, int pointee);
static IR_Type* ast_to_struct_type(Arena* a, Type* ast);
static IR_Type* ast_to_ir_type_ctx(Arena* a, Type* ast, int pointee);

IR_Type*
ast_to_ir_type(Arena* a, Type* ast)
{
    return ast_to_ir_type_ctx(a, ast, 0);
}

static IR_Type*
ast_to_ir_type_ctx(Arena* a, Type* ast, int pointee)
{
    if (!ast) return t_void;

    switch (ast->kind) {
    case TYPE_VOID:   return t_void;
    case TYPE_CHAR:   return t_i8;
    case TYPE_SHORT:  return t_i16;
    case TYPE_INT:    return t_i32;
    case TYPE_LONG:
        /* `long double`: x86-64 f80 has no IR type here, and the chain
         * LONG->DOUBLE previously collapsed to plain i64 with the DOUBLE
         * link dropped — every long-double value was silently computed
         * as a 64-bit INTEGER.  Map to f64 (double semantics) instead:
         * exact for the common subset, and honest otherwise. */
        if (ast->next && ast->next->kind == TYPE_DOUBLE)
            return t_f64;
        return t_i64;
    case TYPE_FLOAT:  return t_f32;
    case TYPE_DOUBLE: return t_f64;
    case TYPE_ENUM:   return t_i32;
    case TYPE_BOOL:   return t_i1;

    case TYPE_SIGNED:
        if (ast->next) return ast_to_ir_type_ctx(a, ast->next, pointee);
        return t_i32;
    case TYPE_UNSIGNED:
        return ast->next
            ? unsigned_of(ast_to_ir_type_ctx(a, ast->next, pointee))
            : t_u32;
    case TYPE_PTR:
    { IR_Type* inner = ast_to_ir_type_ctx(a, ast->inner, 1); int as = 0;
      return ir_ptr_type(a, inner, as); }
    case TYPE_ARRAY:
    { IR_Type* inner = ast_to_ir_type_ctx(a, ast->inner, 1);
      return ir_array_type(a, inner, ast->arr_size > 0 ? ast->arr_size : 0); }
    case TYPE_FUNC:
        return ast_to_func_type(a, ast, pointee);
    case TYPE_STRUCT:
    case TYPE_UNION:
        return ast_to_struct_type(a, ast);
    case TYPE_NAMED:
        /* a typedef reference names a COMPLETE type.  Pointer-form
         * typedefs (typedef int (*BI)(int,int);) carry their PTR/ARRAY
         * layers as pointer-layers — drop the pointee context entering
         * them (BI arr[2] stays [2 x ptr]).  Function-form typedefs
         * (typedef int *FP(int);, marked func_form) are FUNC-rooted:
         * in a pointee context (FP *p3) the layers describe the
         * function's OWN return, so keep pointee=1 — dropping it
         * re-lifted the return pointer into a second fnptr layer. */
        if (ast->inner) {
            Type* rt = ast->inner;
            while (rt && rt->kind == TYPE_NAMED) rt = rt->inner;
            if (pointee && rt && rt->kind == TYPE_FUNC && rt->func_form)
                return ast_to_ir_type_ctx(a, ast->inner, 1);
            return ast_to_ir_type_ctx(a, ast->inner, 0);
        }
        return t_i32;
    default:
        return t_i32;
    }
}

/* pointer-to-function: the declarator parser produces TYPE_FUNC ->
 * TYPE_PTR -> ret_ty for (*f)(args), and TYPE_FUNC -> TYPE_ARRAY ->
 * TYPE_PTR -> ret_ty for (*f[N])(args).  Lift the pointer/array layers
 * outside the function type so IR is ARRAY -> PTR -> FUNC rather than
 * FUNC -> ARRAY -> PTR -> ret. */
static IR_Type*
ast_to_func_type(Arena* a, Type* ast, int pointee)
{
    enum { MAX_LAYERS = 16 };
    Type* layers[MAX_LAYERS];
    int   n_layers = 0;

    Type* inner = ast->inner;
    while (inner && (inner->kind == TYPE_PTR || inner->kind == TYPE_ARRAY)) {
        if (n_layers >= MAX_LAYERS) break;
        layers[n_layers++] = inner;
        inner = inner->inner;
    }
    IR_Type *params = NULL, **tail = &params;

    for (AST_Node* p = ast->params; p; p = p->next) {
        IR_Type* pt = ast_to_ir_type_ctx(a, p->body.param_decl.param_type, 0);
        /* C11 6.7.6.3p7: array parameters decay to a pointer to their
         * element type.  Literal `int a[4]` params already decayed at
         * parse time (ll_declarator_params.c); typedef'd arrays like
         * va_list arrive here as IR_ARRAY and must decay too, or the
         * function type (and every call to it) passes the array by
         * value instead of by address. */
        if (pt && pt->kind == IR_ARRAY)
            pt = ir_ptr_type(a, pt->inner, 0);
        *tail = clone_type_for_chain(a, pt);
        register_clone_ast(*tail, pt);
        tail = &(*tail)->next;
    }

    /* pointee context: the layers describe THIS function's own return
     * type (function returning pointer-to-X).  int *(*q)(int) needs its
     * pointee FUNC((int), PTR(INT)) read as FUNC((int) -> PTR(INT)), not
     * as PTR(FUNC((int) -> INT)). */
    if (pointee) {
        IR_Type* ret = ast_to_ir_type_ctx(a, ast->inner, 0);

        return ir_func_type(a, ret, params, ast->is_variadic);
    }

    /* function returning a function pointer — FUNC(A, PTR(FUNC(B, X))):
     * the pointer/array layers describe the RETURN type (a pointer to
     * the inner function), so convert the inner function as a pointee
     * and wrap ret with the layers.  The plain case (*f)(args) keeps
     * the layers OUTSIDE the function. */
    if (inner && inner->kind == TYPE_FUNC) {
        IR_Type* ret = ast_to_func_type(a, inner, 1);

        for (int i = n_layers - 1; i >= 0; i--) {
            if (layers[i]->kind == TYPE_PTR)
                ret = ir_ptr_type(a, ret, 0);
            else
                ret = ir_array_type(a, ret,
                    layers[i]->arr_size > 0 ? layers[i]->arr_size : 0);
        }
        return ir_func_type(a, ret, params, ast->is_variadic);
    }

    IR_Type* ret = ast_to_ir_type_ctx(a, inner, 0);
    IR_Type* ft = ir_func_type(a, ret, params, ast->is_variadic);

    /* Re-wrap the layers outermost-first (the layer closest to the
     * return type is applied last in the walk, so rebuild in
     * reverse). */
    for (int i = n_layers - 1; i >= 0; i--) {
        if (layers[i]->kind == TYPE_PTR)
            ft = ir_ptr_type(a, ft, 0);
        else
            ft = ir_array_type(a, ft,
                layers[i]->arr_size > 0 ? layers[i]->arr_size : 0);
    }
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
            for (StructCacheEntry* e = struct_cache; e; e = e->next) {
                IR_Type* sc = e->ty;
                /* Exact AST node identity: the entry is cached BEFORE
                 * its members are built, so a cycle back into this very
                 * node (self-/mutual recursion through pointers) must
                 * return the in-flight type — the members/params guard
                 * below would skip it and recurse forever. */
                if (e->ast == ast)
                    return sc;
                if (sc->name.length == ast->name.length &&
                    memcmp(sc->name.data, ast->name.data,
                           ast->name.length) == 0) {
                    /* A forward-declared (incomplete) struct must not
                     * shadow its later complete definition of the same
                     * tag: keep scanning for a complete cached entry. */
                    if (sc->members || !ast->params)
                        return sc;
                    /* In-flight entry of the SAME definition: struct
                     * references resolved by resolve_struct_refs share
                     * the definition's field list (same params pointer),
                     * so a reference nested inside the definition being
                     * built must map to the in-flight type — a fresh
                     * clone would duplicate %struct.NAME in the IR. */
                    if (e->ast->params == ast->params)
                        return sc;
                }
            }
        } else {
            for (StructCacheEntry* e = struct_cache; e; e = e->next)
                if (!e->ty->name.data && e->ast == ast)
                    return e->ty;
        }
    }
    IR_Type* t = ir_type_new(a,
        (ast->kind == TYPE_UNION) ? IR_UNION : IR_STRUCT);
    t->name = ast->name;
    /* Cache BEFORE building members so self-referencing fields
     * (e.g. Arena* prev inside struct Arena) hit the cache and
     * avoid infinite recursion -> stack overflow. */
    if (cache_enabled) {
        StructCacheEntry* e = arena_alloc(a, sizeof(StructCacheEntry));
        e->ty = t;
        e->ast = ast;
        e->next = struct_cache;
        struct_cache = e;
    }
    register_struct_ast(t, ast);
    if (ast->params) {
        /* structs with bit-fields get the gcc storage-unit layout: a
         * member list of units + padding and a parallel field_info list
         * (ir_type_bf.c).  Plain structs keep one member per field. */
        int has_bf = 0;
        for (AST_Node* f = ast->params; f && f->type == AST_VAR_DECL;
             f = f->next)
            if (f->body.var_decl.bit_width) { has_bf = 1; break; }

        if (has_bf) {
            ir_build_bitfield_struct(a, t, ast);
            return t;
        }
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
    struct_cache = NULL;
    cache_enabled = 0;
}
