#include "parse.h"
#include "pp.h"
#include "optimize.h"
#include "ir.h"
#include "ir-opt.h"
#include "cuda.h"
#include "vulkan.h"
#include "llvm_cg.h"
#include "arena.h"

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
    int         codegen_mode = 0;
    const char* out_file = NULL;
    int         dump_preprocess = 0;
    PPCtx       pp_ctx;

    pp_ctx_init(&pp_ctx);

    /* Add bundled include stubs for system headers */
    pp_add_include_path(&pp_ctx, "../include");

    /* Add all source directories for self-hosting cross-directory includes.
     * Paths are relative to the build directory (where cmpl is normally run). */
    pp_add_include_path(&pp_ctx, "../base");
    pp_add_include_path(&pp_ctx, "../tokenizer");
    pp_add_include_path(&pp_ctx, "../ir");
    pp_add_include_path(&pp_ctx, "../pp");
    pp_add_include_path(&pp_ctx, "../parser");
    pp_add_include_path(&pp_ctx, "../ast-opt");
    pp_add_include_path(&pp_ctx, "../ir-opt");
    pp_add_include_path(&pp_ctx, "../vulkan");
    pp_add_include_path(&pp_ctx, "../cuda");
    pp_add_include_path(&pp_ctx, "../llvm-codegen");

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-ir") == 0) {
            dump_ir = 1;
        } else if (strcmp(argv[i], "-cuda") == 0) {
            cuda_mode = 1;
        } else if (strcmp(argv[i], "-S") == 0) {
            /* -S: SPIR-V dump in CUDA mode, native asm in normal mode */
            if (cuda_mode)
                dump_spv = 1;
            else
                codegen_mode = CG_OUT_ASM;
        } else if (strncmp(argv[i], "-O", 2) == 0 && argv[i][2] >= '0'
                   && argv[i][2] <= '2' && argv[i][3] == '\0') {
            opt_level = argv[i][2] - '0';
        } else if (strcmp(argv[i], "-c") == 0) {
            codegen_mode = CG_OUT_OBJECT;
        } else if (strcmp(argv[i], "-E") == 0) {
            dump_preprocess = 1;
        } else if (strcmp(argv[i], "-emit-llvm") == 0) {
            codegen_mode = CG_OUT_LLVM_IR;
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_file = argv[++i];
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
        fprintf(stderr, "Usage: %s [-E] [-I dir]... [-ir] [-cuda] [-c|-S|-emit-llvm] [-o outfile]\n              [-O0|-O1|-O2] <source-file>\n",
                argv[0]);
        pp_ctx_free(&pp_ctx);
        return 1;
    }

    if (dump_preprocess) {
        char* pp_code = pp_preprocess(&pp_ctx, filename);
        if (pp_code) {
            fputs(pp_code, stdout);
        }
        pp_ctx_free(&pp_ctx);
        return 0;
    }

    char* code = pp_preprocess(&pp_ctx, filename);

    if (!code) {
        pp_ctx_free(&pp_ctx);
        return 1;
    }

    printf("--- Parsing ---\n");
    Arena* ast_arena = arena_new();
    AST_Node* root = parse_program(code, ast_arena);

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
    } else if (codegen_mode) {
        /* -------------------------------------------------------
         *  LLVM codegen path: IR gen → optimize → clang subprocess
         * ------------------------------------------------------- */
        printf("\n--- Generating IR for codegen ---\n");
        IR_Module* mod = ir_gen_program(root);

        if (mod) {
            ir_optimize(mod, opt_level);

            /* derive output name from source if not specified */
            char default_out[256];
            const char* output = out_file;
            if (!output) {
                const char* dot = strrchr(filename, '.');
                int baselen = dot ? (int)(dot - filename)
                                  : (int)strlen(filename);
                const char* ext;
                switch (codegen_mode) {
                case CG_OUT_OBJECT:  ext = ".o";  break;
                case CG_OUT_ASM:     ext = ".s";  break;
                case CG_OUT_LLVM_IR: ext = ".ll"; break;
                default:             ext = ".o";  break;
                }
                snprintf(default_out, sizeof(default_out),
                         "%.*s%s", baselen, filename, ext);
                output = default_out;
            }

            int result = cg_compile(mod, output, codegen_mode, opt_level);
            if (result != 0)
                fprintf(stderr, "Codegen failed.\n");
        }
    } else if (dump_ir) {
        printf("\n--- IR ---\n"); fflush(stdout);
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
