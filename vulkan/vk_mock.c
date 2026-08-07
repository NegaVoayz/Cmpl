/* vk_mock.c -- replace kernel launch placeholders with Vulkan runtime calls */

#include "vulkan.h"

#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------
 *  Create a string constant IR value for the kernel name
 * --------------------------------------------------------------- */

static IR_Value*
make_str_const(IR_Type* ptr_ty, const char* s, int len)
{
    IR_Value* v = calloc(1, sizeof(IR_Value));
    v->kind = VAL_CONST_STRING;
    v->type = ptr_ty;
    char* copy = malloc(len + 1);
    memcpy(copy, s, len);
    copy[len] = '\0';
    v->body.str_val.data = copy;
    v->body.str_val.length = len;
    return v;
}

/* ---------------------------------------------------------------
 *  Walk and transform: __cmpl_kl_* → cmpl_vk_launch
 *
 *  Placeholder format:
 *    call void @__cmpl_kl_KERNEL(config[0..n_cfg-1], args[0..n_ka-1])
 *    where config = [grid, block, shared?, stream?]
 *
 *  Transformed to:
 *    call void @cmpl_vk_launch(name_str, gx,1,1, bx,1,1, sh, stream, n_ka, args...)
 * --------------------------------------------------------------- */

static void
transform_call(IR_Instr* inst)
{
    const char* callee = inst->callee.data;
    int         clen   = inst->callee.length;

    /* only transform __cmpl_kl_* calls */
    if (clen < 11 || memcmp(callee, "__cmpl_kl_", 10) != 0)
        return;

    /* extract kernel name from placeholder */
    int name_len = clen - 10;
    char* kname = malloc(name_len + 1);
    memcpy(kname, callee + 10, name_len);
    kname[name_len] = '\0';

    /* build new args: name, (grid_x,1,1), (block_x,1,1), shared, stream, n_ka, ka... */
    int n_cfg = 0;
    int n_ka  = 0;

    /* figure out how many config vs kernel args we have.
     * config is at least 2 (grid, block), at most 4 (+shared, +stream). */
    if (inst->n_call_args >= 2) {
        n_cfg = (inst->n_call_args >= 4) ? 4 : 2;
        n_ka  = inst->n_call_args - n_cfg;
    } else {
        n_ka = inst->n_call_args;  /* no config? shouldn't happen */
    }

    /* config values — use found value or default 0 */
    IR_Value* grid   = (n_cfg >= 1 && inst->call_args[0]) ? inst->call_args[0] : NULL;
    IR_Value* block  = (n_cfg >= 2 && inst->call_args[1]) ? inst->call_args[1] : NULL;
    IR_Value* shared = (n_cfg >= 3 && inst->call_args[2]) ? inst->call_args[2] : NULL;
    IR_Value* stream = (n_cfg >= 4 && inst->call_args[3]) ? inst->call_args[3] : NULL;

    /* build constant 1 and 0 */
    IR_Value* one = calloc(1, sizeof(IR_Value));
    one->kind = VAL_CONST_INT; one->type = t_i32; one->body.int_val = 1;

    IR_Value* zero = calloc(1, sizeof(IR_Value));
    zero->kind = VAL_CONST_INT; zero->type = t_i32; zero->body.int_val = 0;

    IR_Value* zero64 = calloc(1, sizeof(IR_Value));
    zero64->kind = VAL_CONST_INT; zero64->type = t_i64; zero64->body.int_val = 0;

    /* count kernel args */
    IR_Value* nka = calloc(1, sizeof(IR_Value));
    nka->kind = VAL_CONST_INT; nka->type = t_i32; nka->body.int_val = n_ka;

    /* build new args array:
     * [name_str, grid_x, 1, 1, block_x, 1, 1, shared, stream, n_ka, kernel_args...] */
    int new_n = 10 + n_ka;
    IR_Value** new_args = calloc(new_n, sizeof(IR_Value*));
    int idx = 0;

    IR_Type* i8_ptr = ir_ptr_type(t_i8, 0);

    new_args[idx++] = make_str_const(i8_ptr, kname, name_len);
    new_args[idx++] = grid ? grid : zero;     /* grid_x */
    new_args[idx++] = one;                     /* grid_y */
    new_args[idx++] = one;                     /* grid_z */
    new_args[idx++] = block ? block : zero;    /* block_x */
    new_args[idx++] = one;                     /* block_y */
    new_args[idx++] = one;                     /* block_z */
    new_args[idx++] = shared ? shared : zero;  /* shared_mem */
    new_args[idx++] = stream ? stream : zero64;/* stream */
    new_args[idx++] = nka;                     /* n_args */

    /* copy kernel args */
    for (int i = 0; i < n_ka; i++)
        new_args[idx++] = inst->call_args[n_cfg + i];

    /* replace callee */
    free((void*)inst->callee.data);
    char* new_callee = malloc(strlen("cmpl_vk_launch") + 1);
    strcpy(new_callee, "cmpl_vk_launch");
    inst->callee.data = new_callee;
    inst->callee.length = strlen("cmpl_vk_launch");

    /* replace args */
    free(inst->call_args);
    inst->call_args = new_args;
    inst->n_call_args = new_n;

    free(kname);
}

/* ---------------------------------------------------------------
 *  Walk IR module and transform all placeholder calls
 * --------------------------------------------------------------- */

static void
walk_module(IR_Module* mod)
{
    for (IR_Func* fn = mod->funcs; fn; fn = fn->next)
        for (IR_Block* blk = fn->blocks; blk; blk = blk->next)
            for (IR_Instr* inst = blk->first; inst; inst = inst->next)
                if (inst->opcode == IROP_CALL)
                    transform_call(inst);
}

/* ---------------------------------------------------------------
 *  Public API
 * --------------------------------------------------------------- */

void
vk_mock_insert(IR_Module* host_mod, KernelLaunch* launches, int n)
{
    (void)launches;
    (void)n;
    if (host_mod)
        walk_module(host_mod);
}
