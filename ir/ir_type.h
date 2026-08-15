/* ir_type.h -- internal declarations shared across the ir_type*.c split.
 * ir_type.c owns singletons + composite cache; ir_type_ast.c owns AST->IR
 * conversion + the struct dedup cache; ir_type_struct.c owns the IR->AST
 * struct map. */

#ifndef IR_TYPE_H
#define IR_TYPE_H

#include "ir.h"

/* AST -> IR type conversion (ir_type_ast.c) */
IR_Type* ast_to_ir_type(Arena* a, Type* ast);

/* struct-dedup cache reset (ir_type_ast.c) */
void ir_reset_struct_caches(void);

/* IR_Type -> AST Type mapping (ir_type_struct.c) */
void register_struct_ast(IR_Type* ir, Type* ast);
void register_clone_ast(IR_Type* clone, IR_Type* src);
void ir_reset_ast_map(void);

#endif /* IR_TYPE_H */
