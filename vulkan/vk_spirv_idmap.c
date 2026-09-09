/* vk_spirv_idmap.c -- the SPIR-V id tables.
 *
 * Every object the emitter references gets an id from one of four disjoint
 * ranges (types, values, functions, blocks), handed out as base + n by
 * map_id.  The tables are plain arrays keyed by pointer identity: the IR is
 * arena-allocated and never moves, so a linear probe is both correct and
 * fast enough for the few thousand objects a kernel produces.
 *
 * Running out of entries must be LOUD.  The old code returned 0 and the
 * emitter happily wrote "result id 0" instructions, producing a module that
 * no Vulkan implementation accepts (spirv-val: "Result Id is 0") — and a
 * register-tiled kernel has far more than the 256 values the table held.
 */

#include "vulkan.h"

static void
table_full(void)
{
    if (!spv_had_error())
        spv_error("SPIR-V id table overflow "
                  "(raise SPV_MAX_* in vulkan/vulkan.h)");
}

int map_id(IdMap* m, int* n, int cap, void* key, int base)
{
    for (int i = 0; i < *n; i++)
        if (m[i].key == key) return m[i].id;

    if (*n >= cap) {           /* id 0 is invalid SPIR-V: never emit it */
        table_full();
        return 0;
    }
    int id = base + *n;
    m[*n].key = key; m[*n].id = id; (*n)++;
    return id;
}

/* register a key under an EXISTING id: two distinct objects that stand
 * for the same SPIR-V object (equivalent scalar types) share one id */
int map_id_as(IdMap* m, int* n, int cap, void* key, int id)
{
    for (int i = 0; i < *n; i++)
        if (m[i].key == key) return m[i].id;

    if (*n >= cap) {
        table_full();
        return 0;
    }
    m[*n].key = key; m[*n].id = id; (*n)++;
    return id;
}

int find_id(IdMap* m, int n, void* key)
{
    for (int i = 0; i < n; i++)
        if (m[i].key == key) return m[i].id;
    return 0;
}
