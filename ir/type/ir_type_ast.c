/* ir_type_ast.c -- AST type -> IR type conversion, plus the named/anonymous
 * struct dedup cache.  Singletons, the composite interning cache, and the
 * constructors live in ir_type.c; the IR->AST struct map in ir_type_struct.c.
 * Function-form conversion (ast_to_func_type) and the shared chain-clone /
 * signedness helpers live in ir_type_func.c. */

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

/* function-form conversion (ast_to_func_type) and the shared chain-clone /
 * signedness helpers live in ir_type_func.c (declared in ir_type.h). */
static IR_Type* ast_to_struct_type(Arena* a, Type* ast);

IR_Type*
ast_to_ir_type(Arena* a, Type* ast)
{
    return ast_to_ir_type_ctx(a, ast, 0);
}

IR_Type*
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
