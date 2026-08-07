/* llvm_cg.c -- LLVM codegen via clang subprocess
 *
 * Writes the IR module as .ll text, then invokes the system clang
 * to produce native object files or assembly. For IR output mode,
 * dumps .ll directly without a subprocess.
 */

#include "llvm_cg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------
 *  Internal helpers
 * --------------------------------------------------------------- */

static int
write_ll_file(IR_Module* mod, const char* path)
{
    FILE* f = fopen(path, "w");

    if (!f) {
        fprintf(stderr, "llvm-cg: cannot open %s for writing\n", path);
        return 0;
    }

    ir_dump_module(mod, f);
    fclose(f);
    return 1;
}

static int
run_clang(const char* ll_path, const char* out_path,
          int asm_mode, int opt_level)
{
    char cmd[1024];
    const char* mode_flag = asm_mode ? "-S" : "-c";
    char opt_flag[8];

    if (opt_level > 0)
        snprintf(opt_flag, sizeof(opt_flag), "-O%d", opt_level);
    else
        opt_flag[0] = '\0';

    if (opt_flag[0])
        snprintf(cmd, sizeof(cmd), "clang %s -x ir %s %s -o \"%s\" 2>&1",
                 mode_flag, opt_flag, ll_path, out_path);
    else
        snprintf(cmd, sizeof(cmd), "clang %s -x ir %s -o \"%s\" 2>&1",
                 mode_flag, ll_path, out_path);

    FILE* pipe = popen(cmd, "r");

    if (!pipe) {
        fprintf(stderr, "llvm-cg: failed to run clang (is it in PATH?)\n");
        return 1;
    }

    /* capture and print any clang stderr output */
    char buf[256];

    while (fgets(buf, sizeof(buf), pipe))
        fprintf(stderr, "%s", buf);

    int rc = pclose(pipe);
    return rc != 0;
}

/* ---------------------------------------------------------------
 *  Output filename derivation
 * --------------------------------------------------------------- */

static const char*
ext_for_mode(CG_OutputMode mode)
{
    switch (mode) {
    case CG_OUT_OBJECT:  return ".o";
    case CG_OUT_ASM:     return ".s";
    case CG_OUT_LLVM_IR: return ".ll";
    default:             return ".o";
    }
}

/* ---------------------------------------------------------------
 *  Public API
 * --------------------------------------------------------------- */

int
cg_compile(IR_Module* mod, const char* outfile,
           CG_OutputMode mode, int opt_level)
{
    if (!mod) {
        fprintf(stderr, "llvm-cg: null module\n");
        return 1;
    }

    if (!outfile)
        outfile = "cmpl_out.o";

    /* LLVM IR mode: dump directly, no subprocess */
    if (mode == CG_OUT_LLVM_IR) {
        if (!write_ll_file(mod, outfile))
            return 1;

        printf("LLVM IR written to %s\n", outfile);
        return 0;
    }

    /* Object / assembly mode: dump .ll to temp, then run clang */
    char ll_tmp[256];
    const char* tmpdir = getenv("TEMP");

    if (!tmpdir) tmpdir = getenv("TMP");
    if (!tmpdir) tmpdir = ".";

    snprintf(ll_tmp, sizeof(ll_tmp), "%s/cmpl_tmp_XXXXXX.ll", tmpdir);

    /* create a unique-ish temp name */
    {
        int n = 0;
        FILE* test = NULL;

        do {
            snprintf(ll_tmp, sizeof(ll_tmp),
                     "%s/cmpl_tmp_%04d.ll", tmpdir, n++);
            test = fopen(ll_tmp, "r");
            if (test) fclose(test);
        } while (test && n < 9999);
    }

    if (!write_ll_file(mod, ll_tmp))
        return 1;

    int asm_mode = (mode == CG_OUT_ASM);
    int rc = run_clang(ll_tmp, outfile, asm_mode, opt_level);

    /* clean up temp file */
    remove(ll_tmp);

    if (rc != 0) {
        fprintf(stderr, "llvm-cg: clang failed (exit %d)\n", rc);
        return 1;
    }

    printf("Output written to %s\n", outfile);
    return 0;
}
