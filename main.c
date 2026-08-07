#include "parse.h"
#include "pp.h"
#include "optimize.h"
#include "ir.h"
#include "ir-opt.h"
#include "cuda.h"
#include "vulkan.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* AST debug dump -- defined in dump_ast.c */
extern void dump_ast_public(AST_Node* n, int depth);

int
main(int argc, char** argv)
{
    const char* filename = NULL;
    int         dump_ir = 0;
    int         cuda_mode = 0;
    int         dump_spv = 0;
    int         opt_level = 0;
    PPCtx       pp_ctx;

    pp_ctx_init(&pp_ctx);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-ir") == 0) {
            dump_ir = 1;
        } else if (strcmp(argv[i], "-cuda") == 0) {
            cuda_mode = 1;
        } else if (strcmp(argv[i], "-S") == 0) {
            dump_spv = 1;
        } else if (strncmp(argv[i], "-O", 2) == 0 && argv[i][2] >= '0'
                   && argv[i][2] <= '2' && argv[i][3] == '\0') {
            opt_level = argv[i][2] - '0';
        } else if (argv[i][0] == '-' && argv[i][1] == 'I' && argv[i][2] != '\0') {
            pp_add_include_path(&pp_ctx, argv[i] + 2);
        } else if (argv[i][0] == '-' && argv[i][1] == 'I' && argv[i][2] == '\0'
                   && i + 1 < argc) {
            pp_add_include_path(&pp_ctx, argv[++i]);
        } else {
            filename = argv[i];
        }
    }

    if (!filename) {
        fprintf(stderr, "Usage: %s [-I dir]... [-ir] [-cuda] [-S] [-O0|-O1|-O2] <source-file>\n",
                argv[0]);
        pp_ctx_free(&pp_ctx);
        return 1;
    }

    char* code = pp_preprocess(&pp_ctx, filename);

    if (!code) {
        pp_ctx_free(&pp_ctx);
        return 1;
    }

    printf("--- Parsing ---\n");
    AST_Node* root = parse_program(code);

    if (!root) {
        printf("Parse error!\n");
        pp_ctx_free(&pp_ctx);
        return 1;
    }

    printf("\n--- Optimizing ---\n");
    root = optimize(root);

    if (cuda_mode) {
        /* -------------------------------------------------------
         *  CUDA pipeline: split → IR gen → mock → SPIR-V
         * ------------------------------------------------------- */
        CudaSplit cs;
        cuda_split(root, &cs);

        printf("\n--- Device/Host Split ---\n");
        printf("Host decls: %s\n", cs.host_decls ? "yes" : "none");
        printf("Device decls: %s\n", cs.device_decls ? "yes" : "none");

        /* generate two IR modules */
        IR_Module *host_mod = NULL, *device_mod = NULL;
        ir_gen_cuda_modules(cs.host_decls, cs.device_decls,
                            &host_mod, &device_mod);

        /* run IR optimizer on both modules */
        if (host_mod)
            ir_optimize(host_mod, opt_level);
        if (device_mod)
            ir_optimize(device_mod, opt_level);

        /* collect kernel launches from host AST */
        int n_launches = 0;
        KernelLaunch* launches = cuda_collect_launches(cs.host_decls,
                                                       &n_launches);

        /* insert Vulkan mock calls in host IR */
        vk_mock_insert(host_mod, launches, n_launches);

        /* dump host IR */
        if (host_mod) {
            printf("\n--- Host IR ---\n");
            ir_dump_module(host_mod, stdout);
        }

        /* SPIR-V emission for device */
        if (device_mod) {
            SPV_Writer spv;
            spv_init(&spv);

            if (dump_spv) {
                printf("\n--- Device IR (pre-SPIRV) ---\n");
                ir_dump_module(device_mod, stdout);
            }

            spv_emit_module(&spv, device_mod);

            /* write .spv file */
            char spv_name[256];
            snprintf(spv_name, sizeof(spv_name), "%s.spv", filename);
            if (spv_write_file(&spv, spv_name))
                printf("\n--- SPIR-V written to %s (%d words) ---\n",
                       spv_name, spv.len);
            else
                printf("\n--- Failed to write %s ---\n", spv_name);

            spv_free(&spv);
        }

        free(launches);
    } else if (dump_ir) {
        printf("\n--- IR ---\n");
        IR_Module* mod = ir_gen_program(root);

        if (mod) {
            ir_optimize(mod, opt_level);
            ir_dump_module(mod, stdout);
        }
    } else {
        printf("\nAST:\n");
        dump_ast_public(root, 0);
    }

    pp_ctx_free(&pp_ctx);
    /* NOTE: code must not be freed here -- AST String fields are
     * non-owning pointers into token data which references source text. */
    return 0;
}
