/* ir_gen.c -- AST-to-IR walker: symbol table, function/module generation */

#include "ir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "hash.h"

/* ---------------------------------------------------------------
 *  Typedef table entry (for resolving TYPE_NAMED during IR gen)
 * --------------------------------------------------------------- */

typedef struct TypedefEntry {
    String               name;
    Type*                aliased_type;
    struct TypedefEntry* next;
} TypedefEntry;

/* ---------------------------------------------------------------
 *  Generation context (per function)
 * --------------------------------------------------------------- */

typedef struct {
    IR_Builder*   b;
    HashMap       syms;         /* local variables: name → IR_Value* (alloca) */
    HashMap*      sig_map;      /* module-level: func name → IR_Type* (ret type) */
    IR_Block*     break_blk;    /* target for break */
    IR_Block*     cont_blk;     /* target for continue */
    IR_Type*      ret_type;     /* enclosing function return type */
    IR_Module*    mod;          /* for global variable lookup */
    int           is_device;    /* 1 = device IR gen (CUDA builtins), 0 = host */
} GenCtx;

/* ---------------------------------------------------------------
 *  Symbol table ops
 * --------------------------------------------------------------- */

IR_Value* sym_lookup(GenCtx* ctx, String name)
{
    return hashmap_get(&ctx->syms, name);
}

IR_Value* global_lookup(IR_Module* mod, String name)
{
    if (!mod) return NULL;

    for (IR_Value* g = mod->globals; g; g = g->next) {
        if (g->name.length == name.length &&
            memcmp(g->name.data, name.data, name.length) == 0)
            return g;
    }
    return NULL;
}

IR_Type* func_type_lookup(HashMap* sig_map, String name)
{
    return hashmap_get(sig_map, name);
}

void sym_add(GenCtx* ctx, String name, IR_Value* alloca)
{
    hashmap_put(&ctx->syms, name, alloca);
}

/* ---------------------------------------------------------------
 *  Typedef & enum table: lookup, type tree resolution
 * --------------------------------------------------------------- */

static Type* typedef_lookup(TypedefEntry* table, String name)
{
    for (TypedefEntry* te = table; te; te = te->next)
        if (te->name.length == name.length &&
            memcmp(te->name.data, name.data, name.length) == 0)
            return te->aliased_type;
    return NULL;
}

/* resolve_fields: if 1, enter struct/union field lists (AST_VAR_DECL).
 * dropped to 0 when crossing TYPE_PTR — pointers act as a firewall
 * so structs reached through pointers don't have their fields iterated.
 * TYPE_NAMED that resolves directly to TYPE_STRUCT preserves the flag. */
static void resolve_type_tree_ex(Type* t, TypedefEntry* table, int resolve_fields)
{
    if (!t) return;
    if (t->kind == TYPE_NAMED && !t->inner) {
        Type* resolved = typedef_lookup(table, t->name);
        if (resolved) t->inner = resolved;
    }
    /* TYPE_PTR is the firewall: what it points to doesn't enter struct fields */
    resolve_type_tree_ex(t->inner, table,
                         t->kind == TYPE_PTR ? 0 : resolve_fields);
    resolve_type_tree_ex(t->next, table, resolve_fields);
    for (AST_Node* p = t->params; p; p = p->next) {
        Type* ft = NULL;
        if (p->type == AST_PARAM_DECL)
            ft = p->body.param_decl.param_type;
        else if (resolve_fields && p->type == AST_VAR_DECL)
            ft = p->body.var_decl.var_type;
        if (ft) resolve_type_tree_ex(ft, table, 1);
    }
}

static void resolve_type_tree(Type* t, TypedefEntry* table)
{
    resolve_type_tree_ex(t, table, 1);
}

static void resolve_expr_types(AST_Node* e, TypedefEntry* table);
static void resolve_array_sizes(Type* t, TypedefEntry* enum_vals);

static void resolve_expr_types(AST_Node* e, TypedefEntry* table)
{
    if (!e) return;
    switch (e->type) {
    case AST_CAST:
        resolve_type_tree(e->body.cast.type_expr, table);
        resolve_expr_types(e->body.cast.cast_expr, table); break;
    case AST_SIZEOF_TYPE:
        resolve_type_tree(e->body.sizeof_type.type_expr, table); break;
    case AST_SIZEOF_EXPR:
        /* If the operand is an identifier that is a typedef name
         * (e.g. sizeof(PPCtx) where PPCtx is a typedef), convert
         * this to sizeof(type) so the correct size is computed.
         * The LR parser cannot distinguish typedef names from
         * variable names, so this is resolved here. */
        if (e->body.sizeof_expr.expr &&
            e->body.sizeof_expr.expr->type == AST_IDENT) {
            Type* rt = typedef_lookup(table,
                         e->body.sizeof_expr.expr->body.ident.name);
            if (rt) {
                e->type = AST_SIZEOF_TYPE;
                e->body.sizeof_type.type_expr = rt;
                resolve_type_tree(rt, table);
                break;  /* node is now AST_SIZEOF_TYPE — do NOT
                         * fall through to sizeof_expr path! */
            }
        }
        resolve_expr_types(e->body.sizeof_expr.expr, table); break;
    case AST_BINARY:
        resolve_expr_types(e->body.binary.left, table);
        resolve_expr_types(e->body.binary.right, table); break;
    case AST_UNARY:
        resolve_expr_types(e->body.unary.operand, table); break;
    case AST_POSTFIX:
        resolve_expr_types(e->body.postfix.operand, table); break;
    case AST_TERNARY:
        resolve_expr_types(e->body.ternary.cond, table);
        resolve_expr_types(e->body.ternary.then_expr, table);
        resolve_expr_types(e->body.ternary.else_expr, table); break;
    case AST_CALL:
        resolve_expr_types(e->body.call.callee, table);
        for (AST_Node* a = e->body.call.args;
             a && a->type != AST_CALL; a = a->next)
            resolve_expr_types(a, table);
        break;
    case AST_INDEX:
        resolve_expr_types(e->body.subscript.array, table);
        resolve_expr_types(e->body.subscript.index, table); break;
    default: break;
    }
}

static void resolve_array_sizes(Type* t, TypedefEntry* enum_vals)
{
    if (!t) return;
    resolve_array_sizes(t->inner, enum_vals);
    resolve_array_sizes(t->next, enum_vals);
    if (t->kind == TYPE_ARRAY && t->arr_size == 0 &&
        t->size_name.data && enum_vals) {
        Type* found = typedef_lookup(enum_vals, t->size_name);
        if (found) t->arr_size = (int)(intptr_t)found;
    }
}

/* struct definition entry for resolving TYPE_STRUCT references */
typedef struct StructDefEntry {
    String               name;
    AST_Node*            fields;
    int                  is_union;
    struct StructDefEntry* next;
} StructDefEntry;

static void resolve_struct_refs_type(Type* t, HashMap* struct_map)
{
    if (!t) return;
    if ((t->kind == TYPE_STRUCT || t->kind == TYPE_UNION) &&
        t->name.data && !t->params) {
        StructDefEntry* se = hashmap_get(struct_map, t->name);
        if (se) {
            t->params = se->fields;
            if (se->is_union) t->kind = TYPE_UNION;
        }
    }
    resolve_struct_refs_type(t->inner, struct_map);
    resolve_struct_refs_type(t->next, struct_map);
    if (t->kind == TYPE_FUNC)
        for (AST_Node* p = t->params;
             p && p->type == AST_PARAM_DECL; p = p->next)
            resolve_struct_refs_type(p->body.param_decl.param_type, struct_map);
}

#define MAX_VISITED 128
static int was_visited(AST_Node** v, int n, AST_Node* node)
{
    for (int i = 0; i < n; i++) if (v[i] == node) return 1;
    return 0;
}

static void resolve_ast_node(AST_Node* n, TypedefEntry* table);

/* statement node types: nodes that can appear in a block stmt chain */
static int is_stmt_type(AST_Type t)
{
    return t == AST_BLOCK || t == AST_IF || t == AST_WHILE ||
           t == AST_DO_WHILE || t == AST_FOR || t == AST_RETURN ||
           t == AST_BREAK || t == AST_CONTINUE || t == AST_SWITCH ||
           t == AST_CASE || t == AST_DEFAULT || t == AST_GOTO ||
           t == AST_LABEL || t == AST_EXPR_STMT || t == AST_VAR_DECL;
}

static void resolve_stmt_chain(AST_Node* first, TypedefEntry* table,
                               AST_Node** visited, int* n_visited)
{
    for (AST_Node* s = first;
         s && is_stmt_type(s->type) && *n_visited < MAX_VISITED;
         s = s->next) {
        if (was_visited(visited, *n_visited, s)) return;
        visited[(*n_visited)++] = s;
        resolve_ast_node(s, table);
    }
}

static void resolve_ast_node(AST_Node* n, TypedefEntry* table)
{
    if (!n) return;
    switch (n->type) {
    case AST_VAR_DECL:
        resolve_type_tree(n->body.var_decl.var_type, table);
        if (n->body.var_decl.init)
            resolve_expr_types(n->body.var_decl.init, table);
        break;
    case AST_STRUCT_DEF:
    case AST_UNION_DEF:
        for (AST_Node* f = n->body.struct_def.fields; f; f = f->next)
            resolve_type_tree(f->body.var_decl.var_type, table);
        break;
    case AST_TYPEDEF:
        resolve_type_tree(n->body.typedef_decl.aliased_type, table);
        break;
    case AST_FUNC_DEF:
        resolve_type_tree(n->body.func_def.ret_type, table);
        for (AST_Node* p = n->body.func_def.params;
             p && p->type == AST_PARAM_DECL; p = p->next)
            resolve_type_tree(p->body.param_decl.param_type, table);
        if (n->body.func_def.body)
            resolve_ast_node(n->body.func_def.body, table);
        break;
    case AST_BLOCK:
    { AST_Node* vb[MAX_VISITED]; int nv = 0;
      resolve_stmt_chain(n->body.block.stmts, table, vb, &nv); break; }
    case AST_IF:
        resolve_expr_types(n->body.if_stmt.condition, table);
        resolve_ast_node(n->body.if_stmt.then_branch, table);
        resolve_ast_node(n->body.if_stmt.else_branch, table); break;
    case AST_WHILE: case AST_DO_WHILE:
        resolve_expr_types(n->body.loop.condition, table);
        resolve_ast_node(n->body.loop.body, table); break;
    case AST_FOR:
        resolve_ast_node(n->body.for_stmt.init, table);
        resolve_expr_types(n->body.for_stmt.condition, table);
        resolve_expr_types(n->body.for_stmt.update, table);
        resolve_ast_node(n->body.for_stmt.body, table); break;
    case AST_RETURN:
        resolve_expr_types(n->body.ret.expr, table); break;
    case AST_EXPR_STMT:
        resolve_expr_types(n->body.expr_stmt.expr, table); break;
    case AST_SWITCH:
        resolve_expr_types(n->body.switch_stmt.condition, table);
        resolve_ast_node(n->body.switch_stmt.body, table); break;
    case AST_CASE: case AST_DEFAULT:
        resolve_expr_types(n->body.case_stmt.value, table);
        resolve_ast_node(n->body.case_stmt.stmt, table); break;
    default: break;
    }
}
#undef MAX_VISITED

/* ---------------------------------------------------------------
 *  Forward declarations from other sub-files
 * --------------------------------------------------------------- */

extern IR_Value* gen_expr(GenCtx* ctx, AST_Node* n);
extern void      gen_stmt(GenCtx* ctx, AST_Node* n);

/* ---------------------------------------------------------------
 *  Function generation
 * --------------------------------------------------------------- */

IR_Func*
ir_gen_function(IR_Module* mod, AST_Node* func_def, int is_device, HashMap* sig_map)
{
    AST_Node*   fd = func_def;
    Arena*      a = mod->arena;
    IR_Builder* b = ir_builder_new(mod, a);
    GenCtx      ctx;

    hashmap_init(&ctx.syms, a, 32);
    ctx.b = b;
    ctx.sig_map = sig_map;
    ctx.break_blk = NULL;
    ctx.cont_blk = NULL;
    ctx.ret_type = NULL;
    ctx.mod = mod;
    ctx.is_device = is_device;
    IR_Func*    func = arena_alloc(a, sizeof(IR_Func));

    func->name = fd->body.func_def.name;
    func->ret_type = ir_type_from_ast(a, fd->body.func_def.ret_type);

    if (!func->ret_type)
        func->ret_type = t_void;

    ctx.ret_type = func->ret_type;

    func->is_constructor = fd->body.func_def.is_constructor;

    /* map AST linkage to IR_Linkage */
    switch (fd->body.func_def.linkage) {
    case 2: func->linkage = LINK_KERNEL;   break;  /* LINK_GLOBAL */
    case 1: func->linkage = LINK_DEVICE;   break;  /* LINK_DEVICE */
    case 4: func->linkage = LINK_INTERNAL; break;  /* static */
    default: func->linkage = LINK_EXTERNAL; break; /* host */
    }

    /* count and create params */
    int n = 0;
    for (AST_Node* p = fd->body.func_def.params; p; p = p->next) n++;

    func->n_params = n;
    func->params = arena_alloc(a, n * sizeof(IR_Value*));

    for (int i = 0; i < n; i++) {
        func->params[i] = arena_alloc(a, sizeof(IR_Value));
        func->params[i]->kind = VAL_PARAM;
        func->params[i]->type = t_i32;
        func->params[i]->id = b->next_vreg_id++;
    }

    b->cur_func = func;

    /* create entry block first */
    IR_Block* entry = ir_builder_new_block(b, "entry");
    func->blocks = entry;
    func->last_block = entry;
    b->entry_block = entry;
    ir_builder_set_block(b, entry);

    /* emit allocas and stores for params */
    {
        int i = 0;
        for (AST_Node* p = fd->body.func_def.params; p; p = p->next, i++) {
            IR_Type* pty = ir_type_from_ast(a, p->body.param_decl.param_type);
            /* if resolved type is i32 but AST type is a named typedef
             * (e.g. unresolved typedef for function pointer or struct),
             * default to ptr — typedefs aren't resolved at parse time. */
            if (pty && pty->kind == IR_I32) {
                Type* ast = p->body.param_decl.param_type;
                if (ast && ast->kind == TYPE_NAMED && !ast->inner)
                    pty = ir_ptr_type(a, t_i8, 0);
            }
            func->params[i]->type = pty ? pty : t_i32;

            IR_Value* alloca = ir_build_alloca(b, func->params[i]->type);
            sym_add(&ctx, p->body.param_decl.name, alloca);
            ir_build_store(b, func->params[i], alloca);
        }
    }

    /* generate body */
    if (fd->body.func_def.body)
        gen_stmt(&ctx, fd->body.func_def.body);

    /* ensure every block has a proper terminator.
     * empty blocks (e.g. merge blocks after if-without-else) get a ret;
     * blocks whose last instruction is not a terminator get a ret;
     * blocks that already have a terminator are left alone. */
    {
        IR_Block* blk = func->blocks;
        while (blk) {
            IR_Instr* term = blk->last;

            if (!term || (term->opcode != IROP_RET &&
                          term->opcode != IROP_BR &&
                          term->opcode != IROP_COND_BR &&
                          term->opcode != IROP_UNREACHABLE)) {
                ir_builder_set_block(b, blk);

                if (func->ret_type && func->ret_type->kind != IR_VOID) {
                    IR_Value* undef = arena_alloc(a, sizeof(IR_Value));
                    undef->kind = VAL_UNDEF;
                    undef->type = func->ret_type;
                    ir_build_ret(b, undef);
                } else {
                    ir_build_ret(b, NULL);
                }
            }
            blk = blk->next;
        }
    }

    /* append to module */
    if (mod->last_func)
        mod->last_func->next = func;
    else
        mod->funcs = func;
    mod->last_func = func;

    return func;
}

/* ---------------------------------------------------------------
 *  Module generation (top-level entry)
 * --------------------------------------------------------------- */

IR_Module*
ir_gen_module_ex(AST_Node* root, int is_device)
{
    if (!root || root->type != AST_PROGRAM)
        return NULL;

    Arena*     a = arena_new();
    IR_Module* mod = arena_alloc(a, sizeof(IR_Module));
    mod->arena = a;
    mod->addr_space = is_device ? 1 : 0;
    mod->target_triple = is_device ? "spir64-unknown-unknown"
                                   : "x86_64-unknown-linux-gnu";
    mod->data_layout = "e-i64:64-v16:16-v24:32-v32:32-v48:64-v96:128-v192:256-v256:256-v512:512-v1024:1024";

    /* reset static caches so no IR_Type* from a previous module's arena
     * leaks into this one (critical for CUDA host→device dual gen) */
    ir_reset_type_caches();

    /* pass 0.5: collect typedefs + enum constants, resolve throughout AST */
    {
        TypedefEntry *typedefs = NULL, *enum_vals = NULL;

        /* collect typedefs */
        for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
            if (decl->type != AST_TYPEDEF) continue;
            TypedefEntry* te = arena_alloc(a, sizeof(TypedefEntry));
            te->name = decl->body.typedef_decl.name;
            te->aliased_type = decl->body.typedef_decl.aliased_type;
            te->next = typedefs; typedefs = te;
        }

        /* update opaque typedefs from struct/union definitions */
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

        /* build enum constant table */
        for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
            if (decl->type != AST_ENUM_DEF) continue;
            int val = 0;
            for (AST_Node* en = decl->body.enum_def.enumerators;
                 en && en->type == AST_ENUMERATOR; en = en->next) {
                if (en->body.enumerator.value &&
                    en->body.enumerator.value->type == AST_INT_LIT)
                    val = (int)en->body.enumerator.value->body.literal.int_val;
                TypedefEntry* ev = arena_alloc(a, sizeof(TypedefEntry));
                ev->name = en->body.enumerator.name;
                ev->aliased_type = (Type*)(intptr_t)val;
                ev->next = enum_vals; enum_vals = ev;
                val++;
            }
        }

        /* resolve typedefs in all decl type trees */
        for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next)
            resolve_ast_node(decl, typedefs);

        /* resolve struct references: TYPE_STRUCT with name but no params
         * needs to find the AST_STRUCT_DEF and attach fields */
        {
            HashMap struct_map;

            hashmap_init(&struct_map, a, 32);

            for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
                if (decl->type != AST_STRUCT_DEF && decl->type != AST_UNION_DEF) continue;
                if (!decl->body.struct_def.name.data) continue;
                StructDefEntry* se = arena_alloc(a, sizeof(StructDefEntry));
                se->name = decl->body.struct_def.name;
                se->fields = decl->body.struct_def.fields;
                se->is_union = (decl->type == AST_UNION_DEF);
                hashmap_put(&struct_map, se->name, se);
            }

            /* resolve TYPE_STRUCT with missing params */
            for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
                if (decl->type == AST_VAR_DECL)
                    resolve_struct_refs_type(decl->body.var_decl.var_type, &struct_map);
                else if (decl->type == AST_FUNC_DEF) {
                    resolve_struct_refs_type(decl->body.func_def.ret_type, &struct_map);
                    for (AST_Node* p = decl->body.func_def.params;
                         p && p->type == AST_PARAM_DECL; p = p->next)
                        resolve_struct_refs_type(p->body.param_decl.param_type, &struct_map);
                } else if (decl->type == AST_TYPEDEF)
                    resolve_struct_refs_type(decl->body.typedef_decl.aliased_type, &struct_map);
            }
        }

        /* resolve array sizes from enum constants */
        for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
            if (decl->type == AST_VAR_DECL)
                resolve_array_sizes(decl->body.var_decl.var_type, enum_vals);
            else if (decl->type == AST_FUNC_DEF) {
                resolve_array_sizes(decl->body.func_def.ret_type, enum_vals);
                for (AST_Node* p = decl->body.func_def.params;
                     p && p->type == AST_PARAM_DECL; p = p->next)
                    resolve_array_sizes(p->body.param_decl.param_type, enum_vals);
            }
        }
    }

    /* enable struct type dedup cache — typedefs are now resolved,
     * so subsequent ir_type_from_ast() calls get consistent IR_Type* */
    ir_clear_struct_cache();

    /* collect function signatures for call return type lookup.
     * MUST run after typedef resolution so struct return types resolve. */
    HashMap sig_map;

    hashmap_init(&sig_map, a, 64);
    for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
        if (decl->type != AST_FUNC_DEF) continue;

        IR_Type* rt = ir_type_from_ast(a, decl->body.func_def.ret_type);
        hashmap_put(&sig_map, decl->body.func_def.name,
                    rt ? rt : t_void);
    }

    /* first pass: collect global variables (both extern decls and definitions) */
    {
        HashMap global_map;

        hashmap_init(&global_map, a, 64);

        for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
            if (decl->type != AST_VAR_DECL) continue;

            /* dedup by name using HashMap (O(1) vs O(n) list scan) */
            IR_Value* existing = hashmap_get(&global_map,
                                             decl->body.var_decl.name);
            if (existing) {
                /* upgrade extern → definition if init available
                 * or tentative definition (linkage==0, no extern keyword) */
                if (!existing->body.init_val &&
                    (decl->body.var_decl.init || decl->body.var_decl.linkage == 0)) {
                    IR_Value* init = arena_alloc(a, sizeof(IR_Value));
                    if (existing->type->kind == IR_PTR) {
                        init->kind = VAL_CONST_NULL;
                    } else {
                        init->kind = VAL_CONST_INT;
                    }
                    init->type = existing->type;
                    existing->body.init_val = init;
                }
                continue;
            }

        IR_Value* gv = arena_alloc(a, sizeof(IR_Value));
        gv->kind = VAL_GLOBAL;
        gv->name = decl->body.var_decl.name;
        gv->type = ir_type_from_ast(a, decl->body.var_decl.var_type);

        /* fix up: if the IR type is an array-of-i32 but the AST element
         * type is a named typedef (likely fn ptr), use ptr elements */
        if (gv->type && gv->type->kind == IR_ARRAY) {
            Type* ast = decl->body.var_decl.var_type;
            Type* inner = ast;
            int depth = 0;
            while (inner && inner->kind == TYPE_ARRAY) {
                depth++;
                inner = inner->inner;
            }
            if (inner && inner->kind == TYPE_NAMED && !inner->inner) {
                /* typedef not resolved — assume pointer-sized element,
                 * rebuild the array type chain with ptr as leaf */
                IR_Type* leaf = ir_ptr_type(a, t_i8, 0);
                IR_Type* arr = leaf;
                for (int d = 0; d < depth; d++)
                    arr = ir_array_type(a, arr, 0);
                gv->type = arr;
            }
        }

        if (!gv->type || gv->type->kind == IR_VOID)
            gv->type = t_i8;

        /* set linkage for global: 0=internal(static), 1=external */
        gv->linkage = (decl->body.var_decl.linkage == 4) ? 0 : 1;

        if (decl->body.var_decl.init) {
            IR_Value* init = arena_alloc(a, sizeof(IR_Value));
            if (gv->type->kind == IR_PTR) {
                init->kind = VAL_CONST_NULL;
            } else {
                init->kind = VAL_CONST_INT;
            }
            init->type = gv->type;
            gv->body.init_val = init;
        } else if (decl->body.var_decl.linkage != 5) {
            /* not extern: tentative definition or static → zero-initialize */
            IR_Value* init = arena_alloc(a, sizeof(IR_Value));
            if (gv->type->kind == IR_PTR) {
                init->kind = VAL_CONST_NULL;
            } else {
                init->kind = VAL_CONST_INT;
            }
            init->type = gv->type;
            gv->body.init_val = init;
        }
        /* else: extern decl → init_val stays NULL → emitted as external */

        gv->next = mod->globals;
        mod->globals = gv;
        hashmap_put(&global_map, gv->name, gv);
    }
    }

    /* second pass: function definitions */
    for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
        if (decl->type == AST_FUNC_DEF && decl->body.func_def.body)
            ir_gen_function(mod, decl, is_device, &sig_map);
    }

    return mod;
}

IR_Module*
ir_gen_module(AST_Node* root)
{
    return ir_gen_module_ex(root, 0);
}

IR_Module*
ir_gen_program(AST_Node* root)
{
    return ir_gen_module_ex(root, 0);
}
