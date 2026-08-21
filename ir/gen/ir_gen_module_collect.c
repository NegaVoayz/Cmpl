/* ir_gen_module_collect.c -- module-wide collection passes: typedefs,
 * enum constants and function signatures (split out of ir_gen_module.c,
 * B-15).  The two ast_walk callbacks and their ctx structs stay
 * file-local — they are only referenced here. */

#include "ir.h"

#include <stdint.h>
#include <string.h>

#include "ast.h"
#include "hash.h"
#include "ast_walk.h"
#include "ir_gen.h"

typedef struct { Arena* a; TypedefEntry** typedefs; } TypedefCtx;

/* register typedefs nested in function bodies too (mirrors collect_enum_cb). */
static int
collect_typedef_cb(AST_Node* n, void* ctx)
{
    if (n->type != AST_TYPEDEF) return 0;
    TypedefCtx* c = (TypedefCtx*)ctx;
    TypedefEntry* te = arena_alloc(c->a, sizeof(TypedefEntry));
    te->name = n->body.typedef_decl.name;
    te->aliased_type = n->body.typedef_decl.aliased_type;
    te->next = *c->typedefs; *c->typedefs = te;
    return 0;
}

TypedefEntry*
collect_typedefs(Arena* a, AST_Node* root)
{
    TypedefEntry* typedefs = NULL;
    TypedefCtx ctx = { a, &typedefs };

    for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next)
        ast_walk_single(decl, collect_typedef_cb, NULL, &ctx);
    return typedefs;
}

void
update_opaque_typedefs(AST_Node* root, TypedefEntry* typedefs)
{
    for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
        if (decl->type != AST_STRUCT_DEF && decl->type != AST_UNION_DEF) continue;
        if (!decl->body.struct_def.name.data || !decl->body.struct_def.fields) continue;
        for (TypedefEntry* te = typedefs; te; te = te->next) {
            if (!te->aliased_type) continue;
            if (te->aliased_type->kind != TYPE_STRUCT &&
                te->aliased_type->kind != TYPE_UNION) continue;
            if (te->aliased_type->name.length != decl->body.struct_def.name.length) continue;
            if (memcmp(te->aliased_type->name.data, decl->body.struct_def.name.data,
                       te->aliased_type->name.length) != 0) continue;
            te->aliased_type->params = decl->body.struct_def.fields; break;
        }
    }
}

/* register one enumerator list into enum_vals (prepended, head = last).
 * A value expr that is not a literal (opt_enum folded what it could —
 * sizeof/_Alignof/ternary survive) is evaluated with ICE semantics. */
static void
register_enum_def(Arena* a, AST_Node* def, TypedefEntry** enum_vals,
                  HashMap* globals)
{
    int val = 0;
    for (AST_Node* en = def->body.enum_def.enumerators;
         en && en->type == AST_ENUMERATOR; en = en->next) {
        if (en->body.enumerator.value &&
            (en->body.enumerator.value->type == AST_INT_LIT ||
             en->body.enumerator.value->type == AST_LONG_LIT)) {
            val = (int)en->body.enumerator.value->body.literal.int_val;
        } else if (en->body.enumerator.value) {
            ICEVal iev;
            const char* why = NULL;

            resolve_enum_idents(en->body.enumerator.value, *enum_vals);
            if (!ice_eval(a, en->body.enumerator.value, &iev, &why,
                          globals) &&
                !iev.is_float && !iev.is_ptr)
                val = (int)iev.v;
            /* non-constant value expr: keep the previous value (the
             * compile will fail later if the enumerator is actually
             * used in a constant context) */
        }
        TypedefEntry* ev = arena_alloc(a, sizeof(TypedefEntry));
        ev->name = en->body.enumerator.name;
        ev->aliased_type = (Type*)(intptr_t)val;
        ev->next = *enum_vals; *enum_vals = ev;
        val++;
    }
}

typedef struct {
    Arena*        a;
    TypedefEntry** enum_vals;
    HashMap*      globals;
} EnumValCtx;

/* walker: register every enum definition found — top-level or nested in
 * a function body — so array sizes like `int arr[N]` (size_name lookup)
 * resolve for function-scope enum constants too. */
static int
collect_enum_cb(AST_Node* n, void* ctx)
{
    if (n->type == AST_ENUM_DEF) {
        EnumValCtx* c = (EnumValCtx*)ctx;
        register_enum_def(c->a, n, c->enum_vals, c->globals);
    }
    return 0;
}

TypedefEntry*
collect_enum_vals(Arena* a, AST_Node* root, HashMap* globals)
{
    TypedefEntry* enum_vals = NULL;
    EnumValCtx ctx = { a, &enum_vals, globals };

    for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next)
        ast_walk_single(decl, collect_enum_cb, NULL, &ctx);
    return enum_vals;
}

/* collect function signatures (ret + param list) for call lookup.  MUST
 * run after typedef resolution so struct return types resolve. */
void
collect_func_sigs(Arena* a, AST_Node* root, HashMap* sig_map)
{
    for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
        if (decl->type != AST_FUNC_DEF) continue;

        IR_Type* rt = ir_type_from_ast(a, decl->body.func_def.ret_type);
        IR_Type *params = NULL, **ptail = &params;

        for (AST_Node* p = decl->body.func_def.params;
             p && p->type == AST_PARAM_DECL; p = p->next) {
            IR_Type* pty = ir_type_from_ast(a, p->body.param_decl.param_type);
            /* C11 6.7.6.3p7: array parameters decay to a pointer to
             * their element type (typedef'd arrays like va_list arrive
             * here as IR_ARRAY and must decay, or the call passes the
             * array by value instead of by address). */
            if (pty && pty->kind == IR_ARRAY)
                pty = ir_ptr_type(a, pty->inner, 0);
            /* unresolved typedef param → ptr fallback (same fixup as
             * ir_gen_function's param handling) */
            if (pty && pty->kind == IR_I32) {
                Type* ast = p->body.param_decl.param_type;
                if (ast && ast->kind == TYPE_NAMED && !ast->inner)
                    pty = ir_ptr_type(a, t_i8, 0);
            }
            /* clone to avoid corrupting singletons when chaining */
            IR_Type* cp = arena_alloc(a, sizeof(IR_Type));
            memcpy(cp, pty ? pty : t_i32, sizeof(IR_Type));
            cp->next = NULL;
            *ptail = cp;
            ptail = &cp->next;
        }

        IR_Type* func_ty = ir_func_type(a, rt ? rt : t_void, params,
                                         decl->body.func_def.is_variadic);
        hashmap_put(sig_map, decl->body.func_def.name, func_ty);
    }
}
