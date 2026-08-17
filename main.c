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

/* pipeline drivers -- defined in main_driver.c */
extern void run_cuda_pipeline(AST_Node* root, const char* filename,
                              int dump_spv, int opt_level);
extern int  run_codegen_pipeline(AST_Node* root, const char* filename,
                                 const char* out_file, int codegen_mode,
                                 int opt_level);
extern int  run_ir_pipeline(AST_Node* root, int opt_level);
extern void run_ast_pipeline(AST_Node* root);

typedef struct {
    const char* filename;
    int         dump_ir;
    int         cuda_mode;
    int         dump_spv;
    int         opt_level;
    int         codegen_mode;
    const char* out_file;
    int         dump_preprocess;
} CmdOpts;

/* Parse command-line flags into opts and set up the preprocessor include
 * paths (bundled stubs + self-hosting source directories). */
static void
parse_args(int argc, char** argv, PPCtx* pp_ctx, CmdOpts* opts)
{
    memset(opts, 0, sizeof(*opts));

    /* Add bundled include stubs for system headers */
    pp_add_include_path(pp_ctx, "../include");

    /* Add all source directories for self-hosting cross-directory includes.
     * Paths are relative to the build directory (where cmpl is normally run). */
    pp_add_include_path(pp_ctx, "../base");
    pp_add_include_path(pp_ctx, "../tokenizer");
    pp_add_include_path(pp_ctx, "../ir");
    pp_add_include_path(pp_ctx, "../pp");
    pp_add_include_path(pp_ctx, "../parser");
    pp_add_include_path(pp_ctx, "../ast-opt");
    pp_add_include_path(pp_ctx, "../ir-opt");
    pp_add_include_path(pp_ctx, "../vulkan");
    pp_add_include_path(pp_ctx, "../cuda");
    pp_add_include_path(pp_ctx, "../llvm-codegen");

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-ir") == 0) {
            opts->dump_ir = 1;
        } else if (strcmp(argv[i], "-cuda") == 0) {
            opts->cuda_mode = 1;
        } else if (strcmp(argv[i], "-S") == 0) {
            /* -S: SPIR-V dump in CUDA mode, native asm in normal mode */
            if (opts->cuda_mode)
                opts->dump_spv = 1;
            else
                opts->codegen_mode = CG_OUT_ASM;
        } else if (strncmp(argv[i], "-O", 2) == 0 && argv[i][2] >= '0'
                   && argv[i][2] <= '2' && argv[i][3] == '\0') {
            opts->opt_level = argv[i][2] - '0';
        } else if (strcmp(argv[i], "-c") == 0) {
            opts->codegen_mode = CG_OUT_OBJECT;
        } else if (strcmp(argv[i], "-E") == 0) {
            opts->dump_preprocess = 1;
        } else if (strcmp(argv[i], "-emit-llvm") == 0) {
            opts->codegen_mode = CG_OUT_LLVM_IR;
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            opts->out_file = argv[++i];
        } else if (argv[i][0] == '-' && argv[i][1] == 'I' && argv[i][2] != '\0') {
            pp_add_include_path(pp_ctx, argv[i] + 2);
        } else if (argv[i][0] == '-' && argv[i][1] == 'I' && argv[i][2] == '\0'
                   && i + 1 < argc) {
            pp_add_include_path(pp_ctx, argv[++i]);
        } else {
            opts->filename = argv[i];
        }
    }
}

int
main(int argc, char** argv)
{
    CmdOpts opts;
    PPCtx   pp_ctx;

    pp_ctx_init(&pp_ctx);
    parse_args(argc, argv, &pp_ctx, &opts);

    if (!opts.filename) {
        fprintf(stderr, "Usage: %s [-E] [-I dir]... [-ir] [-cuda] [-c|-S|-emit-llvm] [-o outfile]\n              [-O0|-O1|-O2] <source-file>\n",
                argv[0]);
        pp_ctx_free(&pp_ctx);
        return 1;
    }

    if (opts.dump_preprocess) {
        char* pp_code = pp_preprocess(&pp_ctx, opts.filename);

        if (pp_code) {
            fputs(pp_code, stdout);
            free(pp_code);
        }
        pp_ctx_free(&pp_ctx);
        return 0;
    }

    char* code = pp_preprocess(&pp_ctx, opts.filename);

    if (!code) {
        pp_ctx_free(&pp_ctx);
        return 1;
    }

    if (!code[0]) {
        fprintf(stderr, "pp: empty preprocessor output for '%s'\n", opts.filename);
        pp_ctx_free(&pp_ctx);
        free(code);
        return 1;
    }

    printf("--- Parsing ---\n");
    Arena* ast_arena = arena_new();
    AST_Node* root = parse_program(code, ast_arena);

    if (!root) {
        printf("Parse error!\n");
        pp_ctx_free(&pp_ctx);
        arena_free(ast_arena);
        free(code);
        return 1;
    }

    printf("\n--- Optimizing ---\n");
    root = optimize(root);

    if (opts.cuda_mode)
        run_cuda_pipeline(root, opts.filename, opts.dump_spv, opts.opt_level);
    else if (opts.codegen_mode) {
        if (run_codegen_pipeline(root, opts.filename, opts.out_file,
                                 opts.codegen_mode, opts.opt_level) != 0) {
            pp_ctx_free(&pp_ctx);
            arena_free(ast_arena);
            free(code);
            return 1;
        }
    }
    else if (opts.dump_ir) {
        if (run_ir_pipeline(root, opts.opt_level) != 0) {
            pp_ctx_free(&pp_ctx);
            arena_free(ast_arena);
            free(code);
            return 1;
        }
    }
    else
        run_ast_pipeline(root);

    pp_ctx_free(&pp_ctx);

    /* AST arena owns all AST nodes; code buffer holds the preprocessed
     * source text that AST String fields point into.  Free the AST
     * arena first (which invalidates all AST String pointers), then
     * free the code buffer — both are safe to reclaim at exit. */
    arena_free(ast_arena);
    free(code);
    return 0;
}
