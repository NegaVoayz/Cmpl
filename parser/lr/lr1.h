/* lr1.h -- LR(1) expression parser with function-pointer table.
 *
 * The state/stack/parser struct types moved to lr1_types.h (included
 * below); this header keeps the table externs and the public API. */

#ifndef LR1_H
#define LR1_H

#include "lr1_types.h"

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

/* __builtin_va_arg(ap, type-name) (defined in lr1_va_arg.c): p->tok at
 * the '(' after TOK_BUILTIN_VA_ARG; parses the whole call, pushes the
 * AST_VA_ARG node as a primary on the (restored) LR stack.  Returns 1 on
 * success, 0 with p->error set on malformed input. */
int lr1_parse_va_arg(LR1_Parser* p);

/* Depth-aware separator scan shared by lr1_generic.c / lr1_va_arg.c:
 * parse one inner expression up to the first depth-0 separator (',' or the
 * construct's own ')'), arming it as the parser's stop token (the token's
 * kind is never touched).  p->tok is left AT the separator; *sep_out gets
 * it. */
AST_Node* parse_until_sep(LR1_Parser* p, Token** sep_out);

/* Save/restore the outer LR parse context around a re-entrant
 * lr1_parse_expr() (lr1_generic.c / lr1_va_arg.c); defined in lr1_save.c. */
void lr1_save_outer(LR1_Parser* p, LR1_Saved* saved);
void lr1_restore_outer(LR1_Parser* p, const LR1_Saved* saved);

int       try_parse_cast(LR1_Parser* p, LR1_State state);
AST_Node* lr1_stop_at_comma(LR1_Parser* p, TokenKind next);
void      apply_pending_cast_at_reduce(LR1_Parser* p);
AST_Node* apply_pending_casts(LR1_Parser* p, AST_Node* operand);
AST_Node* apply_pending_casts_where(LR1_Parser* p, AST_Node* operand,
                                    int min_pd, int min_sp);

#endif /* LR1_H */
