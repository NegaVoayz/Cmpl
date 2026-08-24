/* vk_mock.c -- replace kernel launch placeholders with Vulkan runtime calls */

#include "vulkan.h"

#include <stdlib.h>
#include <string.h>

#include "arena.h"

/* ---------------------------------------------------------------
 *  Create a string constant IR value for the kernel name
 * --------------------------------------------------------------- */

static IR_Value*
make_str_const(Arena* a, IR_Type* ptr_ty, const char* s, int len)
{
    IR_Value* v = arena_alloc(a, sizeof(IR_Value));
    v->kind = VAL_CONST_STRING;
    v->type = ptr_ty;
    char* copy = arena_alloc(a, len + 1);
    memcpy(copy, s, len);
    copy[len] = '\0';
    v->body.str_val.data = copy;
    v->body.str_val.length = len;
    return v;
}

/* Allocate a small integer constant IR value of the given type. */
static IR_Value*
make_int_const(Arena* a, IR_Type* ty, long long v)
{
    IR_Value* val = arena_alloc(a, sizeof(IR_Value));
    val->kind = VAL_CONST_INT;
    val->type = ty;
    val->body.int_val = v;
    return val;
}

/* ---------------------------------------------------------------
 *  Walk and transform: __cmpl_kl_* → cmpl_vk_launch
 *
 *  Placeholder format (fixed layout set by gen_expr_kernel_launch):
 *    call void @__cmpl_kl_KERNEL(grid, block, shared, stream, args[0..])
 *    where the four config slots are always present (0 when absent).
 *    The split is never guessed from the arg count — every launch with
 *    >= 2 kernel args was previously corrupted (args 2/3 became
 *    "shared"/"stream" and the call arity mismatched the declare).
 *
 *  Transformed to:
 *    call void @cmpl_vk_launch(name_str, gx,gy,gz, bx,by,bz, sh, stream,
 *                              n_ka, args...)   (variadic, see func_type)
 * --------------------------------------------------------------- */

/* Build the transformed argument list for a kernel launch:
 * [name_str, grid_x,1,1, block_x,1,1, shared, stream, n_ka, kernel_args...].
 * Writes the array to *out_args and returns the argument count. */
static int
build_vk_args(IR_Instr* inst, Arena* a, const char* kname, int name_len,
              int n_ka, IR_Value* grid, IR_Value* block,
              IR_Value* shared, IR_Value* stream, IR_Value*** out_args)
{
    IR_Value* one = make_int_const(a, t_i32, 1);
    IR_Value* nka = make_int_const(a, t_i32, n_ka);

    int new_n = 10 + n_ka;
    IR_Value** new_args = arena_alloc(a, new_n * sizeof(IR_Value*));
    int idx = 0;

    IR_Type* i8_ptr = ir_ptr_type(a, t_i8, 0);

    new_args[idx++] = make_str_const(a, i8_ptr, kname, name_len);
    new_args[idx++] = grid;                      /* grid_x */
    new_args[idx++] = one;                       /* grid_y */
    new_args[idx++] = one;                       /* grid_z */
    new_args[idx++] = block;                     /* block_x */
    new_args[idx++] = one;                       /* block_y */
    new_args[idx++] = one;                       /* block_z */
    new_args[idx++] = shared;                    /* shared_mem */
    new_args[idx++] = stream;                    /* stream */
    new_args[idx++] = nka;                       /* n_args */

    /* copy kernel args */
    for (int i = 0; i < n_ka; i++)
        new_args[idx++] = inst->call_args[4 + i];

    *out_args = new_args;
    return new_n;
}

static void
transform_call(IR_Instr* inst, Arena* a)
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

    /* fixed layout: 4 config slots (grid, block, shared, stream), then
     * the kernel args.  Absent config entries are i32 0 (from gen). */
    int n_cfg = 4;
    int n_ka  = inst->n_call_args - n_cfg;
    if (n_ka < 0) n_ka = 0;   /* malformed placeholder: treat as no args */

    IR_Value* grid   = (inst->n_call_args > 0) ? inst->call_args[0] : NULL;
    IR_Value* block  = (inst->n_call_args > 1) ? inst->call_args[1] : NULL;
    IR_Value* shared = (inst->n_call_args > 2) ? inst->call_args[2] : NULL;
    IR_Value* stream = (inst->n_call_args > 3) ? inst->call_args[3] : NULL;

    IR_Value** new_args;
    int new_n = build_vk_args(inst, a, kname, name_len, n_ka,
                              grid, block, shared, stream, &new_args);

    /* replace callee — old data was arena-allocated, no free needed */
    const char* vk_launch = "cmpl_vk_launch";
    int vk_len = (int)strlen(vk_launch);
    char* new_callee = arena_alloc(a, vk_len + 1);
    memcpy(new_callee, vk_launch, vk_len + 1);
    inst->callee.data = new_callee;
    inst->callee.length = vk_len;

    /* the runtime entry is variadic: cmpl_vk_launch(ptr, ...).  Different
     * launch sites carry different kernel-arg counts, so a fixed-arity
     * declare would mismatch every call but the first. */
    IR_Type* i8_ptr = ir_ptr_type(a, t_i8, 0);
    inst->func_type = ir_func_type(a, t_void, i8_ptr, 1);

    /* replace args — old call_args was arena-allocated, no free needed */
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
                    transform_call(inst, mod->arena);
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
