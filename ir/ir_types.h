/* ir_types.h -- the IR type/value model: IR_Type, IR_Value and their kind
 * enums (split out of ir.h, B-23).  The program structures (instructions,
 * blocks, functions, module, builder) live in ir_program.h.  Both are
 * included by ir.h, so every existing consumer compiles unchanged. */

#ifndef IR_TYPES_H
#define IR_TYPES_H

#include <stdio.h>

#include "ast.h"          /* for String, Type, AST_Node */
#include "arena.h"        /* for Arena */
#include "hash.h"         /* for HashMap */

/* forward */
typedef struct IR_Block IR_Block;
typedef struct IR_Instr IR_Instr;

/* ---------------------------------------------------------------
 *  IR type kinds -- maps to LLVM type primitives
 * --------------------------------------------------------------- */

typedef enum {
    IR_VOID, IR_I1,
    IR_I8, IR_I16, IR_I32, IR_I64,
    IR_F32, IR_F64,
    IR_PTR, IR_ARRAY, IR_STRUCT, IR_UNION, IR_FUNC
} IR_TypeKind;

/* ---------------------------------------------------------------
 *  IR type (recursive, similar to AST Type)
 * --------------------------------------------------------------- */

typedef struct IR_Type IR_Type;

/* per-field storage layout for structs that contain bit-fields, built by
 * ir_type_bf.c.  Parallel to the AST field list (same order, incl.
 * anonymous fields).  `unit` indexes the storage-unit member list (see
 * t->members) and `byte_off` is the unit's byte offset within the struct;
 * `bit` is the field's bit offset within that unit.  width==0 marks a
 * plain (whole-type) field. */
typedef struct IR_FieldInfo {
    int   unit;         /* storage-unit member index (0..n_members-1) */
    int   byte_off;     /* unit's byte offset in the struct */
    int   bit;          /* field's bit offset within the unit */
    int   width;        /* bit-field width; 0 = plain field */
    int   is_signed;
    IR_Type* ty;        /* field's own IR type (i8/i16/i32/i64/i1) */
    struct IR_FieldInfo* next;
} IR_FieldInfo;

struct IR_Type {
    IR_TypeKind kind;
    IR_Type*    inner;       /* pointee / array elem / return type */
    int         size;        /* array element count */
    int         addrspace;   /* 0=host, 1=device global, 2=shared, 3=constant */
    unsigned    is_variadic : 1; /* function type has ... */
    unsigned    is_unsigned : 1; /* integer type is unsigned (0 = signed) */
    String      name;        /* struct tag */
    IR_Type*    members;     /* struct fields / func params (linked via next) */
    IR_Type*    next;        /* chain for members / named_types list */
    unsigned    has_bitfields : 1; /* struct has >=1 bit-field: members are
                                      storage units, field_info is populated */
    IR_FieldInfo* field_info;  /* per-field storage layout (bitfield structs) */
    int         align;         /* explicit alignment override (bytes); 0 = auto
                                  (set for bitfield structs: max field align) */
};

/* ---------------------------------------------------------------
 *  IR value kinds
 * --------------------------------------------------------------- */

typedef enum {
    VAL_CONST_INT,
    VAL_CONST_FLOAT,
    VAL_CONST_NULL,
    VAL_CONST_STRING,
    VAL_CONST_AGGREGATE,
    VAL_CONST_BITCAST,
    VAL_CONST_INTTOPTR,
    VAL_PARAM,
    VAL_INSTR,
    VAL_GLOBAL,
    VAL_GLOBAL_GEP, /* address of a global + byte offset (body.int_val) */
    VAL_UNDEF
} IR_ValueKind;

/* ---------------------------------------------------------------
 *  IR value (SSA register, constant, or global)
 * --------------------------------------------------------------- */

typedef struct IR_Value IR_Value;
struct IR_Value {
    IR_ValueKind kind;
    IR_Type*     type;
    String       name;        /* @global_name or %vreg name (optional) */
    int          id;          /* auto-increment numeric ID */
    IR_Value*    next;        /* chain for global list */
    union {
        long long  int_val;
        double     float_val;
        String     str_val;
        IR_Value*  init_val;  /* global initializer */
        IR_Value*  cast_val;  /* const bitcast/inttoptr operand */
        struct {
            IR_Value** elems;
            int        count;
        } aggregate;
    } body;
    int          linkage;    /* for globals: 0=internal(static), 1=external */
    unsigned     is_wide : 1; /* VAL_CONST_STRING with 4-byte wchar (i32)
                                 elements (L"..." literal) */
    IR_Instr*    def_instr;   /* instruction that defines this value (or NULL) */
    IR_Instr**   uses;        /* dynamic: instructions that use this value */
    int          n_uses;
    int          max_uses;
};

#endif /* IR_TYPES_H */
