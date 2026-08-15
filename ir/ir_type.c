/* ir_type.c -- IR type constructors and AST-to-IR type conversion */

/* TODO(refactor): still >200 lines — AST-to-IR resolution (ast_to_ir_type +
 * struct/type caches) is tightly coupled; split into ir_type_ast.c in P2. */

#include "ir.h"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "ast.h"
#include "arena.h"

/* ---------------------------------------------------------------
 *  Common type singletons
 * --------------------------------------------------------------- */

IR_Type *t_void, *t_i1, *t_i8, *t_i16, *t_i32, *t_i64;
IR_Type *t_u8, *t_u16, *t_u32, *t_u64;
IR_Type *t_f32, *t_f64;

static int singletons_inited = 0;

/* named struct type cache — deduplicates IR_Type objects for the same struct.
 * Disabled during pass 0 (pre-typedef-resolution) to avoid caching incomplete types. */
#define MAX_STRUCT_CACHE 64
static IR_Type* struct_cache[MAX_STRUCT_CACHE];
static Type*    struct_cache_ast[MAX_STRUCT_CACHE]; /* AST ptr for anonymous dedup */
static int n_struct_cache = 0;
static int cache_enabled = 0;

/* ---------------------------------------------------------------
 *  Composite type interning cache
 *
 *  Keyed by (kind, inner_ptr, extra) — open addressing, power-of-two.
 *  Makes ir_ptr_type / ir_array_type return the same IR_Type* for
 *  identical parameters, so ir_type_eq reduces to pointer comparison.
 * --------------------------------------------------------------- */

#define TYPE_CACHE_SIZE 128
#define TYPE_CACHE_MASK (TYPE_CACHE_SIZE - 1)

typedef struct {
    IR_TypeKind kind;
    IR_Type*    inner;
    int         extra;     /* addrspace for PTR, size for ARRAY, 0 for FUNC */
    IR_Type*    cached;
} TypeSlot;

static TypeSlot type_slots[TYPE_CACHE_SIZE];

static unsigned type_cache_hash(IR_TypeKind kind, IR_Type* inner, int extra)
{
    unsigned long long h = (unsigned long long)kind;
    h = h * 31 + (unsigned long long)(uintptr_t)inner;
    h = h * 31 + (unsigned long long)extra;
    return (unsigned)(h & TYPE_CACHE_MASK);
}

static IR_Type* type_cache_get(IR_TypeKind kind, IR_Type* inner, int extra)
{
    unsigned h = type_cache_hash(kind, inner, extra);

    for (int i = 0; i < TYPE_CACHE_SIZE; i++) {
        TypeSlot* s = &type_slots[h];
        if (!s->cached) return NULL;
        if (s->kind == kind && s->inner == inner && s->extra == extra)
            return s->cached;
        h = (h + 1) & TYPE_CACHE_MASK;
    }
    return NULL;
}

static void type_cache_put(IR_TypeKind kind, IR_Type* inner, int extra,
                           IR_Type* t)
{
    unsigned h = type_cache_hash(kind, inner, extra);

    for (int i = 0; i < TYPE_CACHE_SIZE; i++) {
        TypeSlot* s = &type_slots[h];
        if (!s->cached) {
            s->kind = kind;
            s->inner = inner;
            s->extra = extra;
            s->cached = t;
            return;
        }
        h = (h + 1) & TYPE_CACHE_MASK;
    }
}

/* IR_Type → AST Type mapping for struct/union types.
 * Used by AST_MEMBER handler to find field indices by name.
 * Kept as a separate table (not a field in IR_Type) to avoid
 * bootstrapping issues when the compiler compiles itself. */
#define MAX_AST_MAP 256
static IR_Type* ast_map_keys[MAX_AST_MAP];
static Type*    ast_map_vals[MAX_AST_MAP];
static int      n_ast_map = 0;

static void register_struct_ast(IR_Type* ir, Type* ast)
{
    if (n_ast_map >= MAX_AST_MAP) return;
    for (int i = 0; i < n_ast_map; i++)
        if (ast_map_keys[i] == ir) return;
    ast_map_keys[n_ast_map] = ir;
    ast_map_vals[n_ast_map] = ast;
    n_ast_map++;
}

/* register a member/param chain clone under its source's AST type so
 * ir_struct_ast_lookup's exact-pointer match resolves clones of
 * anonymous structs (which otherwise fall through to the members-
 * pointer fallback and can miss when the anonymous type materializes
 * into more than one IR_Type object). */
static void
register_clone_ast(IR_Type* clone, IR_Type* src)
{
    if (!clone || (clone->kind != IR_STRUCT && clone->kind != IR_UNION))
        return;

    for (int i = 0; i < n_ast_map; i++)
        if (ast_map_keys[i] == src) {
            register_struct_ast(clone, ast_map_vals[i]);
            return;
        }
}

static IR_Type*
make_singleton(IR_TypeKind kind)
{
    IR_Type* t = calloc(1, sizeof(IR_Type));
    t->kind = kind;
    return t;
}

static IR_Type*
make_singleton_unsigned(IR_TypeKind kind)
{
    IR_Type* t = make_singleton(kind);
    t->is_unsigned = 1;
    return t;
}

static void
init_singletons(void)
{
    if (singletons_inited) return;

    t_void = make_singleton(IR_VOID);
    t_i1   = make_singleton(IR_I1);
    t_i8   = make_singleton(IR_I8);
    t_i16  = make_singleton(IR_I16);
    t_i32  = make_singleton(IR_I32);
    t_i64  = make_singleton(IR_I64);
    t_u8   = make_singleton_unsigned(IR_I8);
    t_u16  = make_singleton_unsigned(IR_I16);
    t_u32  = make_singleton_unsigned(IR_I32);
    t_u64  = make_singleton_unsigned(IR_I64);
    t_f32  = make_singleton(IR_F32);
    t_f64  = make_singleton(IR_F64);
    singletons_inited = 1;
}

/* ---------------------------------------------------------------
 *  Type constructors
 * --------------------------------------------------------------- */

IR_Type*
ir_type_new(Arena* a, IR_TypeKind kind)
{
    IR_Type* t = arena_alloc(a, sizeof(IR_Type));
    t->kind = kind;
    return t;
}

IR_Type*
ir_ptr_type(Arena* a, IR_Type* inner, int addrspace)
{
    IR_Type* cached = type_cache_get(IR_PTR, inner, addrspace);

    if (cached) return cached;

    IR_Type* t = ir_type_new(a, IR_PTR);
    t->inner = inner;
    t->addrspace = addrspace;
    type_cache_put(IR_PTR, inner, addrspace, t);
    return t;
}

IR_Type*
ir_array_type(Arena* a, IR_Type* elem, int size)
{
    IR_Type* cached = type_cache_get(IR_ARRAY, elem, size);

    if (cached) return cached;

    IR_Type* t = ir_type_new(a, IR_ARRAY);
    t->inner = elem;
    t->size = size;
    type_cache_put(IR_ARRAY, elem, size, t);
    return t;
}

IR_Type*
ir_func_type(Arena* a, IR_Type* ret, IR_Type* params, int is_variadic)
{
    IR_Type* t = ir_type_new(a, IR_FUNC);
    t->inner = ret;
    t->members = params;
    t->is_variadic = is_variadic;
    return t;
}

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

static IR_Type*
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

IR_Type*
ir_type_from_ast(Arena* a, Type* ast_type)
{
    init_singletons();
    return ast_to_ir_type(a, ast_type);
}

void
ir_clear_struct_cache(void)
{
    cache_enabled = 1;
}

void
ir_reset_type_caches(void)
{
    n_struct_cache = 0;
    cache_enabled = 0;
    n_ast_map = 0;
    memset(type_slots, 0, sizeof(type_slots));
}

/* ---------------------------------------------------------------
 *  Struct AST lookup — for field-name → index resolution
 * --------------------------------------------------------------- */

Type*
ir_struct_ast_lookup(IR_Type* t)
{
    if (!t || (t->kind != IR_STRUCT && t->kind != IR_UNION)) return NULL;

    /* exact pointer match first (covers originals and registered clones) */
    for (int i = 0; i < n_ast_map; i++)
        if (ast_map_keys[i] == t)
            return ast_map_vals[i];

    /* clone fallback: clone_type_for_chain shallow-copies struct/union
     * types for member chains. clones share the same members pointer
     * and name data — match by structural identity. */
    for (int i = 0; i < n_ast_map; i++) {
        IR_Type* k = ast_map_keys[i];
        if (k->kind != t->kind) continue;
        if (t->name.data) {
            /* named: match by tag name */
            if (k->name.data == t->name.data &&
                k->name.length == t->name.length)
                return ast_map_vals[i];
        } else {
            /* anonymous: match by members pointer (shared via shallow copy) */
            if (k->members == t->members)
                return ast_map_vals[i];
        }
    }
    return NULL;
}

