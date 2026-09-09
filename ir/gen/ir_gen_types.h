/* ir_gen_types.h -- shared type definitions for the ir/gen/*.c walker
 * files (split out of ir_gen.h, B-23).  Included by ir_gen.h, so every
 * existing consumer compiles unchanged.
 *
 * This header holds the single definition of the generation context
 * (GenCtx) and the walker's helper structs.  A single GenCtx definition
 * matters: a previous copy in ir_gen_expr.c omitted `scope_top`, so the
 * struct layout differed between TUs and only worked because the common
 * field offsets happened to coincide. */

#ifndef IR_GEN_TYPES_H
#define IR_GEN_TYPES_H

#include "ir.h"

/* saved binding of a shadowed variable, restored when a scope exits */
typedef struct SymSave {
    String          name;
    IR_Value*       old_val;
    int             had_old;
    struct SymSave* next;
} SymSave;

/* storage location of one field of a bit-field struct: base + byte/bit
 * (ir_gen_bf.c).  width==8*size(field_ty) marks a plain (whole-type)
 * field.  plain_agg marks a plain field whose type is an aggregate
 * (struct/union/array): it owns its bytes, so load/store use a typed
 * byte-address access instead of the integer piece machinery (LLVM
 * forbids bitcast between integer and aggregate). */
typedef struct BfLoc {
    IR_Value* base;      /* pointer to the struct (element ptr for globals) */
    int       byte;      /* field's first byte offset in the struct */
    int       bit;       /* bit offset within that byte */
    int       width;     /* bit width (plain fields get 8*size) */
    int       is_signed;
    IR_Type*  field_ty;  /* the field's own IR type (value coercion) */
    int       record;    /* struct size in bytes (load/store piece bound) */
    int       plain_agg; /* plain field with aggregate type (typed access) */
} BfLoc;

/* generation context (one per function being lowered) */
typedef struct {
    IR_Builder*     b;
    HashMap         syms;       /* local variables: name -> IR_Value* (alloca) */
    HashMap         labels;     /* function-scope label -> IR_Block* (goto target) */
    HashMap*        sig_map;    /* module-level: func name -> IR_Type* (func type) */
    IR_Block*       break_blk;  /* target for break */
    IR_Block*       cont_blk;   /* target for continue */
    IR_Type*        ret_type;   /* enclosing function return type */
    IR_Module*      mod;        /* for global variable lookup */
    int             is_device;  /* 1 = device IR gen (GPU builtins), 0 = host */
    struct SymSave* scope_top;  /* saved shadowed symbols for scope restore */
} GenCtx;

/* typedef table entry (for resolving TYPE_NAMED during IR gen) */
typedef struct TypedefEntry {
    String               name;
    Type*                aliased_type;
    struct TypedefEntry* next;
} TypedefEntry;

/* struct definition entry for resolving TYPE_STRUCT references */
typedef struct StructDefEntry {
    String                  name;
    AST_Node*               fields;
    int                     is_union;
    struct StructDefEntry*  next;
} StructDefEntry;

/* context for the collect_local_struct_def_cb ast_walk callback */
typedef struct {
    HashMap* map;
    Arena*   a;
} LocalDefCtx;

/* one evaluated integer constant: value + type width/signedness.  The
 * width matters because C constant expressions are computed in the
 * operand's type: int arithmetic wraps at 32 bits
 * (0x7fffffff + 1 == -2147483648), while long/sizeof expressions stay
 * 64-bit.  Signed values are stored sign-extended to 64 bits, so
 * widening is a no-op and unsigned reinterprets are a mask. */
typedef struct {
    long long v;        /* value (sign-extended to 64 for signed) */
    int       bits;     /* 1, 8, 16, 32, or 64 */
    int       uns;      /* unsigned type? */
    int       is_float; /* float-valued result (mixed float arith) */
    double    f;        /* float result */
    int       is_ptr;   /* address constant (&g, &garr[1], &s.b, &g + k):
                           the global's address plus a byte offset.
                           ptr_off 0 dumps the bare @name; a nonzero
                           offset dumps getelementptr (i8, ptr @name,
                           i64 off).  ptr_elem is the pointee's byte
                           size, used to scale pointer + integer
                           arithmetic (p + 2 skips 2 pointees). */
    String    ptr_name; /* global name for is_ptr */
    long long ptr_off;  /* byte offset within the global (GEP) */
    int       ptr_elem; /* pointee byte size; 0 when unknown/non-object */
} ICEVal;

#endif /* IR_GEN_TYPES_H */
