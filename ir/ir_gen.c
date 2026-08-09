/* ir_gen.c -- AST-to-IR walker: symbol table, function/module generation */

#include "ir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"

/* ---------------------------------------------------------------
 *  Function signature entry (for return type lookup in calls)
 * --------------------------------------------------------------- */

typedef struct FuncSig {
    String            name;
    IR_Type*          ret_type;
    struct FuncSig*   next;
} FuncSig;

/* ---------------------------------------------------------------
 *  Symbol table entry
 * --------------------------------------------------------------- */

typedef struct SymEntry {
    String        name;
    IR_Value*     alloca;
    struct SymEntry* next;
} SymEntry;

/* ---------------------------------------------------------------
 *  Generation context (per function)
 * --------------------------------------------------------------- */

typedef struct {
    IR_Builder*   b;
    SymEntry*     syms;
    FuncSig*      sigs;         /* function signature table for call ret types */
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
    for (SymEntry* e = ctx->syms; e; e = e->next) {
        if (e->name.length == name.length &&
            memcmp(e->name.data, name.data, name.length) == 0)
            return e->alloca;
    }
    return NULL;
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

IR_Type* func_type_lookup(FuncSig* sigs, String name)
{
    for (FuncSig* s = sigs; s; s = s->next) {
        if (s->name.length == name.length &&
            memcmp(s->name.data, name.data, name.length) == 0)
            return s->ret_type;
    }
    return NULL;
}

void sym_add(GenCtx* ctx, String name, IR_Value* alloca)
{
    SymEntry* e = calloc(1, sizeof(SymEntry));
    e->name = name;
    e->alloca = alloca;
    e->next = ctx->syms;
    ctx->syms = e;
}

/* ---------------------------------------------------------------
 *  Forward declarations from other sub-files
 * --------------------------------------------------------------- */

extern IR_Value* gen_expr(GenCtx* ctx, AST_Node* n);
extern void      gen_stmt(GenCtx* ctx, AST_Node* n);

/* ---------------------------------------------------------------
 *  Function generation
 * --------------------------------------------------------------- */

IR_Func*
ir_gen_function(IR_Module* mod, AST_Node* func_def, int is_device, FuncSig* sigs)
{
    AST_Node*   fd = func_def;
    IR_Builder* b = ir_builder_new(mod);
    GenCtx      ctx = {b, NULL, sigs, NULL, NULL, NULL, mod, is_device};
    IR_Func*    func = calloc(1, sizeof(IR_Func));

    func->name = fd->body.func_def.name;
    func->ret_type = ir_type_from_ast(fd->body.func_def.ret_type);

    if (!func->ret_type)
        func->ret_type = t_void;

    ctx.ret_type = func->ret_type;

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
    func->params = calloc(n, sizeof(IR_Value*));

    for (int i = 0; i < n; i++) {
        func->params[i] = calloc(1, sizeof(IR_Value));
        func->params[i]->kind = VAL_PARAM;
        func->params[i]->type = t_i32;
        func->params[i]->id = b->next_vreg_id++;
    }

    b->cur_func = func;

    /* create entry block first */
    IR_Block* entry = ir_builder_new_block(b, "entry");
    func->blocks = entry;
    ir_builder_set_block(b, entry);

    /* emit allocas and stores for params */
    {
        int i = 0;
        for (AST_Node* p = fd->body.func_def.params; p; p = p->next, i++) {
            IR_Type* pty = ir_type_from_ast(p->body.param_decl.param_type);
            /* if resolved type is i32 but AST type is a named typedef
             * (e.g. unresolved typedef for function pointer or struct),
             * default to ptr — typedefs aren't resolved at parse time. */
            if (pty && pty->kind == IR_I32) {
                Type* ast = p->body.param_decl.param_type;
                if (ast && ast->kind == TYPE_NAMED)
                    pty = ir_ptr_type(t_i8, 0);
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

    /* ensure terminator on the last block, and add unreachable to empty blocks */
    {
        IR_Block* blk = func->blocks;
        while (blk) {
            if (!blk->first) {
                /* empty block: add unreachable */
                ir_builder_set_block(b, blk);
                ir_build_unreachable(b);
            } else {
                IR_Instr* term = blk->last;

                if (!term || (term->opcode != IROP_RET &&
                              term->opcode != IROP_BR &&
                              term->opcode != IROP_COND_BR &&
                              term->opcode != IROP_UNREACHABLE)) {
                    ir_builder_set_block(b, blk);

                    if (func->ret_type && func->ret_type->kind != IR_VOID) {
                        IR_Value* undef = calloc(1, sizeof(IR_Value));
                        undef->kind = VAL_UNDEF;
                        undef->type = func->ret_type;
                        ir_build_ret(b, undef);
                    } else {
                        ir_build_ret(b, NULL);
                    }
                }
            }
            blk = blk->next;
        }
    }

    /* append to module */
    {
        IR_Func** tail = &mod->funcs;
        while (*tail) tail = &(*tail)->next;
        *tail = func;
    }

    ir_builder_free(b);
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

    IR_Module* mod = calloc(1, sizeof(IR_Module));
    mod->addr_space = is_device ? 1 : 0;
    mod->target_triple = is_device ? "spir64-unknown-unknown"
                                   : "x86_64-unknown-linux-gnu";
    mod->data_layout = "e-i64:64-v16:16-v24:32-v32:32-v48:64-v96:128-v192:256-v256:256-v512:512-v1024:1024";

    /* pass 0: collect function signatures for call return type lookup */
    FuncSig* sigs = NULL;
    for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
        if (decl->type != AST_FUNC_DEF) continue;

        FuncSig* s = calloc(1, sizeof(FuncSig));
        s->name = decl->body.func_def.name;
        s->ret_type = ir_type_from_ast(decl->body.func_def.ret_type);
        if (!s->ret_type) s->ret_type = t_void;
        s->next = sigs;
        sigs = s;
    }

    /* first pass: collect global variables (both extern decls and definitions) */
    for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
        if (decl->type != AST_VAR_DECL) continue;

        /* dedup by name */
        {   int dup = 0;
            for (IR_Value* g = mod->globals; g; g = g->next) {
                if (g->name.length != decl->body.var_decl.name.length) continue;
                if (memcmp(g->name.data, decl->body.var_decl.name.data,
                           g->name.length) != 0) continue;
                dup = 1;
                /* upgrade extern → definition if init available
                 * or tentative definition (linkage==0, no extern keyword) */
                if (!g->body.init_val &&
                    (decl->body.var_decl.init || decl->body.var_decl.linkage == 0)) {
                    IR_Value* init = calloc(1, sizeof(IR_Value));
                    if (g->type->kind == IR_PTR) {
                        init->kind = VAL_CONST_NULL;
                    } else {
                        init->kind = VAL_CONST_INT;
                    }
                    init->type = g->type;
                    g->body.init_val = init;
                }
                break;
            }
            if (dup) continue;
        }

        IR_Value* gv = calloc(1, sizeof(IR_Value));
        gv->kind = VAL_GLOBAL;
        gv->name = decl->body.var_decl.name;
        gv->type = ir_type_from_ast(decl->body.var_decl.var_type);

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
                IR_Type* leaf = ir_ptr_type(t_i8, 0);
                IR_Type* arr = leaf;
                for (int d = 0; d < depth; d++)
                    arr = ir_array_type(arr, 0);
                gv->type = arr;
            }
        }

        if (!gv->type || gv->type->kind == IR_VOID)
            gv->type = t_i8;

        /* set linkage for global: 0=internal(static), 1=external */
        gv->linkage = (decl->body.var_decl.linkage == 4) ? 0 : 1;

        if (decl->body.var_decl.init) {
            IR_Value* init = calloc(1, sizeof(IR_Value));
            if (gv->type->kind == IR_PTR) {
                init->kind = VAL_CONST_NULL;
            } else {
                init->kind = VAL_CONST_INT;
            }
            init->type = gv->type;
            gv->body.init_val = init;
        } else if (decl->body.var_decl.linkage != 5) {
            /* not extern: tentative definition or static → zero-initialize */
            IR_Value* init = calloc(1, sizeof(IR_Value));
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
    }

    /* second pass: function definitions */
    for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
        if (decl->type == AST_FUNC_DEF && decl->body.func_def.body)
            ir_gen_function(mod, decl, is_device, sigs);
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
