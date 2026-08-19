/* ir_type_struct.c -- IR_Type -> AST Type mapping for struct/union types.
 * Used by AST_MEMBER resolution to find field indices by name. */

#include "ir.h"
#include "ir_type.h"

/* IR_Type → AST Type mapping for struct/union types.
 * Used by AST_MEMBER handler to find field indices by name.
 * Kept as a separate table (not a field in IR_Type) to avoid
 * bootstrapping issues when the compiler compiles itself. */
#define MAX_AST_MAP 4096
static IR_Type* ast_map_keys[MAX_AST_MAP];
static Type*    ast_map_vals[MAX_AST_MAP];
static int      n_ast_map = 0;

void register_struct_ast(IR_Type* ir, Type* ast)
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
void
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

void
ir_reset_ast_map(void)
{
    n_ast_map = 0;
}
