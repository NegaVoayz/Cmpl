/* main_driver.c -- the four top-level pipelines (CUDA, codegen, IR dump, AST
 * dump).  Split out of main.c so the argument parser and dispatcher stay in
 * one place. */

#include "parse.h"
#include "pp.h"
#include "optimize.h"
#include "ir.h"
#include "ir-opt.h"
#include "cuda.h"
#include "vulkan.h"
#include "llvm_cg.h"
#include "arena.h"
#include "main_driver.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* AST debug dump -- defined in dump_ast.c */
extern void dump_ast_public(AST_Node* n, int depth);

/* derive the output base name from the input file: basename without the
 * extension ("dir/kernel.cu" -> "kernel").  Host/device outputs land in
 * the current directory, like gcc/clang (doc: cmpl -cuda kernel.cu ->
 * kernel.host.ll + kernel.device.spv). */
static void
out_base_name(const char* filename, char* out, int cap)
{
    const char* slash = strrchr(filename, '/');
    const char* base = slash ? slash + 1 : filename;
    const char* dot = strrchr(base, '.');
    int len = dot ? (int)(dot - base) : (int)strlen(base);
    if (len > cap - 1) len = cap - 1;
    memcpy(out, base, len);
    out[len] = '\0';
}

/* CUDA pipeline: split → IR gen → mock → SPIR-V. */
void
run_cuda_pipeline(AST_Node* root, const char* filename, int dump_spv,
                  int opt_level)
{
    CudaSplit cs;

    cuda_split(root, &cs);

    printf("\n--- Device/Host Split ---\n");
    printf("Host decls: %s\n", cs.host_decls ? "yes" : "none");
    printf("Device decls: %s\n", cs.device_decls ? "yes" : "none");

    /* generate two IR modules */
    IR_Module *host_mod = NULL, *device_mod = NULL;

    ir_gen_cuda_modules(cs.host_decls, cs.device_decls, &host_mod, &device_mod);

    /* run IR optimizer on both modules */
    if (host_mod)  ir_optimize(host_mod, opt_level);
    if (device_mod) ir_optimize(device_mod, opt_level);

    /* collect kernel launches from host AST */
    int n_launches = 0;
    KernelLaunch* launches = cuda_collect_launches(cs.host_decls, &n_launches);

    /* insert Vulkan mock calls in host IR */
    vk_mock_insert(host_mod, launches, n_launches);

    char outbase[256];
    out_base_name(filename, outbase, sizeof(outbase));

    /* dump host IR + write it as <base>.host.ll so the program can be
     * compiled with clang (the doc's "compile the host side" flow) */
    if (host_mod) {
        printf("\n--- Host IR ---\n");
        ir_dump_module(host_mod, stdout);

        char host_name[256];
        snprintf(host_name, sizeof(host_name), "%s.host.ll", outbase);
        FILE* hf = fopen(host_name, "w");
        if (hf) {
            ir_dump_module(host_mod, hf);
            fclose(hf);
            printf("\n--- Host IR written to %s ---\n", host_name);
        } else {
            printf("\n--- Failed to write %s ---\n", host_name);
        }
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

        snprintf(spv_name, sizeof(spv_name), "%s.device.spv", outbase);
        if (spv_write_file(&spv, spv_name))
            printf("\n--- SPIR-V written to %s (%d words) ---\n",
                   spv_name, spv.len);
        else
            printf("\n--- Failed to write %s ---\n", spv_name);

        spv_free(&spv);
    }

    free(launches);
    if (host_mod)  arena_free(host_mod->arena);
    if (device_mod) arena_free(device_mod->arena);
}

/* LLVM codegen path: IR gen → optimize → clang subprocess.
 * Returns 0 on success, nonzero when IR gen or codegen failed. */
int
run_codegen_pipeline(AST_Node* root, const char* filename, const char* out_file,
                     int codegen_mode, int opt_level, int quiet)
{
    if (!quiet) printf("\n--- Generating IR for codegen ---\n");
    IR_Module* mod = ir_gen_program(root);

    if (!mod) {
        fprintf(stderr, "Codegen failed (IR generation error).\n");
        return 1;
    }

    ir_optimize(mod, opt_level);

    /* derive output name from source if not specified */
    char default_out[256];
    const char* output = out_file;

    if (!output) {
        const char* dot = strrchr(filename, '.');
        int baselen = dot ? (int)(dot - filename) : (int)strlen(filename);
        const char* ext;

        switch (codegen_mode) {
        case CG_OUT_OBJECT:  ext = ".o";  break;
        case CG_OUT_ASM:     ext = ".s";  break;
        case CG_OUT_LLVM_IR: ext = ".ll"; break;
        default:             ext = ".o";  break;
        }
        snprintf(default_out, sizeof(default_out), "%.*s%s",
                 baselen, filename, ext);
        output = default_out;
    }

    int result = cg_compile(mod, output, codegen_mode, opt_level, quiet);

    if (result != 0)
        fprintf(stderr, "Codegen failed.\n");
    arena_free(mod->arena);
    return result != 0;
}

/* IR dump path.  Returns 0 on success, nonzero when IR gen errored. */
int
run_ir_pipeline(AST_Node* root, int opt_level)
{
    printf("\n--- IR ---\n");
    IR_Module* mod = ir_gen_program(root);

    if (!mod) {
        fprintf(stderr, "IR generation failed.\n");
        return 1;
    }

    ir_optimize(mod, opt_level);
    ir_dump_module(mod, stdout);
    arena_free(mod->arena);
    return 0;
}

/* AST dump path. */
void
run_ast_pipeline(AST_Node* root)
{
    printf("\nAST:\n");
    dump_ast_public(root, 0);
}
