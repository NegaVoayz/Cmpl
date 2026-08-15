/* ir_builder.h -- internal helpers shared across the ir_builder* files.
 *
 * These are not part of the public IR API (see ir_api.h); they exist so
 * the builder's translation units can share make_vreg/make_instr/
 * append_instr without duplicating "extern" declarations in each file.
 */

#ifndef IR_BUILDER_H
#define IR_BUILDER_H

#include "ir.h"

IR_Value* make_vreg(IR_Builder* b, IR_Type* ty);
IR_Instr* make_instr(IR_Builder* b, IR_Opcode op, IR_Type* ty);
void      append_instr(IR_Builder* b, IR_Instr* inst);

#endif /* IR_BUILDER_H */
