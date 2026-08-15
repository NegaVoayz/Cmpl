/* ir_type.c -- IR type singletons, composite interning cache, and type
 * constructors.  AST->IR conversion lives in ir_type_ast.c; the IR->AST
 * struct map lives in ir_type_struct.c. */

#include "ir.h"
#include "ir_type.h"

#include <stdlib.h>
#include <stdint.h>
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

IR_Type*
ir_type_from_ast(Arena* a, Type* ast_type)
{
    init_singletons();
    return ast_to_ir_type(a, ast_type);
}

void
ir_reset_type_caches(void)
{
    memset(type_slots, 0, sizeof(type_slots));
    ir_reset_struct_caches();
    ir_reset_ast_map();
}
