/* ir_gen_const_generic.c -- _Generic in the constant-initializer path.
 *
 * The ast-opt fold pass normally replaces a _Generic selection with a
 * constant controlling expression by the chosen arm before IR gen, so
 * this is a safety net (cast-wrapped or otherwise unfoldable shapes)
 * that selects the arm here and lowers it as a constant instead of
 * hitting the generic "unhandled init type" fallback.
 */

#include "../ir_gen.h"
#include "ir_gen_init.h"

IR_Value*
gen_const_generic(Arena* a, AST_Node* init, IR_Type* target_type,
                  TypedefEntry* enum_vals, HashMap* globals, int* err)
{
    AST_Node* ctrl = init->body.generic.controlling;
    IR_Type* ctype = NULL;

    switch (ctrl->type) {
    case AST_INT_LIT:
        ctype = ctrl->body.literal.is_unsigned ? t_u32 : t_i32; break;
    case AST_LONG_LIT:
        ctype = ctrl->body.literal.is_unsigned ? t_u64 : t_i64; break;
    case AST_CHAR_LIT:  ctype = t_i8;  break;
    case AST_FLOAT_LIT: ctype = t_f32; break;
    case AST_DOUBLE_LIT: ctype = t_f64; break;
    case AST_CAST:
        ctype = ir_type_from_ast(a, ctrl->body.cast.type_expr); break;
    default: break;
    }

    if (!ctype) {
        fprintf(stderr, "gen_const: _Generic controlling expression is not "
                "a constant at line %d\n", init->loc.line);
        IR_Value* v = arena_alloc(a, sizeof(IR_Value));

        v->kind = VAL_CONST_INT;
        v->type = target_type;
        v->body.int_val = 0;
        return v;
    }

    AST_Node* match = NULL;
    AST_Node* deflt = NULL;

    for (AST_Node* as = init->body.generic.assoc_list; as; as = as->next) {
        IR_Type* at;

        if (as->type != AST_GENERIC_ASSOC)
            continue;
        if (!as->body.generic_assoc.type) { deflt = as; continue; }
        at = ir_type_from_ast(a, as->body.generic_assoc.type);
        if (at && ir_type_eq(at, ctype)) { match = as; break; }
    }
    if (!match)
        match = deflt;
    if (!match) {
        fprintf(stderr, "gen_const: _Generic at line %d: no matching "
                "association and no default arm\n", init->loc.line);
        IR_Value* v = arena_alloc(a, sizeof(IR_Value));

        v->kind = VAL_CONST_INT;
        v->type = target_type;
        v->body.int_val = 0;
        return v;
    }
    return gen_const_init(a, match->body.generic_assoc.expr,
                          target_type, enum_vals, globals, err);
}
