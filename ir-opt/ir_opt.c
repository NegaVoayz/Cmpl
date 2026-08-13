/* ir_opt.c -- optimization pass runner */

#include "ir-opt.h"

/* ---------------------------------------------------------------
 *  Run all enabled passes to fixed point
 * --------------------------------------------------------------- */

void
ir_optimize(IR_Module* mod, int level)
{
    int changed, iter = 0;
    int max_iter = 8;  /* safety limit */

    if (!mod) return;

    do {
        changed = 0;

        changed |= opt_mem2reg(mod);
        changed |= opt_dce(mod);
        changed |= opt_const_fold(mod);
        changed |= opt_simplify_cfg(mod);

        if (level >= OPT_DEFAULT) {
            changed |= opt_gvn(mod);
            changed |= opt_inline_dev(mod);
        }
    } while (changed && ++iter < max_iter);
}
