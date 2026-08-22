/* ir_gen_func.c -- per-function AST-to-IR lowering (ir_gen_function) */

#include "ir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "ir_gen.h"

/* count params, create the VAL_PARAM list, and emit allocas + stores
 * that copy each argument into a named local in the entry block. */
static void
gen_func_params(GenCtx* ctx, IR_Builder* b, IR_Func* func, AST_Node* fd,
                Arena* a)
{
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

    {
        int i = 0;
        for (AST_Node* p = fd->body.func_def.params; p; p = p->next, i++) {
            IR_Type* pty = ir_type_from_ast(a, p->body.param_decl.param_type);
            /* C11 6.7.6.3p7: array parameters decay to a pointer to
             * their element type (typedef'd arrays like va_list arrive
             * here as IR_ARRAY and must decay, or the definition takes
             * the array by value and mismatches every call site). */
            if (pty && pty->kind == IR_ARRAY)
                pty = ir_ptr_type(a, pty->inner, 0);
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
            sym_add(ctx, p->body.param_decl.name, alloca);
            ir_build_store(b, func->params[i], alloca);
        }
    }
}

/* ensure every block ends with a proper terminator: empty blocks (e.g.
 * merge blocks after if-without-else) and blocks whose last instruction
 * is not a terminator get a ret; already-terminated blocks are left
 * alone. */
static void
gen_func_terminators(IR_Builder* b, IR_Func* func)
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
                ir_build_ret(b, gen_undef(b, func->ret_type));
            } else {
                ir_build_ret(b, NULL);
            }
        }
        blk = blk->next;
    }
}

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
    hashmap_init(&ctx.labels, a, 32);
    ctx.b = b;
    ctx.sig_map = sig_map;
    ctx.break_blk = NULL;
    ctx.cont_blk = NULL;
    ctx.ret_type = NULL;
    ctx.mod = mod;
    ctx.is_device = is_device;
    ctx.scope_top = NULL;
    IR_Func*    func = arena_alloc(a, sizeof(IR_Func));

    func->name = fd->body.func_def.name;
    func->ret_type = ir_type_from_ast(a, fd->body.func_def.ret_type);

    if (!func->ret_type)
        func->ret_type = t_void;

    ctx.ret_type = func->ret_type;

    func->is_constructor = fd->body.func_def.is_constructor;
    func->is_variadic    = fd->body.func_def.is_variadic;

    /* map AST linkage to IR_Linkage */
    switch (fd->body.func_def.linkage) {
    case LINK_GLOBAL:    func->linkage = IR_LINK_KERNEL;   break;
    case LINK_DEVICE:    func->linkage = IR_LINK_DEVICE;   break;
    case LINK_STATIC:    func->linkage = IR_LINK_INTERNAL; break;
    default:             func->linkage = IR_LINK_EXTERNAL; break; /* host / host_device / extern */
    }

    b->cur_func = func;

    /* create entry block first */
    IR_Block* entry = ir_builder_new_block(b, "entry");
    func->blocks = entry;
    func->last_block = entry;
    b->entry_block = entry;
    ir_builder_set_block(b, entry);

    gen_func_params(&ctx, b, func, fd, a);

    /* generate body */
    if (fd->body.func_def.body)
        gen_stmt(&ctx, fd->body.func_def.body);

    gen_func_terminators(b, func);

    /* append to module */
    if (mod->last_func)
        mod->last_func->next = func;
    else
        mod->funcs = func;
    mod->last_func = func;

    return func;
}
