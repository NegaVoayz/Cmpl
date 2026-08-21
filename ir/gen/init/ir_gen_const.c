/* ir_gen_const.c -- AST-to-IR constant initializer lowering.
 *
 * gen_const_init is the public entry point (declared in ir_gen.h); it
 * dispatches on the AST node kind, delegating lists to
 * gen_const_init_list (ir_gen_const_list.c), the scalar/string/
 * union-aggregate cases to ir_gen_const_scalar.c, and keeping the
 * ident/unary cases here.  `globals` is the file-scope var name ->
 * Type* table (for sizeof/& operands); `err` is set to 1 when the
 * initializer is genuinely non-constant (gcc: "initializer element is
 * not constant"), so emit_global/upgrade_existing_global can fail the
 * compile loudly instead of silently emitting 0.
 */

#include "../ir_gen.h"
#include "ir_gen_init.h"

#include <stdio.h>
#include <string.h>

/* an enum constant reference, or (for pointer targets) an array decay /
 * function name -> VAL_GLOBAL.  A bare non-enum scalar identifier is not
 * a constant initializer (gcc: "initializer element is not constant");
 * with the globals table we can tell arrays apart from scalars. */
static IR_Value*
gen_const_ident(Arena* a, AST_Node* init, IR_Type* target_type,
                TypedefEntry* enum_vals, HashMap* globals, int* err)
{
    for (TypedefEntry* ev = enum_vals; ev; ev = ev->next) {
        if (ev->name.length == init->body.ident.name.length &&
            memcmp(ev->name.data, init->body.ident.name.data,
                   ev->name.length) == 0) {
            long long iv = (long long)(intptr_t)ev->aliased_type;
            return gen_const_scalar(a, target_type, iv, (double)iv);
        }
    }

    /* an identifier initializing a pointer is the address of a global
     * (array decay `int* p = garr;` or a function name) — VAL_GLOBAL.
     * A scalar global is NOT a constant address (gcc rejects
     * `int* p = g;`), so only arrays pass through here. */
    if (target_type && target_type->kind == IR_PTR) {
        if (globals) {
            Type* t = (Type*)hashmap_get(globals, init->body.ident.name);
            while (t && t->kind == TYPE_NAMED && t->inner) t = t->inner;
            if (t && t->kind != TYPE_ARRAY && t->kind != TYPE_FUNC) {
                if (err) *err = 1;
                fprintf(stderr, "cmpl: error: initializer element is not "
                        "constant (line %d)\n", init->loc.line);
            }
        }
        IR_Value* v = arena_alloc(a, sizeof(IR_Value));
        v->type = target_type;
        v->kind = VAL_GLOBAL;
        v->name = ir_const_ir_name(globals, init->body.ident.name);
        return v;
    }

    if (err) *err = 1;
    fprintf(stderr, "cmpl: error: initializer element is not constant "
            "(line %d)\n", init->loc.line);
    { IR_Value* v = arena_alloc(a, sizeof(IR_Value)); v->type = target_type;
      v->kind = VAL_CONST_INT; v->body.int_val = 0; return v; }
}

/* unary -x / ~x on a constant (usually already folded by opt_fold). */
static IR_Value*
gen_const_unary(Arena* a, AST_Node* init, IR_Type* target_type,
                TypedefEntry* enum_vals, HashMap* globals, int* err)
{
    if (init->body.unary.op == TOK_MINUS) {
        IR_Value* inner = gen_const_init(a, init->body.unary.operand,
                                          target_type, enum_vals,
                                          globals, err);
        if (inner && inner->kind == VAL_CONST_INT)
            inner->body.int_val = -inner->body.int_val;
        else if (inner && inner->kind == VAL_CONST_FLOAT)
            inner->body.float_val = -inner->body.float_val;
        return inner;
    }
    if (init->body.unary.op == TOK_TILDE) {
        IR_Value* inner = gen_const_init(a, init->body.unary.operand,
                                          target_type, enum_vals,
                                          globals, err);
        if (inner && inner->kind == VAL_CONST_INT)
            inner->body.int_val = ~inner->body.int_val;
        return inner;
    }
    if (init->body.unary.op == TOK_AMP &&
        init->body.unary.operand &&
        init->body.unary.operand->type == AST_IDENT) {
        /* &global / &function in a constant initializer: the global's
         * address (ptr @g), like the IDENT path for arrays.  Previously
         * this fell through and silently initialized the pointer to 0.
         * Only a pointer target can hold an address — an integer target
         * ((long)&g) fails loudly (gcc would need a relocation). */
        if (!target_type || target_type->kind != IR_PTR) {
            if (err) *err = 1;
            fprintf(stderr, "cmpl: error: initializer element is not "
                    "constant (line %d)\n", init->loc.line);
            { IR_Value* z = arena_alloc(a, sizeof(IR_Value));
              z->type = target_type; z->kind = VAL_CONST_INT;
              z->body.int_val = 0; return z; }
        }
        IR_Value* v = arena_alloc(a, sizeof(IR_Value));
        v->type = target_type;
        v->kind = VAL_GLOBAL;
        v->name = ir_const_ir_name(globals,
            init->body.unary.operand->body.ident.name);
        return v;
    }
    { IR_Value* v = gen_const_ice_eval(a, init, target_type, enum_vals,
                                       globals, err);
      if (v) return v;
      /* gen_const_ice_eval reported the error and set *err; the
       * compile fails, this zero is only an unwind placeholder. */
      { IR_Value* z = arena_alloc(a, sizeof(IR_Value));
        z->kind = VAL_CONST_INT; z->type = target_type;
        z->body.int_val = 0; return z; } }
}

/* _Generic with a constant controlling expression in a const-init path:
 * handled in ir_gen_const_generic.c (the ast-opt fold normally resolves
 * it first). */

IR_Value*
gen_const_init(Arena* a, AST_Node* init, IR_Type* target_type,
               TypedefEntry* enum_vals, HashMap* globals, int* err)
{
    if (!init || !target_type) return NULL;

    switch (init->type) {
    case AST_INIT_LIST:
        return gen_const_init_list(a, init, target_type, enum_vals,
                                   globals, err);

    case AST_INT_LIT:
    case AST_LONG_LIT:
        return gen_const_scalar(a, target_type,
                                init->body.literal.int_val,
                                (double)init->body.literal.int_val);

    case AST_CHAR_LIT:
        return gen_const_scalar(a, target_type,
                                init->body.literal.char_val,
                                (double)init->body.literal.char_val);

    case AST_FLOAT_LIT:
    case AST_DOUBLE_LIT:
        return gen_const_scalar(a, target_type,
                                (long long)init->body.literal.float_val,
                                init->body.literal.float_val);

    case AST_STRING_LIT:
        return gen_const_string(a, init, target_type);

    case AST_IDENT:
        return gen_const_ident(a, init, target_type, enum_vals,
                               globals, err);

    case AST_CAST:
        /* apply the cast type first, then convert to the member type
         * (mirrors gen_cast): {(int)2.5} in a double member is 2.0 */
    {   IR_Type* cty = ir_type_from_ast(a, init->body.cast.type_expr);
        IR_Value* inner = gen_const_init(a, init->body.cast.cast_expr,
                                         cty ? cty : target_type, enum_vals,
                                         globals, err);
        return inner ? gen_const_convert(a, inner, target_type) : NULL;
    }

    case AST_UNARY:
        return gen_const_unary(a, init, target_type, enum_vals,
                               globals, err);

    case AST_GENERIC:
        return gen_const_generic(a, init, target_type, enum_vals,
                                 globals, err);

    default:
        /* sizeof, _Alignof, ternary, arithmetic over constants, mixed
         * float/int: evaluate with ICE semantics (ir_gen_const_ice.c)
         * before the error path.  A non-constant expression (garr[0],
         * s.a, ...) fails the compile loudly instead of emitting 0. */
    {   IR_Value* v = gen_const_ice_eval(a, init, target_type, enum_vals,
                                         globals, err);
        if (v) return v;
        /* gen_const_ice_eval already printed "initializer element is
         * not constant" and set *err — the compile fails; return a
         * zero placeholder so the walk can unwind safely. */
        { IR_Value* z = arena_alloc(a, sizeof(IR_Value));
          z->kind = VAL_CONST_INT; z->type = target_type;
          z->body.int_val = 0; return z; }
    }
    }
}
