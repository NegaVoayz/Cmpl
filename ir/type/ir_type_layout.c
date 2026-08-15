/* ir_type_layout.c -- IR type layout, size, equality, and field lookup */

#include "ir.h"
#include "ast.h"

#include <string.h>

/* ---------------------------------------------------------------
 *  Type utilities
 * --------------------------------------------------------------- */

static int
ir_type_align(IR_Type* t)
{
    if (!t) return 1;

    switch (t->kind) {
    case IR_VOID:  return 1;
    case IR_I1:    return 1;
    case IR_I8:    return 1;
    case IR_I16:   return 2;
    case IR_I32:   return 4;
    case IR_I64:   return 8;
    case IR_F32:   return 4;
    case IR_F64:   return 8;
    case IR_PTR:   return 8;
    case IR_ARRAY: return ir_type_align(t->inner);
    case IR_STRUCT:
    case IR_UNION:
    { int max_a = 1;
      for (IR_Type* f = t->members; f; f = f->next) {
          int a = ir_type_align(f);
          if (a > max_a) max_a = a;
      }
      return max_a; }
    case IR_FUNC:  return 1;
    default:       return 1;
    }
}

int
ir_type_size(IR_Type* t)
{
    if (!t) return 0;

    switch (t->kind) {
    case IR_VOID:  return 0;
    case IR_I1:    return 1;
    case IR_I8:    return 1;
    case IR_I16:   return 2;
    case IR_I32:   return 4;
    case IR_I64:   return 8;
    case IR_F32:   return 4;
    case IR_F64:   return 8;
    case IR_PTR:   return 8;   /* 64-bit pointer */
    case IR_ARRAY: return t->size * ir_type_size(t->inner);
    case IR_UNION:
    { int max_sz = 0, max_al = 1;
      for (IR_Type* f = t->members; f; f = f->next) {
          int sz = ir_type_size(f);
          int al = ir_type_align(f);
          if (sz > max_sz) max_sz = sz;
          if (al > max_al) max_al = al;
      }
      return (max_sz + max_al - 1) / max_al * max_al; }
    case IR_STRUCT:
    { int offset = 0, max_al = 1;
      for (IR_Type* f = t->members; f; f = f->next) {
          int al = ir_type_align(f);
          int sz = ir_type_size(f);
          if (al > max_al) max_al = al;
          offset = (offset + al - 1) / al * al;  /* align */
          offset += sz;
      }
      return (offset + max_al - 1) / max_al * max_al; }
    case IR_FUNC:  return 0;
    default:       return 0;
    }
}

/* the largest member of a union (by size) — the single slot the union is
 * emitted as (all members overlap at offset 0).  NULL for non-unions. */
IR_Type*
ir_union_largest_member(IR_Type* t)
{
    if (!t || t->kind != IR_UNION || !t->members) return NULL;
    IR_Type* best = NULL;
    int best_sz = 0;
    for (IR_Type* m = t->members; m; m = m->next) {
        int sz = ir_type_size(m);
        if (sz > best_sz) { best_sz = sz; best = m; }
    }
    return best;
}

/* number of child slots an aggregate occupies: array element count,
 * struct member count, or 1 for a union (single largest-member slot).
 * 0 for non-aggregates. */
int
ir_agg_count(IR_Type* t)
{
    if (!t) return 0;
    if (t->kind == IR_ARRAY) return t->size;
    if (t->kind == IR_UNION) return 1;
    if (t->kind == IR_STRUCT) {
        int n = 0;
        for (IR_Type* m = t->members; m; m = m->next) n++;
        return n;
    }
    return 0;
}

int
ir_type_eq(IR_Type* a, IR_Type* b)
{
    if (a == b) return 1;
    if (!a || !b) return 0;
    if (a->kind != b->kind) return 0;

    switch (a->kind) {
    case IR_PTR:
        return a->addrspace == b->addrspace && ir_type_eq(a->inner, b->inner);
    case IR_ARRAY:
        return a->size == b->size && ir_type_eq(a->inner, b->inner);
    case IR_FUNC:
        return ir_type_eq(a->inner, b->inner) && ir_type_eq(a->members, b->members);
    case IR_STRUCT:
    case IR_UNION:
        if (!a->name.data && !b->name.data)
            return a == b;  /* anonymous: pointer identity only */
        return a->name.data == b->name.data;  /* named: compare by tag */
    default:
        return 1;
    }
}

const char*
ir_type_name(IR_Type* t)
{
    if (!t) return "void";

    switch (t->kind) {
    case IR_VOID:  return "void";
    case IR_I1:    return "i1";
    case IR_I8:    return "i8";
    case IR_I16:   return "i16";
    case IR_I32:   return "i32";
    case IR_I64:   return "i64";
    case IR_F32:   return "float";
    case IR_F64:   return "double";
    case IR_PTR:   return "ptr";
    case IR_ARRAY: return "array";
    case IR_STRUCT:return "struct";
    case IR_UNION: return "union";
    case IR_FUNC:  return "func";
    default:       return "?";
    }
}

/* ---------------------------------------------------------------
 *  Struct field lookup -- field-name -> index resolution
 * --------------------------------------------------------------- */

int
ir_struct_field_index(Type* ast_struct, String field_name)
{
    if (!ast_struct || (ast_struct->kind != TYPE_STRUCT &&
                         ast_struct->kind != TYPE_UNION))
        return -1;

    int idx = 0;
    for (AST_Node* f = ast_struct->params;
         f && f->type == AST_VAR_DECL; f = f->next, idx++) {
        if (f->body.var_decl.name.length == field_name.length &&
            memcmp(f->body.var_decl.name.data,
                   field_name.data, field_name.length) == 0)
            return idx;
    }
    return -1;  /* not found */
}
