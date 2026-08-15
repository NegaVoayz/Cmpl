/* ir_dump.h -- internal cross-file declarations for the ir/dump/*.c printers.
 *
 * The module dumper is split across ir_dump.c (type/value/condition
 * printers), ir_dump_instr.c (instruction printer), ir_dump_str.c (string
 * globals), ir_dump_struct.c (struct collector/emitter), ir_dump_func.c
 * (block/function printers) and ir_dump_module.c (module pass driver).
 * This header replaces the "extern" soup each file used to duplicate.
 */

#ifndef IR_DUMP_H
#define IR_DUMP_H

#include <stdio.h>

#include "ir.h"

/* --- type / value / condition printers (ir_dump.c) --- */
void        dump_type(FILE* out, IR_Type* ty);
void        dump_value(FILE* out, IR_Value* val);
const char* cond_str(IR_Cond cond);
const char* fcmp_cond_str(IR_Cond cond);

/* shared anonymous-struct name table (defined in ir_dump.c) */
extern IR_Type* dump_anon_types[IR_MAX_ANON_TYPES];
extern int      dump_anon_count;

/* --- instruction printer (ir_dump_instr.c) --- */
void dump_instr(FILE* out, IR_Instr* inst);

/* --- string constant table (ir_dump_str.c) --- */
void dump_str_reset(void);
void dump_str_globals(FILE* out);
void dump_str_collect_module(IR_Module* mod);
int  dump_str_index(String s);

/* --- struct type collector/emitter (ir_dump_struct.c) --- */
void emit_struct_types(FILE* out, IR_Module* mod);

/* --- block / function printers (ir_dump_func.c) --- */
void dump_func(FILE* out, IR_Func* func);

#endif /* IR_DUMP_H */
