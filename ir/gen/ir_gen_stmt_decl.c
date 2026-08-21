/* ir_gen_stmt_decl.c -- variable-declaration lowering: static locals as
 * module-level globals, block locals as allocas (split out of
 * ir_gen_stmt.c, B-14).  Both entry points are declared in ir_gen.h and
 * dispatched from gen_stmt. */

#include "ir.h"

#include <stdio.h>
#include <string.h>

#include "ast.h"
#include "arena.h"
#include "ir_gen.h"

/* ---------------------------------------------------------------
 *  Static local: module-level global with a function-mangled name.
 *  C11 6.2.4p3: the object persists across calls, so it must live in
 *  static storage (mod->globals), not in a per-call alloca.  The
 *  initializer is constant by definition (C11 6.7.9p4); a failed
 *  const fold falls back to zero-init.
 * --------------------------------------------------------------- */

IR_Value* gen_static_local(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;
    IR_Module*  mod = ctx->mod;
    IR_Type*    vt = ir_type_from_ast(b->arena, n->body.var_decl.var_type);

    if (!vt || vt->kind == IR_VOID) vt = t_i8;

    /* unique global name: "<func>.<var>.<line>" — source identifiers
     * cannot contain '.', so this cannot collide with user globals */
    int   flen = b->cur_func && b->cur_func->name.data
               ? (int)b->cur_func->name.length : 0;
    int   vlen = (int)n->body.var_decl.name.length;
    char* nm = arena_alloc(b->arena, flen + vlen + 24);

    if (b->cur_func && b->cur_func->name.data) {
        memcpy(nm, b->cur_func->name.data, flen);
        nm[flen] = '.';
    } else {
        nm[0] = '.';
        flen = 1;
    }
    memcpy(nm + flen + 1, n->body.var_decl.name.data, vlen);
    sprintf(nm + flen + 1 + vlen, ".%d", n->loc.line);

    IR_Value* gv = arena_alloc(b->arena, sizeof(IR_Value));
    gv->kind = VAL_GLOBAL;
    gv->name.data = nm;
    gv->name.length = (int)strlen(nm);
    gv->type = vt;
    gv->linkage = IR_LINK_INTERNAL;   /* static local: internal */

    if (n->body.var_decl.init) {
        int err = 0;
        gv->body.init_val = gen_const_init(b->arena,
                                           n->body.var_decl.init, vt,
                                           (TypedefEntry*)mod->enum_vals,
                                           (HashMap*)mod->global_types, &err);
        if (err) mod->had_error = 1;
    }
    if (!gv->body.init_val) {
        IR_Value* init = arena_alloc(b->arena, sizeof(IR_Value));
        init->kind = (vt->kind == IR_PTR) ? VAL_CONST_NULL : VAL_CONST_INT;
        init->type = vt;
        gv->body.init_val = init;
    }

    /* register the static as an address-constant root for LATER statics
     * in this function (`static int* p = arr + 1;`): the ICE evaluator
     * reads the TYPE from mod->global_types, and ir_const_ir_name spots
     * the entry by the mangled name stored in the clone's `name` field
     * (source identifiers cannot contain '.').  File-scope inits all ran
     * before function gen (emit_global precedes gen_module_functions),
     * so these entries can never shadow a later file-scope init. */
    if (mod->global_types) {
        Type* clone = arena_alloc(b->arena, sizeof(Type));
        memcpy(clone, n->body.var_decl.var_type, sizeof(Type));
        clone->name = gv->name;
        hashmap_put((HashMap*)mod->global_types,
                    n->body.var_decl.name, clone);
    }

    gv->next = mod->globals;
    mod->globals = gv;
    sym_add(ctx, n->body.var_decl.name, gv);
    return gv;
}

/* ---------------------------------------------------------------
 *  Variable declaration: alloca + type-aware initialiser
 * --------------------------------------------------------------- */

void gen_stmt_var_decl(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;

    if (n->body.var_decl.linkage == LINK_STATIC) {   /* static local */
        gen_static_local(ctx, n);
        return;
    }

    IR_Type* vt = ir_type_from_ast(b->arena, n->body.var_decl.var_type);

    if (!vt || vt->kind == IR_VOID) vt = t_i8;
    IR_Value* al = ir_build_alloca(b, vt);
    sym_add(ctx, n->body.var_decl.name, al);

    if (n->body.var_decl.init &&
        n->body.var_decl.init->type == AST_INIT_LIST) {
        /* array/struct initializer: recursive, type-aware stores
         * (nested braces, designators, C99 cursor).  The old per-element
         * gen_expr loop dropped inner brace lists (no AST_INIT_LIST case
         * in gen_expr → undef) and emitted wrong GEP chains.  Zero the
         * uncovered slots first (C99 6.7.8p21: unlisted members/elements
         * are zero-initialized). */
        if (vt->kind == IR_ARRAY || vt->kind == IR_STRUCT ||
            vt->kind == IR_UNION)
            ir_gen_zero_fill(ctx, al, vt);
        ir_gen_init_one(ctx, al, n->body.var_decl.init, vt, NULL);
    } else if (n->body.var_decl.init) {
        AST_Node* initn = n->body.var_decl.init;
        if (initn->type == AST_STRING_LIT && vt &&
            vt->kind == IR_ARRAY && vt->size > 0) {
            /* char a[N] = "s": copy bytes, not the pointer */
            gen_string_array_init(ctx, al, initn, vt);
        } else {
            IR_Value* init = gen_expr(ctx, initn);
            if (init && vt == t_i32 &&
                n->body.var_decl.var_type &&
                n->body.var_decl.var_type->kind == TYPE_NAMED &&
                !n->body.var_decl.var_type->inner &&
                init->type && init->type->kind == IR_PTR)
                vt = ir_ptr_type(b->arena, t_i8, 0);
            /* Coerce the init to the variable's type before storing:
               `int* p = 0;` must store a full-width null pointer, not
               a 4-byte i32 0 into an 8-byte `alloca ptr` (the load then
               read 4 garbage bytes -> `p == 0` was false).  Only scalar
               coercions are allowed here: an aggregate init whose type is
               an anonymous struct/union CLONE fails ir_type_eq (clones
               compare by pointer identity) and coerce_to would emit a
               self-bitcast on a value, which LLVM rejects.  An aggregate
               store is already valid as-is (value type drives the store). */
            if (init && init->type && vt &&
                !ir_type_eq(init->type, vt) &&
                !(init->type->kind == IR_STRUCT ||
                  init->type->kind == IR_UNION ||
                  init->type->kind == IR_ARRAY) &&
                !(vt->kind == IR_STRUCT ||
                  vt->kind == IR_UNION ||
                  vt->kind == IR_ARRAY))
                init = coerce_to(b, init, vt);
            if (init) ir_build_store(b, init, al);
        }
    }
}
