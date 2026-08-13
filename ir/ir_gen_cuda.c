/* ir_gen_cuda.c -- two-module IR generation for CUDA device/host split */

#include "ir.h"

#include <stdlib.h>
#include <string.h>

#include "cuda.h"

/* ---------------------------------------------------------------
 *  Wrap a decl chain in a temporary AST_PROGRAM node
 * --------------------------------------------------------------- */

static AST_Node*
wrap_decls(AST_Node* decls)
{
    AST_Node* prog = calloc(1, sizeof(AST_Node));
    prog->type = AST_PROGRAM;
    prog->body.program.decls = decls;
    return prog;
}

/* ---------------------------------------------------------------
 *  Free malloc'd copies in the device decl chain.
 *  Type-def copies (typedef/struct/union/enum) and function
 *  clones (LINK_HOST_DEVICE) are shallow-copied during
 *  cuda_split() and must be freed after IR gen completes.
 * --------------------------------------------------------------- */

static void
free_device_copies(AST_Node* device_root)
{
    AST_Node* curr = device_root;

    while (curr) {
        AST_Node* next = curr->next;
        int is_copy = 0;

        if (curr->type == AST_TYPEDEF || curr->type == AST_STRUCT_DEF ||
            curr->type == AST_UNION_DEF || curr->type == AST_ENUM_DEF)
            is_copy = 1;
        else if (curr->type == AST_FUNC_DEF &&
                 curr->body.func_def.linkage == LINK_HOST_DEVICE)
            is_copy = 1;

        if (is_copy)
            free(curr);

        curr = next;
    }
}

/* ---------------------------------------------------------------
 *  Public: generate separate host and device IR modules
 * --------------------------------------------------------------- */

void
ir_gen_cuda_modules(AST_Node* host_root, AST_Node* device_root,
                    IR_Module** out_host, IR_Module** out_device)
{
    *out_host = NULL;
    *out_device = NULL;

    if (host_root) {
        AST_Node* prog = wrap_decls(host_root);
        *out_host = ir_gen_module_ex(prog, 0);
        free(prog);
    }

    if (device_root) {
        AST_Node* prog = wrap_decls(device_root);
        *out_device = ir_gen_module_ex(prog, 1);
        free(prog);
    }

    /* free shallow copies created by cuda_split */
    free_device_copies(device_root);
}
