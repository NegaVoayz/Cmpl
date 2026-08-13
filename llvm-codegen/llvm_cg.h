/* llvm_cg.h -- LLVM codegen public API for the Cmpl compiler
 *
 * Invokes the system clang as a subprocess to compile our IR tree's
 * .ll text output into native object files or assembly.
 */

#ifndef LLVM_CG_H
#define LLVM_CG_H

#include "ir.h"

typedef enum {
    CG_OUT_OBJECT = 1,  /* -c: native object file (.o / .obj) */
    CG_OUT_ASM,         /* -S: assembly text (.s) */
    CG_OUT_LLVM_IR      /* -emit-llvm: LLVM IR text (.ll) */
} CG_OutputMode;

/* Compile an IR module to native code via clang subprocess.
   Writes the module as .ll text, then runs clang to produce .o/.s.
   For CG_OUT_LLVM_IR, dumps .ll directly -- no subprocess.
   Returns 0 on success, nonzero on failure. */
int cg_compile(IR_Module* mod, const char* outfile,
               CG_OutputMode mode, int opt_level);

#endif /* LLVM_CG_H */
