/* ir_type_struct_cache.c -- struct/union dedup cache backed by base/hash.c.
 * name_map: tag name -> newest entry (named structs only); ast_map: exact
 * Type* address bytes -> entry (named AND anonymous).  Every entry lands in
 * ast_map so the exact-AST-identity lookup is ordering-independent; the name
 * map's overwrite-on-equal-key is exactly "newest wins", which the
 * head-inserted arena list the map replaces also produced.  Reset zeroes both
 * maps; the zeroed arena field triggers lazy re-init on the next put. */

#include "ir.h"
#include "ir_type.h"
#include "hash.h"

#include <string.h>

typedef struct {
    IR_Type* ty;
    Type*    ast;   /* for the same-definition params guard */
} StructCacheVal;

static HashMap name_map;   /* struct tag name  -> StructCacheVal* (named) */
static HashMap ast_map;    /* &Type (ptr bytes) -> StructCacheVal* (all)  */
static int cache_enabled = 0;

/* Look up a struct/union.  Exact AST node identity wins unconditionally (the
 * entry is cached before members are built, so a cycle back into this very
 * node must return the in-flight type or it recurses forever).  Named structs
 * then dedup by tag, guarded exactly as the old scan: a complete entry or a
 * tag-only reference is accepted; an in-flight entry of the same definition
 * (references share its params pointer) is accepted; an incomplete
 * forward-decl must not shadow the later complete definition. */
IR_Type*
struct_cache_get_ty(Type* ast)
{
    StructCacheVal* v;
    String key;

    if (!cache_enabled) return NULL;

    key.data = (char*)&ast;
    key.length = (int)sizeof ast;
    v = (StructCacheVal*)hashmap_get(&ast_map, key);
    if (v) return v->ty;

    if (ast->name.data) {
        v = (StructCacheVal*)hashmap_get(&name_map, ast->name);
        if (v && (v->ty->members || !ast->params ||
                  v->ast->params == ast->params))
            return v->ty;
    }
    return NULL;
}

/* Cache a struct/union BEFORE its members are built so self-referencing
 * fields (e.g. Arena* prev inside struct Arena) hit the cache.  The ast_map
 * key is the pointer value persisted in the arena; the name_map key is the
 * tag String, which lives in the AST and outlives the map. */
void
struct_cache_put(Arena* a, Type* ast, IR_Type* t)
{
    StructCacheVal* v;
    char* blob;
    String key;

    if (!cache_enabled) return;

    v = arena_alloc(a, sizeof(StructCacheVal));
    v->ty = t;
    v->ast = ast;

    if (!ast_map.arena) hashmap_init(&ast_map, a, 128);
    blob = arena_alloc(a, sizeof ast);
    memcpy(blob, &ast, sizeof ast);
    key.data = blob;
    key.length = (int)sizeof ast;
    hashmap_put(&ast_map, key, v);

    if (ast->name.data) {
        if (!name_map.arena) hashmap_init(&name_map, a, 128);
        hashmap_put(&name_map, ast->name, v);
    }
}

void
ir_clear_struct_cache(void)
{
    cache_enabled = 1;
}

void
ir_reset_struct_caches(void)
{
    memset(&ast_map, 0, sizeof(ast_map));
    memset(&name_map, 0, sizeof(name_map));
    cache_enabled = 0;
}
