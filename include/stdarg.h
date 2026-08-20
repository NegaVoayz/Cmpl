/* Minimal C11 stdarg.h stub for Cmpl — x86-64 SysV va_list.
 *
 * The struct layout (gp_offset/fp_offset @0/4, overflow_arg_area/
 * reg_save_area @8/16, 24 bytes total) matches what @llvm.va_start
 * writes and what the inline va_arg lowering in ir_gen_va_arg.c reads.
 * The macros map to __builtin_* which the IR generator recognizes:
 * va_start/va_end/va_copy become the @llvm.va_* intrinsics, va_arg is
 * lowered inline. */

#ifndef _STDARG_H
#define _STDARG_H

typedef struct __va_list_tag {
    unsigned gp_offset;
    unsigned fp_offset;
    void* overflow_arg_area;
    void* reg_save_area;
} va_list;

#define va_start(ap, last) __builtin_va_start((ap), (last))
#define va_arg(ap, type)   __builtin_va_arg((ap), type)
#define va_end(ap)         __builtin_va_end((ap))
#define va_copy(d, s)      __builtin_va_copy((d), (s))

#endif
