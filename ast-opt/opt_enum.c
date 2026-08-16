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

/* from ast-opt/fold/opt_fold_cast.c -- fold a (T)int_literal cast */
extern int try_fold_cast(AST_Node* n);

/* ---------------------------------------------------------------
 *  Enum value table
 * --------------------------------------------------------------- */

#define MAX_ENUM 512

typedef struct {
    String name;
    int    value;
} EnumEntry;

typedef struct {
    EnumEntry* entries;
    int*       n_entries;
} EnumCtx;

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
 *  Phase-1 collector -- register one enum definition's enumerators
 * --------------------------------------------------------------- */

static void
collect_enum_def(AST_Node* def, EnumCtx* ctx)
{
    EnumEntry* entries = ctx->entries;
    int* n_entries = ctx->n_entries;
    int  val = 0;

    for (AST_Node* en = def->body.enum_def.enumerators;
         en && en->type == AST_ENUMERATOR; en = en->next) {

        AST_Node* value = en->body.enumerator.value;
        if (value) {
            /* resolve refs to earlier enumerators, then fold the
             * expression so binary/unary/cast values become a literal */
            ast_walk(value, replace_cb, NULL, entries);
            opt_fold(value);
            try_fold_cast(value);
            if (is_int_literal_kind(value->type))
                val = (int)value->body.literal.int_val;
        }

        int skip = 0;
        for (int i = 0; i < *n_entries; i++) {
            if (entries[i].name.length == en->body.enumerator.name.length &&
                memcmp(entries[i].name.data,
                       en->body.enumerator.name.data,
                       entries[i].name.length) == 0) {
                skip = 1; break;
            }
        }

        if (!skip && *n_entries < MAX_ENUM) {
            entries[*n_entries].name  = en->body.enumerator.name;
            entries[*n_entries].value = val;
            (*n_entries)++;
        }

        val++;
    }
}

/* walker: collect any enum definition found — top-level or nested in a
 * function body (function-scope enums are legal C and were previously
 * ignored, leaving their constants as unresolved AST_IDENTs). */
static int
collect_enum_cb(AST_Node* n, void* ctx)
{
    if (n->type == AST_ENUM_DEF)
        collect_enum_def(n, (EnumCtx*)ctx);
    return 0;
}

/* ---------------------------------------------------------------
 *  Public API
 * --------------------------------------------------------------- */

int opt_enum(AST_Node* root)
{
    if (!root || root->type != AST_PROGRAM) return 0;

    /* Phase 1: collect enum constants from every enum definition.
     * ast_walk visits top-level decls in order and descends into
     * function bodies, so later enumerators can reference earlier
     * ones regardless of scope. */
    EnumEntry entries[MAX_ENUM + 1];
    memset(entries, 0, sizeof(entries));
    int n_entries = 0;
    EnumCtx ctx = { entries, &n_entries };

    for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next)
        ast_walk(decl, collect_enum_cb, NULL, &ctx);

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
        AST_Node* s0 = e->body.designator.steps;
        /* only a single top-level [i] step (no .field) sizes the array */
        if (!s0 || s0->next || s0->body.desig_step.field_name.data)
            continue;
        AST_Node* ix = s0->body.desig_step.index_expr;
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
