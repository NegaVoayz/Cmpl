/* lr1.h -- LR(1) expression parser with function-pointer table */

#ifndef LR1_H
#define LR1_H

#include <stddef.h>

#include "token.h"
#include "ast.h"

typedef struct Arena Arena;

/* ---------------------------------------------------------------
 *  Parser states
 *
 *  Token-shifted states (0-19): a single terminal token was just
 *  pushed onto the stack. These states expect a reduce, or more
 *  tokens to complete the production.
 *
 *  Have-expr states (20-39): after reducing a subexpression.
 *  These states encode the precedence level of the expression
 *  on top of the value stack.
 * --------------------------------------------------------------- */

typedef enum {
    /* token-shifted states */
    S_ENTRY = 0,
    S_LIT,              /* shifted a literal token */
    S_IDENT,            /* shifted TOK_IDENT */
    S_LPAREN,           /* shifted ( -- disambiguate paren-expr vs cast */
    S_UNARY_OP,         /* shifted + - ! ~ * & as prefix unary op */
    S_SIZEOF,           /* shifted TOK_SIZEOF */
    S_PREFIX_INC,       /* shifted prefix ++ */
    S_PREFIX_DEC,       /* shifted prefix -- */
    S_POSTFIX_LBRACK,   /* shifted [ for array index */
    S_POSTFIX_LPAREN,   /* shifted ( for function call */
    S_POSTFIX_DOT,      /* shifted . for member access */
    S_POSTFIX_ARROW,    /* shifted -> for member access */
    S_POSTFIX_INC,      /* shifted postfix ++ */
    S_POSTFIX_DEC,      /* shifted postfix -- */
    S_POSTFIX_MEMBER,   /* shifted member name after . or -> */
    S_BINARY_OP,        /* shifted a binary operator token */
    S_ASSIGN_OP,        /* shifted = += -= *= /= */
    S_TERNARY_Q,        /* shifted ? */
    S_TERNARY_COLON,    /* shifted : */
    S_BINARY_RHS,       /* have LHS, shifted op, RHS at cast_expr+ -- ready to reduce or shift */
    S_BINRHS_PRIMARY,   /* RHS is primary, needs passthrough to higher level */
    S_BINRHS_POSTFIX,   /* RHS is postfix, needs passthrough */
    S_BINRHS_UNARY,     /* RHS is unary, needs passthrough to cast_expr */
    S_UNARY_RHS,        /* shifted unary prefix op, parsed operand -- ready to reduce */
    S_ASSIGN_RHS,       /* shifted assign op, parsed RHS -- ready to reduce */
    S_TERNARY_RHS,      /* have cond ? then : else -- ready to reduce ternary */
    S_GENERIC,          /* shifted TOK__GENERIC -- the whole selection is
                           parsed wholesale at its '(' (lr1_generic.c) */

    /* have-expr states (after reducing a subexpression) */
    HS_PRIMARY = 30,
    HS_POSTFIX,
    HS_UNARY,
    HS_CAST_EXPR,
    HS_MULT,
    HS_ADD,
    HS_SHIFT,
    HS_REL,
    HS_EQ,
    HS_BAND,
    HS_BXOR,
    HS_BOR,
    HS_LAND,
    HS_LOR,
    HS_COND,
    HS_ASSIGN,
    HS_EXPR,

    NUM_STATES
} LR1_State;

/* ---------------------------------------------------------------
 *  Nonterminal symbols for the GOTO table
 * --------------------------------------------------------------- */

typedef enum {
    SYM_PRIMARY = 0,
    SYM_POSTFIX,
    SYM_UNARY,
    SYM_CAST_EXPR,
    SYM_MULT,
    SYM_ADD,
    SYM_SHIFT,
    SYM_REL,
    SYM_EQ,
    SYM_BAND,
    SYM_BXOR,
    SYM_BOR,
    SYM_LAND,
    SYM_LOR,
    SYM_COND,
    SYM_ASSIGN,
    SYM_EXPR,
    SYM_ARG_LIST,
    NUM_SYMBOLS
} LR1_Symbol;

/* ---------------------------------------------------------------
 *  Action result and function pointer type
 * --------------------------------------------------------------- */

typedef enum {
    LR_ACCEPT,
    LR_SHIFT,
    LR_REDUCE,
    LR_ERROR
} LR_Action;

typedef struct LR1_Parser LR1_Parser;

typedef LR_Action (*LR1_Func)(LR1_Parser* p);

/* ---------------------------------------------------------------
 *  Parse stack and parser state
 * --------------------------------------------------------------- */

#define MAX_STACK 256

/* One pending (T) cast prefix in a cast chain.  Casts are unary-level
 * operators in C (cast-expression nests arbitrarily: (T1)(T2)...x), so
 * the chain must be unbounded — an arena-linked list.  A fixed-size
 * array silently dropped casts beyond its depth (wrong code, no
 * diagnostic).  Head = innermost cast, tail = outermost. */
typedef struct PendingCast {
    Type*               type;
    SourceLoc           loc;
    struct PendingCast* next;
} PendingCast;

typedef struct {
    int       state;
    Token*    token;
    AST_Node* node;
} StackFrame;

/* typedef-name registry node (defined in lr1.c) — for (ident)( casts */
typedef struct TypedefName TypedefName;

struct LR1_Parser {
    Token*     tok;
    StackFrame stack[MAX_STACK];
    int        sp;
    int        error;
    int        allow_unmatched_rparen;  /* for for-loop update expr terminated by ')' */
    int        stop_at_comma;           /* treat comma as expression terminator */
    int        pending_cast;            /* cast prefix was detected; wrap result */
    PendingCast* cast_chain;            /* pending cast prefixes, head = innermost */
    int        cast_count;             /* number of pending casts in the chain */
    int        cast_paren_depth;       /* paren depth when cast was set */
    int        cast_sp;               /* stack depth when cast was set */
    int        paren_depth;            /* current ()/[] nesting depth */
    TypedefName* typedefs;             /* typedef names seen so far (casts) */
    Arena*     arena;                  /* arena for AST node allocations */
};

/* ---------------------------------------------------------------
 *  Table access (defined in lr1_table.c)
 * --------------------------------------------------------------- */

extern LR1_Func action_table[NUM_STATES][NUM_TOKENS];
extern int     goto_table[NUM_STATES][NUM_SYMBOLS];

void lr1_table_init(void);

/* ---------------------------------------------------------------
 *  Public API
 * --------------------------------------------------------------- */

LR1_Parser* lr1_parser_new(Token* first_tok, Arena* a);
AST_Node*   lr1_parse_expr(LR1_Parser* p);

/* stack helper used by reduce functions */
void goto_push(LR1_Parser* p, AST_Node* node, int lhs_sym);
void goto_passthru(LR1_Parser* p, int lhs_sym);

/* ---------------------------------------------------------------
 *  Cast / sizeof(type) / compound-literal helpers
 *  (defined in lr1_cast.c and lr1_cast_apply.c)
 * --------------------------------------------------------------- */

int    is_cast_start(Token* tok);
int    is_postfix_token(TokenKind k);
int    is_cast_level(int s);
int    is_have_expr_state(LR1_State s);
Type*  ll_parse_type_name(LR1_Parser* p);

/* typedef-name registry (defined in lr1.c) */
void parser_add_typedef(LR1_Parser* p, String name);
int  parser_is_typedef(LR1_Parser* p, String name);

/* C11 _Generic selection (defined in lr1_generic.c): p->tok at the '('
 * after TOK__GENERIC; parses the whole selection, pushes the AST_GENERIC
 * node as a primary on the (restored) LR stack.  Returns 1 on success,
 * 0 with p->error set on malformed input. */
int lr1_parse_generic(LR1_Parser* p);

int       try_parse_cast(LR1_Parser* p, LR1_State state);
AST_Node* lr1_stop_at_comma(LR1_Parser* p, TokenKind next);
void      apply_pending_cast_at_reduce(LR1_Parser* p);
AST_Node* apply_pending_casts(LR1_Parser* p, AST_Node* operand);

#endif /* LR1_H */
