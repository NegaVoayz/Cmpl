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
}
