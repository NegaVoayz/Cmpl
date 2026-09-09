/* ir_type.c -- IR type singletons, composite interning cache (backed by
 * base/hash.c), and type constructors.  AST->IR conversion lives in
 * ir_type_ast.c; the IR->AST struct map lives in ir_type_struct.c. */

#include "ir.h"
#include "ir_type.h"
#include "hash.h"

#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------
 *  Common type singletons
 * --------------------------------------------------------------- */

IR_Type *t_void, *t_i1, *t_i8, *t_i16, *t_i32, *t_i64;
IR_Type *t_u8, *t_u16, *t_u32, *t_u64;
IR_Type *t_f32, *t_f64;

static int singletons_inited = 0;

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

/* public: ensure the type singletons exist.  The ICE type inference and
 * const-init paths use t_i8/t_i32/... without going through
 * ir_type_from_ast (which used to be the only initializer), so a module
 * with no other type conversion (e.g. a lone _Static_assert) must init
 * them explicitly. */
void
ir_init_types(void)
{
    init_singletons();
}

/* ---------------------------------------------------------------
 *  Composite type interning cache
 *
 *  Keyed by (kind, inner_ptr, extra) so ir_ptr_type / ir_array_type
 *  return the same IR_Type* for identical parameters, making
 *  ir_type_eq reduce to pointer comparison.  Backed by base/hash.c's
 *  open-addressing map (reused, not reimplemented): the key is a
 *  packed blob of the three fields, and the map grows past the old
 *  fixed 128 slots at 70 % load.  The map is per-module — reset drops
 *  it between modules (GPU has two arenas) — so each module's first
 *  put re-inits it from that module's arena, which also owns the
 *  persistent key blobs.
 * --------------------------------------------------------------- */

#define TYPE_CACHE_KEY_BYTES \
    (sizeof(IR_TypeKind) + sizeof(IR_Type*) + sizeof(int))

static HashMap type_cache;

/* pack (kind, inner, extra) into blob; returns the blob length */
static int
type_cache_fill(char* blob, IR_TypeKind kind, IR_Type* inner, int extra)
{
    char* p = blob;

    memcpy(p, &kind, sizeof kind);   p += sizeof kind;
    memcpy(p, &inner, sizeof inner); p += sizeof inner;
    memcpy(p, &extra, sizeof extra); p += sizeof extra;
    return (int)(p - blob);
}

static IR_Type*
type_cache_get(IR_TypeKind kind, IR_Type* inner, int extra)
{
    char   blob[TYPE_CACHE_KEY_BYTES];
    String key;

    key.data = blob;
    key.length = type_cache_fill(blob, kind, inner, extra);
    return (IR_Type*)hashmap_get(&type_cache, key);
}

static void
type_cache_put(Arena* a, IR_TypeKind kind, IR_Type* inner, int extra,
               IR_Type* t)
{
    String key;

    if (!type_cache.arena) hashmap_init(&type_cache, a, 128);
    key.data = arena_alloc(a, TYPE_CACHE_KEY_BYTES);
    key.length = type_cache_fill((char*)key.data, kind, inner, extra);
    hashmap_put(&type_cache, key, t);
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
    type_cache_put(a, IR_PTR, inner, addrspace, t);
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
    type_cache_put(a, IR_ARRAY, elem, size, t);
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

IR_Type*
ir_type_from_ast(Arena* a, Type* ast_type)
{
    init_singletons();
    return ast_to_ir_type(a, ast_type);
}

void
ir_reset_type_caches(void)
{
    memset(&type_cache, 0, sizeof(type_cache));
    ir_reset_struct_caches();
    ir_reset_ast_map();
}
