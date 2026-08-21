/* ir.h -- LLVM IR tree data structures for the Cmpl compiler.
 *
 * The struct/enum type declarations moved to ir_types.h (type/value model)
 * and ir_program.h (program model), both included below; this header keeps
 * the common type singletons and pulls in ir_api.h. */

#ifndef IR_H
#define IR_H

#include <stdio.h>

#include "ir_types.h"
#include "ir_program.h"

extern IR_Type *t_void, *t_i1, *t_i8, *t_i16, *t_i32, *t_i64;
extern IR_Type *t_u8, *t_u16, *t_u32, *t_u64;
extern IR_Type *t_f32, *t_f64;

#include "ir_api.h"

#endif /* IR_H */
