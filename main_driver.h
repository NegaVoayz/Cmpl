/* main_driver.h -- shared driver API for the cmpl front-end entry points.
 *
 * CmdOpts, the per-TU pipeline drivers (main.c / main_driver.c), and the
 * batch subcommand (main_batch.c) all need the same options struct and the
 * single-TU entry point.  Kept together so main.c stays under 200 lines and
 * main_batch.c can reuse compile_source unchanged. */

#ifndef MAIN_DRIVER_H
#define MAIN_DRIVER_H

#include "pp.h"
#include "llvm_cg.h"

typedef struct AST_Node AST_Node;

typedef struct {
    const char* filename;
    int         dump_ir;
    int         cuda_mode;
    int         dump_spv;
    int         opt_level;
    int         codegen_mode;
    const char* out_file;
    int         dump_preprocess;
    int         quiet;   /* suppress progress banners (batch mode) */
} CmdOpts;

/* Bundled include stubs + self-hosting source dirs, in search order.
 * Both parse_args and batch per-TU contexts must apply these first. */
void cmpl_add_default_include_paths(PPCtx* pp_ctx);

/* Single-TU pipeline: pp -> parse -> ast-opt -> output pipeline -> teardown.
 * Frees the PPCtx on every exit path (caller hands a fresh one per TU). */
int  compile_source(PPCtx* pp_ctx, const CmdOpts* opts);

/* Batch subcommand (cmpl batch <filelist> / cmpl @filelist). */
int  run_batch(int argc, char** argv);

/* Pipeline drivers -- defined in main_driver.c */
void run_cuda_pipeline(AST_Node* root, const char* filename,
                       int dump_spv, int opt_level);
int  run_codegen_pipeline(AST_Node* root, const char* filename,
                          const char* out_file, int codegen_mode,
                          int opt_level, int quiet);
int  run_ir_pipeline(AST_Node* root, int opt_level);
void run_ast_pipeline(AST_Node* root);

#endif /* MAIN_DRIVER_H */
