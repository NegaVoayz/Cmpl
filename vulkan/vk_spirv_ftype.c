/* vk_spirv_ftype.c -- OpTypeFunction declarations for the device module.
 *
 * Function types belong to the types section (SPIR-V logical layout
 * section 9), so they are emitted before the function definitions and
 * only looked up in emit_func.  An entry point takes NO parameters
 * (VUID-StandaloneSpirv-None-04633) — its arguments live in a
 * PushConstant block — while a device function keeps its own.  Duplicate
 * OpTypeFunction declarations are invalid, so identical signatures share
 * one type.
 */

#include "vulkan.h"

#define MAX_FTY 32
#define MAX_FTY_PARM 8

static IR_Func* ft_key[SPV_MAX_FN];
static int      ft_id[SPV_MAX_FN];
static int      ft_n;

static int fty_ret[MAX_FTY];
static int fty_id[MAX_FTY];
static int fty_np[MAX_FTY];
static int fty_par[MAX_FTY][MAX_FTY_PARM];
static int fty_n;

static int
func_type_lookup(int ret, const int* par, int np)
{
    for (int i = 0; i < fty_n; i++) {
        if (fty_ret[i] != ret || fty_np[i] != np) continue;

        int same = 1;

        for (int j = 0; j < np; j++)
            if (fty_par[i][j] != par[j]) { same = 0; break; }
        if (same) return fty_id[i];
    }
    return 0;
}

static void
func_type_add(int ret, const int* par, int np, int id)
{
    if (fty_n >= MAX_FTY || np > MAX_FTY_PARM) return;

    fty_ret[fty_n] = ret; fty_id[fty_n] = id; fty_np[fty_n] = np;
    for (int j = 0; j < np; j++) fty_par[fty_n][j] = par[j];
    fty_n++;
}

void
spv_emit_func_types(SPV_Writer* w, IR_Module* mod, IdMap* tm, int tn)
{
    ft_n = 0;
    fty_n = 0;

    for (IR_Func* f = mod->funcs; f; f = f->next) {
        if (!f->blocks || (f->linkage != IR_LINK_KERNEL &&
                           f->linkage != IR_LINK_DEVICE))
            continue;

        int np = (f->linkage == IR_LINK_KERNEL) ? 0 : f->n_params;
        int ret = find_id(tm, tn, f->ret_type);
        int par[MAX_FTY_PARM] = {0};

        for (int i = 0; i < np && i < MAX_FTY_PARM; i++) {
            IR_Type* pt = f->params[i]->type;

            par[i] = (pt && pt->kind == IR_PTR)
                     ? spv_ptr_type(w, pt->inner, spv_value_sc(f->params[i]),
                                    tm, tn)
                     : find_id(tm, tn, pt);
        }

        int func_ty = np <= MAX_FTY_PARM ? func_type_lookup(ret, par, np) : 0;

        if (!func_ty) {
            func_ty = w->next_id++;
            spv_op(w, SPV_OP_TYPE_FUNCTION, 2 + np);
            spv_w(w, func_ty); spv_w(w, ret);
            for (int i = 0; i < np; i++) spv_w(w, par[i]);
            func_type_add(ret, par, np, func_ty);
        }

        if (ft_n < SPV_MAX_FN) {
            ft_key[ft_n] = f; ft_id[ft_n] = func_ty; ft_n++;
        }
    }
}

int
func_type_id(IR_Func* f)
{
    for (int i = 0; i < ft_n; i++)
        if (ft_key[i] == f) return ft_id[i];
    return 0;
}
