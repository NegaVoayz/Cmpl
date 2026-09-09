/* gpu_split.c -- split program AST into host and device declaration lists */

#include "gpu.h"

#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------
 *  Shallow clone of an AST function node (shared body pointers)
 * --------------------------------------------------------------- */

static AST_Node*
clone_func(AST_Node* orig)
{
    AST_Node* copy = malloc(sizeof(AST_Node));
    memcpy(copy, orig, sizeof(AST_Node));
    copy->next = NULL;
    return copy;
}

/* ---------------------------------------------------------------
 *  Append a node to a linked list
 * --------------------------------------------------------------- */

static void
append_node(AST_Node** head, AST_Node** tail, AST_Node* node)
{
    node->next = NULL;

    if (*tail)
        (*tail)->next = node;
    else
        *head = node;

    *tail = node;
}

/* ---------------------------------------------------------------
 *  Main split: host vs device
 * --------------------------------------------------------------- */

void
gpu_split(AST_Node* root, GpuSplit* out)
{
    AST_Node *host_head = NULL, *host_tail = NULL;
    AST_Node *dev_head = NULL, *dev_tail = NULL;

    out->copies = NULL;

    if (!root || root->type != AST_PROGRAM) {
        out->host_decls = NULL;
        out->device_decls = NULL;
        return;
    }

    for (AST_Node* decl = root->body.program.decls; decl; ) {
        AST_Node* next = decl->next;  /* save before append_node clobbers it */

        if (decl->type == AST_FUNC_DEF) {
            GpuLinkage linkage = decl->body.func_def.linkage;

            if (linkage == LINK_GLOBAL || linkage == LINK_DEVICE) {
                append_node(&dev_head, &dev_tail, decl);
            } else if (linkage == LINK_HOST_DEVICE) {
                AST_Node* dev_copy = clone_func(decl);
                append_node(&host_head, &host_tail, decl);
                append_node(&dev_head, &dev_tail, dev_copy);
            } else {
                /* LINK_HOST or default */
                append_node(&host_head, &host_tail, decl);
            }

        } else if (decl->type == AST_VAR_DECL) {
            GpuAddrSpace addr_space = decl->body.var_decl.addr_space;

            if (addr_space != ADDR_HOST)
                append_node(&dev_head, &dev_tail, decl);
            else
                append_node(&host_head, &host_tail, decl);

        } else {
            /* type definitions are needed by both host and device
             * for type resolution in ir_gen_module_ex().  shallow-
             * clone them so each side has its own decl chain. */
            append_node(&host_head, &host_tail, decl);
            if (decl->type == AST_TYPEDEF || decl->type == AST_STRUCT_DEF ||
                decl->type == AST_UNION_DEF || decl->type == AST_ENUM_DEF) {
                AST_Node* copy = malloc(sizeof(AST_Node));
                memcpy(copy, decl, sizeof(AST_Node));
                copy->next = NULL;
                append_node(&dev_head, &dev_tail, copy);
            }
        }
        decl = next;
    }

    out->host_decls = host_head;
    out->device_decls = dev_head;
}
