/* ir_gen.c -- AST-to-IR walker: converts AST nodes to LLVM IR instructions */

#include "ir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"

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
    IR_Block*     break_blk;    /* target for break */
    IR_Block*     cont_blk;     /* target for continue */
    int           is_device;    /* 1 = device IR gen (CUDA builtins), 0 = host */
} GenCtx;

/* ---------------------------------------------------------------
 *  Symbol table ops
 * --------------------------------------------------------------- */

static IR_Value*
sym_lookup(GenCtx* ctx, String name)
{
    for (SymEntry* e = ctx->syms; e; e = e->next) {
        if (e->name.length == name.length &&
            memcmp(e->name.data, name.data, name.length) == 0)
            return e->alloca;
    }
    return NULL;
}

static void
sym_add(GenCtx* ctx, String name, IR_Value* alloca)
{
    SymEntry* e = calloc(1, sizeof(SymEntry));
    e->name = name;
    e->alloca = alloca;
    e->next = ctx->syms;
    ctx->syms = e;
}

/* ---------------------------------------------------------------
 *  Forward declarations
 * --------------------------------------------------------------- */

static IR_Value* gen_expr(GenCtx* ctx, AST_Node* n);
static void      gen_stmt(GenCtx* ctx, AST_Node* n);

/* ---------------------------------------------------------------
 *  CUDA builtin lookup (for device IR only)
 * --------------------------------------------------------------- */

typedef struct {
    const char* name;
    const char* member;
    int         dim;
    const char* fn;
} CudaBuiltin;

static const CudaBuiltin cuda_builtins[] = {
    {"blockIdx",  "x", 0, "__spv_workgroup_id"},
    {"blockIdx",  "y", 1, "__spv_workgroup_id"},
    {"blockIdx",  "z", 2, "__spv_workgroup_id"},
    {"threadIdx", "x", 0, "__spv_local_invocation_id"},
    {"threadIdx", "y", 1, "__spv_local_invocation_id"},
    {"threadIdx", "z", 2, "__spv_local_invocation_id"},
    {"blockDim",  "x", 0, "__spv_workgroup_size"},
    {"blockDim",  "y", 1, "__spv_workgroup_size"},
    {"blockDim",  "z", 2, "__spv_workgroup_size"},
    {"gridDim",   "x", 0, "__spv_num_workgroups"},
    {"gridDim",   "y", 1, "__spv_num_workgroups"},
    {"gridDim",   "z", 2, "__spv_num_workgroups"},
};

static int
match_str(const char* a, const String* b)
{
    int len = strlen(a);
    return len == b->length && memcmp(a, b->data, len) == 0;
}

/* ---------------------------------------------------------------
 *  Expression generation
 * --------------------------------------------------------------- */

static IR_Value*
gen_binary_op(GenCtx* ctx, TokenKind op, IR_Value* lhs, IR_Value* rhs)
{
    IR_Builder* b = ctx->b;

    switch (op) {
    case TOK_PLUS:     return ir_build_add(b, lhs, rhs);
    case TOK_MINUS:    return ir_build_sub(b, lhs, rhs);
    case TOK_STAR:     return ir_build_mul(b, lhs, rhs);
    case TOK_SLASH:    return ir_build_sdiv(b, lhs, rhs);
    case TOK_PERCENT:  return ir_build_srem(b, lhs, rhs);
    case TOK_AMP:      return ir_build_and(b, lhs, rhs);
    case TOK_PIPE:     return ir_build_or(b, lhs, rhs);
    case TOK_CARET:    return ir_build_xor(b, lhs, rhs);
    case TOK_LTLT:     return ir_build_shl(b, lhs, rhs);
    case TOK_EQEQ:     return ir_build_icmp(b, IR_COND_EQ, lhs, rhs);
    case TOK_BANGEQ:   return ir_build_icmp(b, IR_COND_NE, lhs, rhs);
    case TOK_LT:       return ir_build_icmp(b, IR_COND_SLT, lhs, rhs);
    case TOK_GT:       return ir_build_icmp(b, IR_COND_SGT, lhs, rhs);
    case TOK_LTEQ:     return ir_build_icmp(b, IR_COND_SLE, lhs, rhs);
    case TOK_GTEQ:     return ir_build_icmp(b, IR_COND_SGE, lhs, rhs);
    default:           return lhs;  /* fallback */
    }
}

static IR_Value*
gen_expr(GenCtx* ctx, AST_Node* n)
{
    if (!n) return NULL;

    IR_Builder* b = ctx->b;

    switch (n->type) {
    case AST_INT_LIT:
        return ir_const_int(b, t_i32, n->body.literal.int_val);

    case AST_LONG_LIT:
        return ir_const_int(b, t_i64, n->body.literal.int_val);

    case AST_CHAR_LIT:
        return ir_const_int(b, t_i8, n->body.literal.char_val);

    case AST_FLOAT_LIT:
        return ir_const_float(t_f32, n->body.literal.float_val);

    case AST_DOUBLE_LIT:
        return ir_const_float(t_f64, n->body.literal.float_val);

    case AST_STRING_LIT:
    {
        IR_Value* v = calloc(1, sizeof(IR_Value));
        v->kind = VAL_CONST_STRING;
        v->type = ir_ptr_type(t_i8, 0);
        v->body.str_val = n->body.literal.str_val;
        return v;
    }

    case AST_IDENT:
    {
        IR_Value* ptr = sym_lookup(ctx, n->body.ident.name);

        if (ptr)
            return ir_build_load(b, ptr);

        /* might be a function name used as value -- return undef for now */
        IR_Value* v = calloc(1, sizeof(IR_Value));
        v->kind = VAL_UNDEF;
        v->type = t_i32;
        return v;
    }

    case AST_BINARY:
    {
        /* assignment: store RHS into LHS's alloca */
        if (n->body.binary.op == TOK_EQ) {
            IR_Value* rhs = gen_expr(ctx, n->body.binary.right);

            if (n->body.binary.left->type == AST_IDENT) {
                IR_Value* ptr = sym_lookup(ctx,
                                           n->body.binary.left->body.ident.name);
                if (ptr)
                    ir_build_store(b, rhs, ptr);
            }
            return rhs;
        }
        IR_Value* lhs = gen_expr(ctx, n->body.binary.left);
        IR_Value* rhs = gen_expr(ctx, n->body.binary.right);
        return gen_binary_op(ctx, n->body.binary.op, lhs, rhs);
    }

    case AST_UNARY:
    {
        IR_Value* op = gen_expr(ctx, n->body.unary.operand);

        if (n->body.unary.op == TOK_MINUS)
            return ir_build_sub(b, ir_const_int(b, op->type, 0), op);

        if (n->body.unary.op == TOK_BANG)
            return ir_build_icmp(b, IR_COND_EQ, op,
                                 ir_const_int(b, op->type, 0));
        return op;
    }

    case AST_CALL:
    {
        int       n_args = 0;
        IR_Value* arg_buf[16];
        String    callee_name = {0, 0};

        /* get callee name directly from AST (not through expr gen) */
        if (n->body.call.callee->type == AST_IDENT)
            callee_name = n->body.call.callee->body.ident.name;

        for (AST_Node* a = n->body.call.args; a; a = a->next) {
            if (n_args < 16)
                arg_buf[n_args++] = gen_expr(ctx, a);
        }

        char name_buf[128];
        int name_len = callee_name.length;

        if (name_len > 127) name_len = 127;
        memcpy(name_buf, callee_name.data, name_len);
        name_buf[name_len] = '\0';
        return ir_build_call(b, name_buf, t_i32, arg_buf, n_args);
    }

    case AST_KERNEL_LAUNCH:
    {
        /* generate placeholder: __cmpl_kl_KERNELNAME(config..., args...)
         * config order: grid, block, [shared], [stream]
         * mock insertion replaces __cmpl_kl_* with cmpl_vk_launch. */
        AST_Node* callee_node = n->body.kernel_launch.callee;
        String    kn = {0, 0};

        if (callee_node && callee_node->type == AST_IDENT)
            kn = callee_node->body.ident.name;

        char pn[128];
        int  kl = kn.length > 120 ? 120 : kn.length;

        memcpy(pn, "__cmpl_kl_", 10);
        if (kl > 0) memcpy(pn + 10, kn.data, kl);
        pn[10 + kl] = '\0';

        /* collect config + kernel args into one array */
        int       n_args = 0;
        IR_Value* arg_buf[16];

        /* config: grid, block, shared, stream */
        for (AST_Node* c = n->body.kernel_launch.config;
             c && n_args < 16; c = c->next)
            arg_buf[n_args++] = gen_expr(ctx, c);

        /* kernel args */
        for (AST_Node* a = n->body.kernel_launch.args;
             a && n_args < 16; a = a->next)
            arg_buf[n_args++] = gen_expr(ctx, a);

        return ir_build_call(b, pn, t_void, arg_buf, n_args);
    }

    case AST_TERNARY:
    {
        IR_Value* cond = gen_expr(ctx, n->body.ternary.cond);
        IR_Value* then_v = gen_expr(ctx, n->body.ternary.then_expr);
        IR_Value* else_v = gen_expr(ctx, n->body.ternary.else_expr);
        return ir_build_select(b, cond, then_v, else_v);
    }

    case AST_CAST:
    {
        /* simplified: just generate the inner expression */
        return gen_expr(ctx, n->body.cast.cast_expr);
    }

    case AST_INDEX:
    {
        IR_Value* arr = gen_expr(ctx, n->body.subscript.array);
        IR_Value* idx = gen_expr(ctx, n->body.subscript.index);
        IR_Value* gep = ir_build_gep(b, arr,
                                      ir_const_int(b, t_i32, 0), idx);
        return ir_build_load(b, gep);
    }

    case AST_MEMBER:
        /* CUDA builtins: blockIdx.x → call @__spv_workgroup_id(i32 0) */
        if (ctx->is_device &&
            n->body.member.record->type == AST_IDENT) {
            String* rec_name = &n->body.member.record->body.ident.name;
            String* memb = &n->body.member.member;

            for (int i = 0; i < 12; i++) {
                if (match_str(cuda_builtins[i].name, rec_name) &&
                    match_str(cuda_builtins[i].member, memb)) {
                    IR_Value* dim = ir_const_int(b, t_i32,
                                                 cuda_builtins[i].dim);
                    IR_Value* args[] = {dim};
                    return ir_build_call(b, cuda_builtins[i].fn,
                                         t_i32, args, 1);
                }
            }
        }
        /* non-builtin member access: return undef for now */
        {
            IR_Value* v = calloc(1, sizeof(IR_Value));
            v->kind = VAL_UNDEF;
            v->type = t_i32;
            return v;
        }

    default:
        return NULL;
    }
}

/* ---------------------------------------------------------------
 *  Statement generation
 * --------------------------------------------------------------- */

static void
gen_stmt(GenCtx* ctx, AST_Node* n)
{
    if (!n) return;

    IR_Builder* b = ctx->b;

    switch (n->type) {
    case AST_BLOCK:
        for (AST_Node* s = n->body.block.stmts; s; s = s->next)
            gen_stmt(ctx, s);
        break;

    case AST_EXPR_STMT:
        gen_expr(ctx, n->body.expr_stmt.expr);
        break;

    case AST_RETURN:
        ir_build_ret(b, gen_expr(ctx, n->body.ret.expr));
        break;

    case AST_IF:
    {
        IR_Value* cond = gen_expr(ctx, n->body.if_stmt.condition);
        IR_Block* then_blk = ir_builder_new_block(b, "then");
        IR_Block* else_blk = n->body.if_stmt.else_branch ?
                             ir_builder_new_block(b, "else") : NULL;
        IR_Block* merge_blk = ir_builder_new_block(b, "merge");

        /* link blocks into function */
        {
            IR_Block** tail = &b->cur_func->blocks;
            while (*tail) tail = &(*tail)->next;
            *tail = then_blk;

            if (else_blk) {
                then_blk->next = else_blk;
                else_blk->next = merge_blk;
            } else {
                then_blk->next = merge_blk;
            }
        }

        ir_build_cond_br(b, cond, then_blk, else_blk ? else_blk : merge_blk);

        ir_builder_set_block(b, then_blk);
        gen_stmt(ctx, n->body.if_stmt.then_branch);

        if (!b->cur_block->last ||
            b->cur_block->last->opcode != IROP_RET)
            ir_build_br(b, merge_blk);

        if (else_blk) {
            ir_builder_set_block(b, else_blk);
            gen_stmt(ctx, n->body.if_stmt.else_branch);

            if (!b->cur_block->last ||
                b->cur_block->last->opcode != IROP_RET)
                ir_build_br(b, merge_blk);
        }

        ir_builder_set_block(b, merge_blk);
        break;
    }

    case AST_WHILE:
    {
        IR_Block* cond_blk = ir_builder_new_block(b, "while.cond");
        IR_Block* body_blk = ir_builder_new_block(b, "while.body");
        IR_Block* merge_blk = ir_builder_new_block(b, "while.end");

        {
            IR_Block** tail = &b->cur_func->blocks;
            while (*tail) tail = &(*tail)->next;
            *tail = cond_blk;
            cond_blk->next = body_blk;
            body_blk->next = merge_blk;
        }

        ir_build_br(b, cond_blk);

        ir_builder_set_block(b, cond_blk);
        IR_Value* cond = gen_expr(ctx, n->body.loop.condition);
        ir_build_cond_br(b, cond, body_blk, merge_blk);

        /* push loop context for break/continue */
        IR_Block* save_break = ctx->break_blk;
        IR_Block* save_cont = ctx->cont_blk;

        ctx->break_blk = merge_blk;
        ctx->cont_blk = cond_blk;

        ir_builder_set_block(b, body_blk);
        gen_stmt(ctx, n->body.loop.body);

        if (!b->cur_block->last ||
            b->cur_block->last->opcode != IROP_RET)
            ir_build_br(b, cond_blk);

        ctx->break_blk = save_break;
        ctx->cont_blk = save_cont;

        ir_builder_set_block(b, merge_blk);
        break;
    }

    case AST_FOR:
    {
        if (n->body.for_stmt.init)
            gen_stmt(ctx, n->body.for_stmt.init);

        IR_Block* cond_blk = ir_builder_new_block(b, "for.cond");
        IR_Block* body_blk = ir_builder_new_block(b, "for.body");
        IR_Block* update_blk = ir_builder_new_block(b, "for.update");
        IR_Block* merge_blk = ir_builder_new_block(b, "for.end");

        {
            IR_Block** tail = &b->cur_func->blocks;
            while (*tail) tail = &(*tail)->next;
            *tail = cond_blk;
            cond_blk->next = body_blk;
            body_blk->next = update_blk;
            update_blk->next = merge_blk;
        }

        ir_build_br(b, cond_blk);
        ir_builder_set_block(b, cond_blk);

        IR_Value* cond = gen_expr(ctx, n->body.for_stmt.condition);
        if (cond)
            ir_build_cond_br(b, cond, body_blk, merge_blk);
        else
            ir_build_br(b, body_blk);

        IR_Block* save_break = ctx->break_blk;
        IR_Block* save_cont = ctx->cont_blk;

        ctx->break_blk = merge_blk;
        ctx->cont_blk = update_blk;

        ir_builder_set_block(b, body_blk);
        gen_stmt(ctx, n->body.for_stmt.body);

        if (!b->cur_block->last ||
            b->cur_block->last->opcode != IROP_RET)
            ir_build_br(b, update_blk);

        ir_builder_set_block(b, update_blk);
        gen_expr(ctx, n->body.for_stmt.update);
        ir_build_br(b, cond_blk);

        ctx->break_blk = save_break;
        ctx->cont_blk = save_cont;

        ir_builder_set_block(b, merge_blk);
        break;
    }

    case AST_BREAK:
        if (ctx->break_blk)
            ir_build_br(b, ctx->break_blk);
        break;

    case AST_CONTINUE:
        if (ctx->cont_blk)
            ir_build_br(b, ctx->cont_blk);
        break;

    case AST_VAR_DECL:
    {
        IR_Type* var_ty = ir_type_from_ast(n->body.var_decl.var_type);
        IR_Value* alloca = ir_build_alloca(b, var_ty ? var_ty : t_i32);

        sym_add(ctx, n->body.var_decl.name, alloca);

        if (n->body.var_decl.init) {
            IR_Value* init = gen_expr(ctx, n->body.var_decl.init);

            if (init)
                ir_build_store(b, init, alloca);
        }
        break;
    }

    case AST_SWITCH:
        /* simplified: just generate body */
        gen_stmt(ctx, n->body.switch_stmt.body);
        break;

    case AST_CASE:
    case AST_DEFAULT:
        gen_stmt(ctx, n->body.case_stmt.stmt);
        break;

    /* skip: goto, labels, do-while (can add later) */
    default:
        break;
    }
}

/* ---------------------------------------------------------------
 *  Function generation
 * --------------------------------------------------------------- */

IR_Func*
ir_gen_function(IR_Module* mod, AST_Node* func_def, int is_device)
{
    AST_Node*   fd = func_def;
    IR_Builder* b = ir_builder_new(mod);
    GenCtx      ctx = {b, NULL, NULL, NULL, is_device};
    IR_Func*    func = calloc(1, sizeof(IR_Func));

    func->name = fd->body.func_def.name;
    func->ret_type = ir_type_from_ast(fd->body.func_def.ret_type);

    if (!func->ret_type)
        func->ret_type = t_void;

    /* map AST linkage to IR_Linkage */
    switch (fd->body.func_def.linkage) {
    case 2: func->linkage = LINK_KERNEL;  break;  /* LINK_GLOBAL */
    case 1: func->linkage = LINK_DEVICE;  break;  /* LINK_DEVICE */
    default: func->linkage = LINK_EXTERNAL; break; /* host */
    }

    /* count and create params (no builder calls yet -- no block to append to) */
    int n = 0;
    for (AST_Node* p = fd->body.func_def.params; p; p = p->next) n++;

    func->n_params = n;
    func->params = calloc(n, sizeof(IR_Value*));

    for (int i = 0; i < n; i++) {
        func->params[i] = calloc(1, sizeof(IR_Value));
        func->params[i]->kind = VAL_PARAM;
        func->params[i]->type = t_i32;  /* set properly below */
        func->params[i]->id = b->next_vreg_id++;
    }

    b->cur_func = func;

    /* create entry block first */
    IR_Block* entry = ir_builder_new_block(b, "entry");
    func->blocks = entry;
    ir_builder_set_block(b, entry);

    /* now emit allocas and stores for params */
    {
        int i = 0;
        for (AST_Node* p = fd->body.func_def.params; p; p = p->next, i++) {
            IR_Type* pty = ir_type_from_ast(p->body.param_decl.param_type);
            func->params[i]->type = pty ? pty : t_i32;

            IR_Value* alloca = ir_build_alloca(b, func->params[i]->type);
            sym_add(&ctx, p->body.param_decl.name, alloca);
            ir_build_store(b, func->params[i], alloca);
        }
    }

    /* generate body */
    if (fd->body.func_def.body)
        gen_stmt(&ctx, fd->body.func_def.body);

    /* ensure terminator on the last block */
    {
        IR_Block* last_blk = func->blocks;
        while (last_blk->next) last_blk = last_blk->next;

        IR_Instr* term = last_blk->last;

        if (!term || (term->opcode != IROP_RET &&
                      term->opcode != IROP_BR &&
                      term->opcode != IROP_COND_BR &&
                      term->opcode != IROP_UNREACHABLE)) {
            ir_builder_set_block(b, last_blk);
            ir_build_ret(b, NULL);
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

    for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
        if (decl->type == AST_FUNC_DEF)
            ir_gen_function(mod, decl, is_device);
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
