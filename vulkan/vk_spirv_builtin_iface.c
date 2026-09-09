/* vk_spirv_builtin_iface.c -- the builtin objects an entry point must
 * list in its interface, and the accessors the emitters use. */

#include "vulkan.h"

extern int builtin_is_var_kind(int k);
extern int builtin_obj_of(int k);

int
spv_builtin_interface_count(void)
{
    int n = 0;

    for (int k = 0; k < 4; k++)
        if (builtin_is_var_kind(k) && builtin_obj_of(k)) n++;
    return n;
}

int
spv_builtin_interface_at(int i)
{
    for (int k = 0; k < 4; k++) {
        if (!builtin_is_var_kind(k) || !builtin_obj_of(k)) continue;
        if (i == 0) return builtin_obj_of(k);
        i--;
    }
    return 0;
}
