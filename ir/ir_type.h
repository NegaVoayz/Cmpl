/* ir_type.h -- internal declarations shared across the ir_type*.c split.
 * ir_type.c owns singletons + composite cache; ir_type_ast.c owns AST->IR
 * conversion + the struct dedup cache; ir_type_func.c owns the function-form
 * conversion; ir_type_struct.c owns the IR->AST struct map. */

#ifndef IR_TYPE_H
#define IR_TYPE_H

#include "ir.h"

/* AST -> IR type conversion (ir_type_ast.c) */
IR_Type* ast_to_ir_type(Arena* a, Type* ast);
IR_Type* ast_to_ir_type_ctx(Arena* a, Type* ast, int pointee);

/* function-form conversion + shared chain-clone/signedness helpers
 * (ir_type_func.c) */
IR_Type* ast_to_func_type(Arena* a, Type* ast, int pointee);
IR_Type* clone_type_for_chain(Arena* a, IR_Type* src);
IR_Type* unsigned_of(IR_Type* t);

/* struct-dedup cache reset (ir_type_ast.c) */
void ir_reset_struct_caches(void);

/* IR_Type -> AST Type mapping (ir_type_struct.c) */
void register_struct_ast(IR_Type* ir, Type* ast);
void register_clone_ast(IR_Type* clone, IR_Type* src);
void ir_reset_ast_map(void);

/* per-field layout temp (pass 1), shared across the bit-field split */
typedef struct Lay {
    int byte_off;   /* storage-unit start byte */
    int bit;        /* bit offset within the unit */
    int width;      /* bit-field width; 0 = regular field */
    int is_signed;
} Lay;

/* bit-field layout (ir_type_bf.c / ir_type_bf_emit.c) */
void ir_build_bitfield_struct(Arena* a, IR_Type* t, Type* ast);
void ir_build_bitfield_members(Arena* a, IR_Type* t, int record_bytes,
                               int align);
void ir_build_field_info(Arena* a, IR_Type* t, Type* ast, Lay* lays,
                         int n_lays);

/* shared bit-field helpers (ir_type_bf_emit.c) */
void field_base_info(Arena* a, Type* var_type, int* size, int* is_signed);
long long eval_width(AST_Node* e);
int round_up(int v, int mul);

/* bit-field struct queries (ir_type_bfq.c) */
int ir_has_bitfields(IR_Type* t);
IR_FieldInfo* ir_field_info(IR_Type* t, int raw_idx);
int ir_struct_field_count(IR_Type* t);
int ir_struct_named_count(IR_Type* t);
int ir_struct_named_at(IR_Type* t, int named_idx);
int ir_struct_next_named(IR_Type* t, int raw_idx);
int ir_struct_named_before(IR_Type* t, int raw_idx);
int ir_struct_member_at(IR_Type* t, int byte_off, int* mstart);

#endif /* IR_TYPE_H */
