/* vk_spirv_entry.c -- SPIR-V entry point and execution mode emission.
 *
 * Entry points come first (section 5) and every execution mode after them
 * (section 6): interleaving the two puts an OpEntryPoint behind an
 * OpExecutionMode, which is an invalid layout section.  Since SPIR-V 1.4
 * an entry point must also list every global variable it uses.
 */

#include "vulkan.h"

#include <string.h>

/* Every global variable an entry point uses must be listed in its
 * interface (SPIR-V 1.4+): the BuiltIn Input variables, the module
 * globals (Workgroup/StorageBuffer/Uniform/Private) and the kernel's own
 * PushConstant block.  count=0 writes the ids, count=1 only counts. */
static int
interface_ids(SPV_Writer* w, IR_Module* mod, IR_Func* f, IdMap* vm, int vn,
              int count)
{
    int n = spv_builtin_interface_count();

    for (int i = 0; i < n; i++)
        if (!count) spv_w(w, spv_builtin_interface_at(i));

    for (IR_Value* gv = mod->globals; gv; gv = gv->next) {
        int id = find_id(vm, vn, gv);

        if (!id) continue;
        if (!count) spv_w(w, id);
        n++;
    }

    int pcv = spv_params_var_of(f);

    if (pcv) {
        if (!count) spv_w(w, pcv);
        n++;
    }
    return n;
}

void
emit_entries(SPV_Writer* w, IR_Module* mod, IdMap* fm, int fnc,
             IdMap* vm, int vn)
{
    /* all entry points first (section 5), then all execution modes
     * (section 6): interleaving them puts an OpEntryPoint after an
     * OpExecutionMode, which is an invalid layout section */
    for (IR_Func* f = mod->funcs; f; f = f->next) {
        if (f->linkage != IR_LINK_KERNEL || !f->blocks) continue;

        int fid = find_id(fm, fnc, f);
        int nlen = f->name.length;
        char buf[256] = {0};

        /* buf holds 255 name bytes + NUL: a longer kernel name must not
         * make nwords read past the buffer */
        if (nlen > 255) nlen = 255;
        memcpy(buf, f->name.data, nlen);
        int nwords = (nlen + 1 + 3) / 4;
        int nif = interface_ids(w, mod, f, vm, vn, 1);

        spv_op(w, SPV_OP_ENTRY_POINT, 2 + nwords + nif);
        spv_w(w, 5);   /* GLCompute */
        spv_w(w, fid);
        for (int i = 0; i < nwords; i++) {
            uint32_t wrd = 0;
            memcpy(&wrd, buf + i * 4, 4);
            spv_w(w, wrd);
        }
        interface_ids(w, mod, f, vm, vn, 0);
    }

    for (IR_Func* f = mod->funcs; f; f = f->next) {
        if (f->linkage != IR_LINK_KERNEL || !f->blocks) continue;

        int fid = find_id(fm, fnc, f);
        int ls[3];

        /* the GPU block size of this kernel's launch sites: an entry
         * point with LocalSize (1,1,1) runs ONE invocation per workgroup,
         * so threadIdx/blockDim would both be degenerate */
        spv_local_size_of(f, ls);

        spv_op(w, SPV_OP_EXECUTION_MODE, 5);
        spv_w(w, fid);
        spv_w(w, 17);  /* LocalSize */
        spv_w(w, (uint32_t)ls[0]);
        spv_w(w, (uint32_t)ls[1]);
        spv_w(w, (uint32_t)ls[2]);
    }
}
