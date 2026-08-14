/* opt_enum.c -- AST-level enum constant resolution
 *
 * Replaces every AST_IDENT that names an enum member with an
 * AST_INT_LIT holding its integer value.  This matches how GCC
 * and Clang handle enums — resolution at name-binding time, so
 * IR gen only sees integer constants.
 */

#include "optimize.h"
#include "ast_walk.h"

#include <string.h>

/* ---------------------------------------------------------------
 *  Enum value table
 * --------------------------------------------------------------- */

#define MAX_ENUM 512

typedef struct {
    String name;
    int    value;
} EnumEntry;

/* ---------------------------------------------------------------
 *  Phase-2 pre-callback: replace AST_IDENT with AST_INT_LIT
 * --------------------------------------------------------------- */

static int replace_cb(AST_Node* n, void* ctx)
{
    EnumEntry* entries = (EnumEntry*)ctx;

    if (n->type != AST_IDENT) return 0;

    String* nm = &n->body.ident.name;

    for (EnumEntry* e = entries; e->name.data; e++) {
        if (e->name.length == nm->length &&
            memcmp(e->name.data, nm->data, nm->length) == 0) {
            n->type = AST_INT_LIT;
            n->body.literal.int_val = e->value;
            return 1;
        }
    }

    return 0;
}

/* ---------------------------------------------------------------
 *  Public API
 * --------------------------------------------------------------- */

int opt_enum(AST_Node* root)
{
    if (!root || root->type != AST_PROGRAM) return 0;

    /* Phase 1: collect enum constants from program decls */
    EnumEntry entries[MAX_ENUM + 1];
    memset(entries, 0, sizeof(entries));
    int n_entries = 0;

    for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
        if (decl->type != AST_ENUM_DEF) continue;

        int val = 0;
        for (AST_Node* en = decl->body.enum_def.enumerators;
             en && en->type == AST_ENUMERATOR; en = en->next) {

            if (en->body.enumerator.value &&
                en->body.enumerator.value->type == AST_INT_LIT)
                val = (int)en->body.enumerator.value->body.literal.int_val;

            int skip = 0;
            for (int i = 0; i < n_entries; i++) {
                if (entries[i].name.length == en->body.enumerator.name.length &&
                    memcmp(entries[i].name.data,
                           en->body.enumerator.name.data,
                           entries[i].name.length) == 0) {
                    skip = 1; break;
                }
            }

            if (!skip && n_entries < MAX_ENUM) {
                entries[n_entries].name  = en->body.enumerator.name;
                entries[n_entries].value = val;
                n_entries++;
            }

            val++;
        }
    }

    if (n_entries == 0) return 0;

    /* Phase 2: replace AST_IDENT in all decls */
    int changed = 0;
    for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next)
        changed |= ast_walk(decl, replace_cb, NULL, entries);

    return changed;
}

/* ---------------------------------------------------------------
 *  Designator-aware array size inference
 * --------------------------------------------------------------- */

/* `int a[] = {[i] = v}` sizes the array by element count in the parser,
 * which cannot resolve enum/const indices.  Run after enum + fold so
 * index_expr is an AST_INT_LIT; grow size_inferred arrays to cover the
 * largest pure-[i] (empty field_name) designator index. */
static int
size_bump_cb(AST_Node* n, void* ctx)
{
    (void)ctx;

    if (n->type != AST_VAR_DECL) return 0;

    Type* scan = n->body.var_decl.var_type;
    while (scan && scan->kind == TYPE_PTR)
        scan = scan->inner;
    if (!scan || scan->kind != TYPE_ARRAY || !scan->size_inferred)
        return 0;

    AST_Node* init = n->body.var_decl.init;
    if (!init || init->type != AST_INIT_LIST) return 0;

    long long maxi = scan->arr_size - 1;
    for (AST_Node* e = init->body.init_list.elems; e; e = e->next) {
        if (e->type != AST_DESIGNATOR) continue;
        if (!e->body.designator.is_index) continue;
        if (e->body.designator.field_name.data) continue;  /* .f[i] */
        AST_Node* ix = e->body.designator.index_expr;
        if (ix && ix->type == AST_INT_LIT && ix->body.literal.int_val > maxi)
            maxi = ix->body.literal.int_val;
    }
    if (maxi + 1 > scan->arr_size)
        scan->arr_size = (int)(maxi + 1);
    return 0;
}

int opt_designator_size(AST_Node* root)
{
    if (!root || root->type != AST_PROGRAM) return 0;
    return ast_walk(root, size_bump_cb, NULL, NULL);
}
