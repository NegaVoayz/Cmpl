/* ir_gen_expr.c -- AST-to-IR expression generation */

#include "ir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "arena.h"
#include "ir_gen.h"

/* ---------------------------------------------------------------
 *  CUDA builtin lookup (for device IR only)
 * --------------------------------------------------------------- */

typedef struct { const char *name, *member; int dim; const char* fn; } CudaBuiltin;

static const CudaBuiltin cuda_builtins[] = {
    {"blockIdx","x",0,"__spv_workgroup_id"},{"blockIdx","y",1,"__spv_workgroup_id"},
    {"blockIdx","z",2,"__spv_workgroup_id"},{"threadIdx","x",0,"__spv_local_invocation_id"},
    {"threadIdx","y",1,"__spv_local_invocation_id"},{"threadIdx","z",2,"__spv_local_invocation_id"},
    {"blockDim","x",0,"__spv_workgroup_size"},{"blockDim","y",1,"__spv_workgroup_size"},
    {"blockDim","z",2,"__spv_workgroup_size"},{"gridDim","x",0,"__spv_num_workgroups"},
    {"gridDim","y",1,"__spv_num_workgroups"},{"gridDim","z",2,"__spv_num_workgroups"},
};

static int match_str(const char* a, const String* b)
{
    int len = strlen(a);
    return len == b->length && memcmp(a, b->data, len) == 0;
}

/* ---------------------------------------------------------------
 *  Initializer-list stores into a temp (compound literals)
 * --------------------------------------------------------------- */

/* child (element/member) type at position idx of an aggregate type */
static IR_Type*
init_child_type(IR_Type* ty, int idx)
{
    if (!ty) return NULL;
    if (ty->kind == IR_ARRAY) return ty->inner;
    if (ty->kind == IR_STRUCT || ty->kind == IR_UNION) {
        IR_Type* m = ty->members;
        for (int i = 0; m && i < idx; i++) m = m->next;
        return m;
    }
    return NULL;
}

/* walk a designator step chain from `dst` (type `ty`), emitting nested
 * GEPs into the target slot.  sets *final_ty to the slot's type and
 * *top_idx to the first step's resolved slot index (for cursor advance).
 * returns the final slot, or NULL (after printing an error) on a bad
 * field name or out-of-range [i].  mirrors gen_const_desig in ir_gen.c. */
static IR_Value*
desig_walk_slot(GenCtx* ctx, IR_Value* dst, IR_Type* ty,
                AST_Node* steps, IR_Type** final_ty, int* top_idx,
                ContLevel* cont, int* depth)
{
    IR_Builder* b = ctx->b;
    IR_Value* slot = dst;
    IR_Type* cur = ty;
    int first = 1;

    *final_ty = NULL;
    *top_idx = -1;
    if (depth) *depth = 0;

    for (AST_Node* s = steps; s; s = s->next) {
        String fn = s->body.desig_step.field_name;

        if (fn.data) {
            Type* ast = ir_struct_ast_lookup(cur);
            int fi = ast ? ir_struct_field_index(ast, fn) : -1;
            if (fi < 0) {
                fprintf(stderr, "cmpl: error: no member '%.*s'\n",
                        fn.length, fn.data);
                return NULL;
            }
            if (first) *top_idx = fi;
            if (cont && *depth < CONT_MAX) {
                cont[*depth].agg = cur;
                cont[*depth].idx = fi;
                (*depth)++;
            }
            int is_union = (cur && cur->kind == IR_UNION);
            int gep = is_union ? 0 : fi;
            slot = ir_build_gep(b, slot,
                ir_const_int(b, t_i32, 0), ir_const_int(b, t_i32, gep));
            cur = init_child_type(cur, fi);
            /* union is emitted as { largest_member }: the offset-0 GEP gives
             * the largest member's pointer, so bitcast to the accessed
             * member's type (mirrors the member-access path). */
            if (is_union && cur)
                slot = ir_build_bitcast(b, slot, ir_ptr_type(b->arena, cur, 0));
        } else if (s->body.desig_step.index_expr) {
            AST_Node* ix = s->body.desig_step.index_expr;
            long long ii = (ix && ix->type == AST_INT_LIT)
                ? ix->body.literal.int_val : 0;
            if (!cur || cur->kind != IR_ARRAY) {
                fprintf(stderr, "cmpl: error: [index] designator on non-array\n");
                return NULL;
            }
            if (ii < 0 || ii >= cur->size) {
                fprintf(stderr, "cmpl: error: array index %lld out of bounds"
                        " for array of %d\n", ii, cur->size);
                return NULL;
            }
            if (first) *top_idx = (int)ii;
            if (cont && *depth < CONT_MAX) {
                cont[*depth].agg = cur;
                cont[*depth].idx = (int)ii;
                (*depth)++;
            }
            slot = ir_build_gep(b, slot,
                ir_const_int(b, t_i32, 0), ir_const_int(b, t_i32, (int)ii));
            cur = cur->inner;
        }
        first = 0;
    }

    *final_ty = cur;
    return slot;
}

/* descend a continuation path (built by desig_walk_slot) emitting GEPs
 * into the innermost slot; sets *child to that slot's type. */
static IR_Value*
cont_walk_slot(GenCtx* ctx, IR_Value* dst, IR_Type* ty,
               ContLevel* cont, int depth, IR_Type** child)
{
    IR_Builder* b = ctx->b;
    IR_Value* slot = dst;
    IR_Type* cur = ty;

    for (int i = 0; i < depth; i++) {
        int idx = cont[i].idx;
        int is_union = (cur && cur->kind == IR_UNION);
        int gep = is_union ? 0 : idx;
        slot = ir_build_gep(b, slot,
            ir_const_int(b, t_i32, 0), ir_const_int(b, t_i32, gep));
        cur = init_child_type(cur, idx);
        if (is_union && cur)
            slot = ir_build_bitcast(b, slot, ir_ptr_type(b->arena, cur, 0));
    }
    *child = cur;
    return slot;
}

/* advance a continuation path past its current (just-filled) slot: bump
 * the deepest index; on overflow pop and increment the parent.  leaves
 * *depth at its post-advance value (the caller drops back to the plain
 * top-level cursor once depth < 2). */
static void
init_cont_advance(ContLevel* cont, int* depth)
{
    while (*depth > 0) {
        ContLevel* L = &cont[*depth - 1];
        L->idx++;
        if (L->idx < ir_agg_count(L->agg)) return;
        (*depth)--;
    }
}

/* store one initializer element into dst; nested lists recurse into
 * the corresponding sub-slot (arrays/structs) of the aggregate.
 * brace-elided sub-aggregates (scalar into an array/struct member,
 * C99 6.7.8p20) absorb the following list elements.  shared with
 * ir_gen_stmt.c (var-decl brace inits). */
/* char a[N] = "s": copy the string's BYTES into the array element by
 * element, zero-padding the remainder and truncating at N (C11 6.7.9p14,
 * p21).  A string operand is a pointer — storing it directly would write
 * the pointer value into the array slot. */
void
gen_string_array_init(GenCtx* ctx, IR_Value* dst, AST_Node* e, IR_Type* ty)
{
    IR_Builder* b = ctx->b;
    String st = e->body.literal.str_val;
    long n = ty->size;

    for (long i = 0; i < n; i++) {
        long byte = (i < (long)st.length)
            ? (unsigned char)st.data[i] : 0;
        IR_Value* p = ir_build_gep(b, dst, ir_const_int(b, t_i32, 0),
                                   ir_const_int(b, t_i32, i));
        ir_build_store(b, ir_const_int(b, t_i8, byte), p);
    }
}

void
ir_gen_init_one(GenCtx* ctx, IR_Value* dst, AST_Node* e, IR_Type* ty)
{
    IR_Builder* b = ctx->b;

    if (e->type == AST_INIT_LIST) {
        int pos = 0;
        ContLevel cont[CONT_MAX];
        int depth = 0;
        AST_Node* sub = e->body.init_list.elems;

        while (sub) {
            int is_desig = (sub->type == AST_DESIGNATOR);
            IR_Type* child = NULL;
            IR_Value* slot = dst;
            AST_Node* val = sub;

            if (is_desig) {
                int top_idx = -1;
                depth = 0;
                val = sub->body.designator.value;
                slot = desig_walk_slot(ctx, dst, ty,
                                       sub->body.designator.steps,
                                       &child, &top_idx, cont, &depth);
                if (!slot) { depth = 0; sub = sub->next; continue; }
                if (depth < 2) depth = 0;   /* single-step: no continuation */
                pos = top_idx + 1;
            } else if (depth >= 2) {
                /* continue inside the innermost designated subobject */
                slot = cont_walk_slot(ctx, dst, ty, cont, depth, &child);
            } else {
                /* a union has a single slot: only the first positional
                 * element initializes it; later ones are excess elements
                 * (gcc ignores them — and a per-member GEP would be
                 * invalid on the one-slot union type) */
                if (ty->kind == IR_UNION && pos > 0) {
                    pos++;
                    sub = sub->next;
                    continue;
                }
                /* a string literal directly inside a char array's brace
                 * list fills the WHOLE array (C11 6.7.9p14); the cursor
                 * jumps past it */
                if (val->type == AST_STRING_LIT && ty->kind == IR_ARRAY &&
                    ty->size > 0 && ty->inner &&
                    ty->inner->kind == IR_I8) {
                    gen_string_array_init(ctx, dst, val, ty);
                    pos += ty->size;
                    sub = sub->next;
                    continue;
                }
                child = init_child_type(ty, pos);
                if ((ty->kind == IR_ARRAY || ty->kind == IR_STRUCT ||
                     ty->kind == IR_UNION) &&
                    (!child || (ty->kind == IR_ARRAY &&
                                pos >= ty->size))) {
                    /* excess initializer beyond the aggregate's capacity:
                     * gcc ignores it (with a warning).  Scalars have no
                     * capacity — (int){9} still stores its single
                     * element. */
                    pos++;
                    sub = sub->next;
                    continue;
                }
                if (child) {
                    slot = ir_build_gep(b, dst,
                        ir_const_int(b, t_i32, 0),
                        ir_const_int(b, t_i32, pos));
                    /* union slot is emitted as the largest member; bitcast
                     * to the first member's type for a positional init */
                    if (ty->kind == IR_UNION)
                        slot = ir_build_bitcast(b, slot,
                            ir_ptr_type(b->arena, child, 0));
                }
            }

            if (val->type != AST_INIT_LIST &&
                val->type != AST_STRING_LIT && child &&
                (child->kind == IR_ARRAY || child->kind == IR_STRUCT ||
                 child->kind == IR_UNION)) {
                /* brace-elided sub-aggregate: this and the next (up to
                 * capacity, stopping at a designator — C99 6.7.8p20: a
                 * designator targets the enclosing aggregate) elements
                 * initialize the child.  A string literal initializing
                 * a char array fills the WHOLE array (6.7.9p14), so it
                 * absorbs no following elements. */
                int cap = (child->kind == IR_ARRAY) ? child->size : 0;
                if (child->kind != IR_ARRAY)
                    for (IR_Type* m = child->members; m; m = m->next) cap++;
                AST_Node* last = val;
                int n = 1;
                /* the designator's value node is not linked into the list
                 * (the designator node replaced it): relink it onto the
                 * sibling chain so the elision absorbs the value AND the
                 * following elements */
                AST_Node* old_vnext = val->next;
                if (is_desig) val->next = sub->next;
                while (n < cap && last->next &&
                       last->next->type != AST_DESIGNATOR) {
                    last = last->next; n++;
                }
                AST_Node* saved = last->next;
                last->next = NULL;
                AST_Node synth;
                synth.type = AST_INIT_LIST;
                synth.next = NULL;
                synth.body.init_list.elems = val;
                synth.body.init_list.last_elem = last;
                ir_gen_init_one(ctx, slot, &synth, child);
                last->next = saved;
                if (is_desig) val->next = old_vnext;

                sub = saved;
            } else {
                ir_gen_init_one(ctx, slot, val, child);
                sub = sub->next;
            }

            if (depth >= 2) {
                init_cont_advance(cont, &depth);
                if (depth < 2) depth = 0;
            } else if (!is_desig) {
                pos++;
            }
        }
        return;
    }

    IR_Value* v;
    if (e->type == AST_STRING_LIT && ty && ty->kind == IR_ARRAY &&
        ty->size > 0) {
        gen_string_array_init(ctx, dst, e, ty);
        return;
    }
    v = gen_expr(ctx, e);
    if (!v) return;
    if (ty && v->type && !ir_type_eq(v->type, ty))
        v = coerce_to(b, v, ty);
    ir_build_store(b, v, dst);
}

/* zero-fill every scalar slot of an aggregate.  C99 requires brace-init
 * slots not covered by the list (skipped by [i]/designators or short
 * lists) to be zero-initialized; without this the alloca keeps stack
 * garbage.  shared with ir_gen_stmt.c. */
void
ir_gen_zero_fill(GenCtx* ctx, IR_Value* dst, IR_Type* ty)
{
    IR_Builder* b = ctx->b;
    if (!ty) return;

    if (ty->kind == IR_ARRAY) {
        for (int i = 0; i < ty->size; i++) {
            IR_Value* slot = ir_build_gep(b, dst,
                ir_const_int(b, t_i32, 0), ir_const_int(b, t_i32, i));
            ir_gen_zero_fill(ctx, slot, ty->inner);
        }
    } else if (ty->kind == IR_UNION) {
        /* union emits a single largest-member slot at offset 0; the GEP
         * gives the first member's pointer, so bitcast to the largest */
        IR_Type* largest = ir_union_largest_member(ty);
        if (largest) {
            IR_Value* slot = ir_build_gep(b, dst,
                ir_const_int(b, t_i32, 0), ir_const_int(b, t_i32, 0));
            slot = ir_build_bitcast(b, slot, ir_ptr_type(b->arena, largest, 0));
            ir_gen_zero_fill(ctx, slot, largest);
        }
    } else if (ty->kind == IR_STRUCT) {
        int i = 0;
        for (IR_Type* m = ty->members; m; m = m->next, i++) {
            IR_Value* slot = ir_build_gep(b, dst,
                ir_const_int(b, t_i32, 0), ir_const_int(b, t_i32, i));
            ir_gen_zero_fill(ctx, slot, m);
        }
    } else if (ty->kind == IR_PTR) {
        ir_build_store(b, ir_const_null(ctx->b->arena, ty), dst);
    } else {
        IR_Value* z = (ty->kind == IR_F32 || ty->kind == IR_F64)
            ? ir_const_float(ctx->b->arena, ty, 0.0)
            : ir_const_int(b, ty, 0);
        ir_build_store(b, z, dst);
    }
}

/* ---------------------------------------------------------------
 *  Store-target pointer for assignment LHS
 * --------------------------------------------------------------- */

/* forward: gen_expr is defined after gen_store_ptr */
IR_Value* gen_expr(GenCtx* ctx, AST_Node* n);

IR_Value*
gen_store_ptr(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;

    switch (n->type) {
    case AST_IDENT: {
        IR_Value* ptr = sym_lookup(ctx, n->body.ident.name);
        if (ptr) return ptr;
        return global_lookup(ctx->mod, n->body.ident.name);
    }
    case AST_COMPOUND_LIT: {
        /* lvalue: allocate a fresh temp and store the initializer */
        Type* ct = n->body.compound_lit.type_expr;
        IR_Type* ir_t = ct ? ir_type_from_ast(ctx->b->arena, ct) : NULL;
        IR_Value* alloca_ptr = ir_build_alloca(b, ir_t ? ir_t : t_i8);

        if (n->body.compound_lit.init) {
            /* zero skipped slots first (C99: unlisted slots are zero) */
            if (ir_t && (ir_t->kind == IR_ARRAY || ir_t->kind == IR_STRUCT ||
                         ir_t->kind == IR_UNION))
                ir_gen_zero_fill(ctx, alloca_ptr, ir_t);
            ir_gen_init_one(ctx, alloca_ptr,
                         n->body.compound_lit.init, ir_t);
        }
        return alloca_ptr;
    }
    case AST_MEMBER: {
        TokenKind op = n->body.member.op;
        String mem_name = n->body.member.member;
        IR_Value* struct_ptr = NULL;
        IR_Type* struct_ty = NULL;
        Type* ast_struct = NULL;

        if (op == TOK_ARROW) {
            IR_Value* record_val = gen_expr(ctx, n->body.member.record);
            if (!record_val || !record_val->type ||
                record_val->type->kind != IR_PTR)
                return NULL;
            struct_ty = record_val->type->inner;
            struct_ptr = record_val;
        } else {
            if (n->body.member.record->type == AST_IDENT) {
                struct_ptr = sym_lookup(ctx,
                    n->body.member.record->body.ident.name);
                if (!struct_ptr)
                    struct_ptr = global_lookup(ctx->mod,
                        n->body.member.record->body.ident.name);
                if (struct_ptr) {
                    /* locals are allocas (ptr to struct); globals carry
                     * the struct type directly — handle both */
                    struct_ty = struct_ptr->type;
                    if (struct_ty && struct_ty->kind == IR_PTR)
                        struct_ty = struct_ty->inner;
                }
            }
            if (!struct_ptr) {
                /* nested lvalue (a.b.c, arr[i].x, p->q.r): get the
                 * ADDRESS of the record via gen_store_ptr rather than
                 * loading it into a temporary.  The temp approach only
                 * reads the value, so a store to the field would be
                 * lost. */
                IR_Value* rec_ptr = gen_store_ptr(ctx,
                    n->body.member.record);
                if (rec_ptr && rec_ptr->type &&
                    rec_ptr->type->kind == IR_PTR &&
                    rec_ptr->type->inner &&
                    (rec_ptr->type->inner->kind == IR_STRUCT ||
                     rec_ptr->type->inner->kind == IR_UNION)) {
                    struct_ptr = rec_ptr;
                    struct_ty = rec_ptr->type->inner;
                } else {
                    /* true rvalue (e.g. f().x): eval + temp copy */
                    IR_Value* record_val = gen_expr(ctx,
                        n->body.member.record);
                    if (!record_val || !record_val->type ||
                        (record_val->type->kind != IR_STRUCT &&
                         record_val->type->kind != IR_UNION))
                        return NULL;
                    struct_ty = record_val->type;
                    struct_ptr = ir_build_alloca(b, struct_ty);
                    ir_build_store(b, record_val, struct_ptr);
                }
            }
        }

        if (!struct_ty || (struct_ty->kind != IR_STRUCT &&
                           struct_ty->kind != IR_UNION))
            return NULL;

        ast_struct = ir_struct_ast_lookup(struct_ty);

        int field_idx = -1;
        if (ast_struct)
            field_idx = ir_struct_field_index(ast_struct, mem_name);
        if (field_idx < 0) return NULL;

        IR_Type* field_ty = t_i32;
        { int fi = 0;
          for (IR_Type* m = struct_ty->members; m; m = m->next, fi++)
              if (fi == field_idx) { field_ty = m; break; } }

        if (ast_struct && ast_struct->kind == TYPE_UNION) {
            /* union: all fields at offset 0 — bitcast */
            if (struct_ptr->type && struct_ptr->type->kind != IR_PTR) {
                /* global variable: GEP to get its address (bitcast
                 * needs a pointer operand; globals carry the type
                 * directly) */
                struct_ptr = ir_build_gep(b, struct_ptr,
                    ir_const_int(b, t_i32, 0),
                    ir_const_int(b, t_i32, 0));
            }
            return ir_build_bitcast(b, struct_ptr,
                ir_ptr_type(ctx->b->arena, field_ty,
                    struct_ptr->type ? struct_ptr->type->addrspace : 0));
        }

        IR_Value* gep = ir_build_gep(b, struct_ptr,
            ir_const_int(b, t_i32, 0),
            ir_const_int(b, t_i32, field_idx));
        gep->type = ir_ptr_type(ctx->b->arena, field_ty, 0);
        return gep;
    }
    case AST_INDEX: {
        /* for nested indices like arr[i][j], recurse to get a pointer
         * chain rather than loading the inner value */
        IR_Value* arr = gen_store_ptr(ctx, n->body.subscript.array);
        /* if arr is a pointer-to-pointer (e.g. char** from a struct member),
         * we need to LOAD the pointer value first to get the actual base
         * address for the GEP. Otherwise we'd GEP on the address of the
         * pointer field itself, scaling by pointer size instead of byte size.
         * This fixes b->data[b->len] generating *(b + len*8) instead of
         * *(b->data + len) — the former overwrites b->len with '\0'. */
        if (arr && arr->type && arr->type->kind == IR_PTR &&
            arr->type->inner && arr->type->inner->kind == IR_PTR)
            arr = ir_build_load(b, arr);
        if (!arr) arr = gen_expr(ctx, n->body.subscript.array);
        IR_Value* idx = gen_expr(ctx, n->body.subscript.index);
        return ir_build_gep(b, arr, ir_const_int(b, t_i32, 0), idx);
    }
    case AST_UNARY:
        if (n->body.unary.op == TOK_STAR)
            return gen_expr(ctx, n->body.unary.operand);
        return NULL;
    default:
        return NULL;
    }
}

/* ---------------------------------------------------------------
 *  Expression generation
 * --------------------------------------------------------------- */

static IR_Value*
gen_member_expr(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;

        /* CUDA builtin check (device IR only) */
        if (ctx->is_device && n->body.member.record->type == AST_IDENT) {
            String *rn = &n->body.member.record->body.ident.name,
                   *mb = &n->body.member.member;
            for (int i = 0; i < 12; i++)
                if (match_str(cuda_builtins[i].name, rn) &&
                    match_str(cuda_builtins[i].member, mb))
                    { IR_Value* d = ir_const_int(b, t_i32, cuda_builtins[i].dim);
                      return ir_build_call(b, cuda_builtins[i].fn, t_i32,
                                           (IR_Value*[]){d}, 1); }
        }

        TokenKind op = n->body.member.op;
        String mem_name = n->body.member.member;
        IR_Value* struct_ptr = NULL;
        IR_Type* struct_ty = NULL;
        Type* ast_struct = NULL;

        if (op == TOK_ARROW) {
            /* -> : record is a pointer to struct/union */
            IR_Value* record_val = gen_expr(ctx, n->body.member.record);
            if (!record_val || !record_val->type ||
                record_val->type->kind != IR_PTR) {
                IR_Value* v = arena_alloc(ctx->b->arena, sizeof(IR_Value));
                v->kind = VAL_UNDEF; v->type = t_i32; return v;
            }
            struct_ty = record_val->type->inner;
            struct_ptr = record_val;
        } else {
            /* . : record is a struct/union lvalue or rvalue */
            if (n->body.member.record->type == AST_IDENT) {
                /* lvalue: use the alloca pointer directly for GEP */
                struct_ptr = sym_lookup(ctx,
                    n->body.member.record->body.ident.name);
                if (!struct_ptr)
                    struct_ptr = global_lookup(ctx->mod,
                        n->body.member.record->body.ident.name);
                if (struct_ptr) {
                    struct_ty = struct_ptr->type;
                    if (struct_ty && struct_ty->kind == IR_PTR)
                        struct_ty = struct_ty->inner;
                }
            }

            if (!struct_ptr) {
                /* nested lvalue chain (a.b.c, arr[i].x): resolve the
                 * ADDRESS first via gen_store_ptr (GEP chain, no
                 * aggregate copy), then load.  The rvalue-copy path
                 * below is NOT byte-faithful for structs/records that
                 * contain an anonymous union: the union's IR type
                 * models only the largest member, so a copied member
                 * whose offset crosses that layout's field boundary
                 * gets truncated to the field's width (e.g. a pointer
                 * read as i32 + padding). */
                IR_Value* addr = gen_store_ptr(ctx, n);

                if (addr) return ir_build_load(b, addr);

                /* rvalue: eval, store to temp alloca, GEP from there */
                IR_Value* record_val = gen_expr(ctx,
                    n->body.member.record);
                if (!record_val || !record_val->type ||
                    record_val->type->kind != IR_STRUCT &&
                    record_val->type->kind != IR_UNION) {
                    IR_Value* v = arena_alloc(ctx->b->arena, sizeof(IR_Value));
                    v->kind = VAL_UNDEF; v->type = t_i32; return v;
                }
                struct_ty = record_val->type;
                struct_ptr = ir_build_alloca(b, struct_ty);
                ir_build_store(b, record_val, struct_ptr);
            }
        }

        if (!struct_ty || (struct_ty->kind != IR_STRUCT &&
                           struct_ty->kind != IR_UNION)) {
            IR_Value* v = arena_alloc(ctx->b->arena, sizeof(IR_Value));
            v->kind = VAL_UNDEF; v->type = t_i32; return v;
        }

        ast_struct = ir_struct_ast_lookup(struct_ty);

        int field_idx = -1;
        if (ast_struct)
            field_idx = ir_struct_field_index(ast_struct, mem_name);

        if (field_idx < 0) {
            IR_Value* v = arena_alloc(ctx->b->arena, sizeof(IR_Value));
            v->kind = VAL_UNDEF; v->type = t_i32; return v;
        }

        /* compute field type */
        IR_Type* field_ty = t_i32;
        { int fi = 0;
          for (IR_Type* m = struct_ty->members; m; m = m->next, fi++)
              if (fi == field_idx) { field_ty = m; break; } }

        if (ast_struct && ast_struct->kind == TYPE_UNION) {
            /* union: all fields at offset 0 — bitcast pointer, then load */
            if (struct_ptr->type && struct_ptr->type->kind != IR_PTR) {
                /* global variable: GEP to get its address first */
                struct_ptr = ir_build_gep(b, struct_ptr,
                    ir_const_int(b, t_i32, 0),
                    ir_const_int(b, t_i32, 0));
            }
            IR_Value* cast_ptr = ir_build_bitcast(b, struct_ptr,
                ir_ptr_type(ctx->b->arena, field_ty, struct_ptr->type ?
                    struct_ptr->type->addrspace : 0));
            return ir_build_load(b, cast_ptr);
        }

        /* struct: GEP to field, then load */
        IR_Value* gep = ir_build_gep(b, struct_ptr,
            ir_const_int(b, t_i32, 0),
            ir_const_int(b, t_i32, field_idx));
        gep->type = ir_ptr_type(ctx->b->arena, field_ty, 0);
        return ir_build_load(b, gep);
}

IR_Value* gen_expr(GenCtx* ctx, AST_Node* n)
{
    if (!n) return NULL;

    IR_Builder* b = ctx->b;

    switch (n->type) {
    case AST_INT_LIT:   return ir_const_int(b, n->body.literal.is_unsigned ? t_u32 : t_i32, n->body.literal.int_val);
    case AST_LONG_LIT:  return ir_const_int(b, n->body.literal.is_unsigned ? t_u64 : t_i64, n->body.literal.int_val);
    case AST_CHAR_LIT:  return ir_const_int(b, t_i8, n->body.literal.char_val);
    case AST_FLOAT_LIT: return ir_const_float(ctx->b->arena, t_f32, n->body.literal.float_val);
    case AST_DOUBLE_LIT:return ir_const_float(ctx->b->arena, t_f64, n->body.literal.float_val);

    case AST_STRING_LIT:
    { IR_Value* v = arena_alloc(ctx->b->arena, sizeof(IR_Value));
      v->kind = VAL_CONST_STRING; v->type = ir_ptr_type(ctx->b->arena, t_i8, 0);
      v->body.str_val = n->body.literal.str_val; return v; }

    case AST_IDENT:
    { IR_Value* ptr = sym_lookup(ctx, n->body.ident.name);
      if (ptr) return ir_build_load(b, ptr);
      ptr = global_lookup(ctx->mod, n->body.ident.name);
      if (ptr) return ir_build_load(b, ptr);
      /* function name used as a value (function pointer) —
       * resolves to the global function symbol. */
      if (ctx->sig_map) {
          IR_Type* func_ty = func_type_lookup(ctx->sig_map, n->body.ident.name);
          if (func_ty) {
              IR_Value* fn_val = arena_alloc(ctx->b->arena, sizeof(IR_Value));
              fn_val->kind = VAL_GLOBAL;
              fn_val->name = n->body.ident.name;
              fn_val->type = ir_ptr_type(ctx->b->arena, func_ty, 0);
              return fn_val;
          }
      }
      IR_Value* v = arena_alloc(ctx->b->arena, sizeof(IR_Value));
      v->kind = VAL_UNDEF; v->type = t_i32; return v; }

    case AST_BINARY:
    { TokenKind op = n->body.binary.op;
      int is_cmpd = (op == TOK_PLUSEQ || op == TOK_MINUSEQ ||
                     op == TOK_STAREQ || op == TOK_SLASHEQ ||
                     op == TOK_PERCENTEQ || op == TOK_AMPEQ ||
                     op == TOK_PIPEEQ || op == TOK_CARETEQ ||
                     op == TOK_LTLTEQ || op == TOK_GTGTEQ);

      /* logical AND/OR: short-circuit (must not evaluate RHS if LHS
       * decides the result) */
      if (op == TOK_AMPAMP || op == TOK_PIPEPIPE)
          return gen_logical(ctx, op, n->body.binary.left,
                             n->body.binary.right);

      if (op == TOK_EQ || is_cmpd) {
          IR_Value* rhs = gen_expr(ctx, n->body.binary.right);
          IR_Value* ptr = gen_store_ptr(ctx, n->body.binary.left);
          if (ptr) {
              IR_Value* result;
              if (op == TOK_EQ) result = rhs;
              else { IR_Value* old = ir_build_load(b, ptr);
                     result = gen_binary_op(ctx, op, old, rhs); }
              ir_build_store(b, result, ptr);
              return result; }
          if (op == TOK_EQ) return rhs;
          IR_Value* l = gen_expr(ctx, n->body.binary.left);
          return gen_binary_op(ctx, op, l, rhs); }
      IR_Value* l = gen_expr(ctx, n->body.binary.left);
      IR_Value* r = gen_expr(ctx, n->body.binary.right);
      return gen_binary_op(ctx, op, l, r); }

    case AST_UNARY: return gen_unary_expr(ctx, n);

    case AST_CALL: return gen_call_expr(ctx, n);

    case AST_KERNEL_LAUNCH:
    { AST_Node* cn = n->body.kernel_launch.callee; String kn = {0,0};
      if (cn && cn->type == AST_IDENT) kn = cn->body.ident.name;
      char pn[128]; int kl = kn.length > 120 ? 120 : kn.length;
      memcpy(pn, "__cmpl_kl_", 10); if (kl>0) memcpy(pn+10, kn.data, kl); pn[10+kl] = '\0';
      int n_args = 0; IR_Value* ab[16];
      for (AST_Node* c = n->body.kernel_launch.config; c && n_args < 16; c = c->next) ab[n_args++] = gen_expr(ctx, c);
      for (AST_Node* a = n->body.kernel_launch.args; a && n_args < 16; a = a->next) ab[n_args++] = gen_expr(ctx, a);
      return ir_build_call(b, pn, t_void, ab, n_args); }

    case AST_TERNARY: return gen_ternary_expr(ctx, n);

    case AST_CAST: return gen_cast(ctx, n);
    case AST_COMPOUND_LIT:
    { /* (type){init} — temp lvalue: alloca + initializer stores.
         * Array types decay to a pointer; scalar/struct values load. */
        IR_Value* ptr = gen_store_ptr(ctx, n);
        if (!ptr) return NULL;

        if (ptr->type && ptr->type->kind == IR_PTR &&
            ptr->type->inner && ptr->type->inner->kind == IR_ARRAY)
            return ptr;
        return ir_build_load(b, ptr); }

    case AST_INDEX:
    { /* Get the base address via gen_store_ptr, which does NOT decay a
       * multi-dimensional array to &arr[0].  Using gen_expr here would
       * decay `table` to &table[0], making the first index step ELEMENTS
       * instead of ROWS (so table[i][j] reads table[0][i][j]). */
      IR_Value* arr = gen_store_ptr(ctx, n->body.subscript.array);
      if (arr && arr->type && arr->type->kind == IR_PTR &&
          arr->type->inner && arr->type->inner->kind == IR_PTR)
          arr = ir_build_load(b, arr);
      if (!arr) arr = gen_expr(ctx, n->body.subscript.array);
      IR_Value* idx = gen_expr(ctx, n->body.subscript.index);
      IR_Value* gep = ir_build_gep(b, arr, ir_const_int(b, t_i32, 0), idx);
      return ir_build_load(b, gep); }

    case AST_MEMBER: return gen_member_expr(ctx, n);

    case AST_SIZEOF_TYPE:
    { IR_Type* t = ir_type_from_ast(ctx->b->arena, n->body.sizeof_type.type_expr);
      return ir_const_int(b, t_i32, ir_type_size(t)); }

    case AST_SIZEOF_EXPR:
    { int sz = 4;
      /* sizeof(array) must not decay the array to a pointer.
       * Check both global and local arrays so sizeof(buf) yields the
       * full array size, not the pointer size. */
      if (n->body.sizeof_expr.expr &&
          n->body.sizeof_expr.expr->type == AST_IDENT) {
          String nm = n->body.sizeof_expr.expr->body.ident.name;
          IR_Value* gv = global_lookup(ctx->mod, nm);
          if (gv && gv->type && gv->type->kind == IR_ARRAY) {
              sz = ir_type_size(gv->type);
          } else {
              IR_Value* lv = sym_lookup(ctx, nm);
              if (lv && lv->type && lv->type->kind == IR_PTR &&
                  lv->type->inner && lv->type->inner->kind == IR_ARRAY) {
                  sz = ir_type_size(lv->type->inner);
              } else {
                  IR_Value* sub = gen_expr(ctx, n->body.sizeof_expr.expr);
                  sz = sub ? ir_type_size(sub->type) : 4;
              }
          }
      } else {
          IR_Value* sub = gen_expr(ctx, n->body.sizeof_expr.expr);
          sz = sub ? ir_type_size(sub->type) : 4;
      }
      return ir_const_int(b, t_i32, sz); }

    case AST_POSTFIX:
    { IR_Value* ptr = gen_store_ptr(ctx, n->body.postfix.operand);
      if (!ptr) { IR_Value* v = arena_alloc(ctx->b->arena, sizeof(IR_Value)); v->kind = VAL_UNDEF; v->type = t_i32; return v; }
      IR_Value* old_val = ir_build_load(b, ptr);
      IR_Value* new_val;
      if (old_val->type && old_val->type->kind == IR_PTR) {
          /* pointer +/- 1 → GEP */
          IR_Value* idx;
          if (n->body.postfix.op == TOK_PLUSPLUS)
              idx = ir_const_int(b, t_i32, 1);
          else {
              IR_Value* neg = ir_build_sub(b, ir_const_int(b, t_i32, 0),
                                           ir_const_int(b, t_i32, 1));
              idx = neg;
          }
          new_val = ir_build_gep(b, old_val, idx, ir_const_int(b, t_i32, 0));
      } else {
          IR_Value* one = ir_const_int(b, old_val->type ? old_val->type : t_i32, 1);
          new_val = (n->body.postfix.op == TOK_PLUSPLUS) ? ir_build_add(b, old_val, one)
                                                         : ir_build_sub(b, old_val, one);
      }
      ir_build_store(b, new_val, ptr);
      return old_val; }

    default:
    { IR_Value* v = arena_alloc(ctx->b->arena, sizeof(IR_Value)); v->kind = VAL_UNDEF; v->type = t_i32; return v; }
    }
}
