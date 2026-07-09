/* lr1_reduce.c -- reduction functions for the LR(1) expression parser
 *
 * Each reduction function pops N stack frames, builds the appropriate
 * AST_Node, and pushes the result via goto_push() with its LHS symbol.
 */

#include "lr1.h"

/* forward: shift function used by lr1_binary_rhs_action */
LR_Action shift_binary_op(LR1_Parser* p);
LR_Action shift_ternary_colon(LR1_Parser* p);

/* precedence of a binary/assign operator (1=highest, 11=lowest) */
int prec_of(TokenKind k)
{
    switch (k) {
    case TOK_STAR: case TOK_SLASH: case TOK_PERCENT:  return 1;
    case TOK_PLUS: case TOK_MINUS:                    return 2;
    case TOK_LTLT: case TOK_GTGT:                     return 3;
    case TOK_LT: case TOK_GT: case TOK_LTEQ: case TOK_GTEQ: return 4;
    case TOK_EQEQ: case TOK_BANGEQ:                   return 5;
    case TOK_AMP:                                     return 6;
    case TOK_CARET:                                   return 7;
    case TOK_PIPE:                                    return 8;
    case TOK_AMPAMP:                                  return 9;
    case TOK_PIPEPIPE:                                return 10;
    case TOK_COMMA:                                   return 12;
    default:                                          return 11;
    }
}

/* ===========================================================
 *  Primary expression reductions (pop 1 token, build leaf)
 * =========================================================== */

LR_Action reduce_primary_lit(LR1_Parser* p)
{
    Token* t = p->stack[p->sp].token;
    AST_Type kind;

    switch (t->kind) {
    case TOK_INT_LIT:    kind = AST_INT_LIT;    break;
    case TOK_LONG_LIT:   kind = AST_LONG_LIT;   break;
    case TOK_CHAR_LIT:   kind = AST_CHAR_LIT;   break;
    case TOK_STRING_LIT: kind = AST_STRING_LIT; break;
    case TOK_FLOAT_LIT:  kind = AST_FLOAT_LIT;  break;
    case TOK_DOUBLE_LIT: kind = AST_DOUBLE_LIT; break;
    default:             return LR_ERROR;
    }

    AST_Node* n = ast_node_new(kind, t->loc.line, t->loc.col);

    switch (t->kind) {
    case TOK_INT_LIT:  case TOK_LONG_LIT:
        n->body.literal.int_val = t->body.int_val;   break;
    case TOK_CHAR_LIT:
        n->body.literal.char_val = t->body.char_val; break;
    case TOK_FLOAT_LIT: case TOK_DOUBLE_LIT:
        n->body.literal.float_val = t->body.float_val; break;
    case TOK_STRING_LIT:
        n->body.literal.str_val = t->body.str_val;   break;
    default: break;
    }

    p->sp--;
    goto_push(p, n, SYM_PRIMARY);

    return LR_REDUCE;
}

LR_Action reduce_primary_ident(LR1_Parser* p)
{
    Token* t = p->stack[p->sp].token;

    AST_Node* n = ast_node_new(AST_IDENT, t->loc.line, t->loc.col);

    n->body.ident.name = t->body.ident;
    p->sp--;
    goto_push(p, n, SYM_PRIMARY);

    return LR_REDUCE;
}

LR_Action reduce_primary_paren(LR1_Parser* p)
{
    /* pop 3: TOK_LPAREN, expr, TOK_RPAREN */
    AST_Node* inner = p->stack[p->sp - 1].node;

    p->sp -= 3;
    goto_push(p, inner, SYM_PRIMARY);

    return LR_REDUCE;
}

/* close a parenthesized expression: stack has [LPAREN, expr], current token is ) */
LR_Action reduce_primary_paren_close(LR1_Parser* p)
{
    AST_Node* inner = p->stack[p->sp].node;

    p->sp -= 2;
    p->tok = p->tok->next;
    goto_push(p, inner, SYM_PRIMARY);

    return LR_REDUCE;
}

/* handle ) when inside function call args */
LR_Action reduce_call_close(LR1_Parser* p)
{
    /* stack: [..., postfix, POSTFIX_LPAREN, args..., current_expr]
     * current token: )
     * Find the POSTFIX_LPAREN frame, build arg list, reduce call */
    int lparen_idx = p->sp - 1;

    while (lparen_idx >= 0 && p->stack[lparen_idx].state != S_POSTFIX_LPAREN)
        lparen_idx--;

    if (lparen_idx < 0) return LR_ERROR;

    /* collect args from lparen_idx+1 to sp into a linked list */
    AST_Node* args = NULL;
    AST_Node** tail = &args;

    for (int i = lparen_idx + 1; i <= p->sp; i++) {
        if (p->stack[i].node) {
            *tail = p->stack[i].node;
            while ((*tail)->next)
                *tail = (*tail)->next;
            tail = &(*tail)->next;
        }
    }

    AST_Node* callee = p->stack[lparen_idx - 1].node;
    AST_Node* n = ast_node_new(AST_CALL, callee->loc.line, callee->loc.col);

    n->body.call.callee = callee;
    n->body.call.args = args;

    /* skip both LPAREN and callee frames so dead callee doesn't
     * interfere with passthrough chain */
    p->sp = lparen_idx - 2;
    p->tok = p->tok->next;
    goto_push(p, n, SYM_POSTFIX);

    return LR_REDUCE;
}

/* ===========================================================
 *  Passthrough reductions (pop 1, repush at higher level)
 * =========================================================== */

LR_Action reduce_to_postfix(LR1_Parser* p)
{
    AST_Node* n = p->stack[p->sp].node;

    p->sp--;
    goto_push(p, n, SYM_POSTFIX);

    return LR_REDUCE;
}

LR_Action reduce_to_unary(LR1_Parser* p)
{
    AST_Node* n = p->stack[p->sp].node;

    p->sp--;
    goto_push(p, n, SYM_UNARY);

    return LR_REDUCE;
}

LR_Action reduce_to_cast(LR1_Parser* p)
{
    AST_Node* n = p->stack[p->sp].node;

    p->sp--;
    goto_push(p, n, SYM_CAST_EXPR);

    return LR_REDUCE;
}

LR_Action reduce_to_mult(LR1_Parser* p)
{
    AST_Node* n = p->stack[p->sp].node;

    p->sp--;
    goto_push(p, n, SYM_MULT);

    return LR_REDUCE;
}

LR_Action reduce_to_add(LR1_Parser* p)
{
    AST_Node* n = p->stack[p->sp].node;

    p->sp--;
    goto_push(p, n, SYM_ADD);

    return LR_REDUCE;
}

LR_Action reduce_to_shift(LR1_Parser* p)
{
    AST_Node* n = p->stack[p->sp].node;

    p->sp--;
    goto_push(p, n, SYM_SHIFT);

    return LR_REDUCE;
}

LR_Action reduce_to_rel(LR1_Parser* p)
{
    AST_Node* n = p->stack[p->sp].node;

    p->sp--;
    goto_push(p, n, SYM_REL);

    return LR_REDUCE;
}

LR_Action reduce_to_eq(LR1_Parser* p)
{
    AST_Node* n = p->stack[p->sp].node;

    p->sp--;
    goto_push(p, n, SYM_EQ);

    return LR_REDUCE;
}

LR_Action reduce_to_band(LR1_Parser* p)
{
    AST_Node* n = p->stack[p->sp].node;

    p->sp--;
    goto_push(p, n, SYM_BAND);

    return LR_REDUCE;
}

LR_Action reduce_to_bxor(LR1_Parser* p)
{
    AST_Node* n = p->stack[p->sp].node;

    p->sp--;
    goto_push(p, n, SYM_BXOR);

    return LR_REDUCE;
}

LR_Action reduce_to_bor(LR1_Parser* p)
{
    AST_Node* n = p->stack[p->sp].node;

    p->sp--;
    goto_push(p, n, SYM_BOR);

    return LR_REDUCE;
}

LR_Action reduce_to_land(LR1_Parser* p)
{
    AST_Node* n = p->stack[p->sp].node;

    p->sp--;
    goto_push(p, n, SYM_LAND);

    return LR_REDUCE;
}

LR_Action reduce_to_lor(LR1_Parser* p)
{
    AST_Node* n = p->stack[p->sp].node;

    p->sp--;
    goto_push(p, n, SYM_LOR);

    return LR_REDUCE;
}

LR_Action reduce_to_cond(LR1_Parser* p)
{
    AST_Node* n = p->stack[p->sp].node;

    p->sp--;
    goto_push(p, n, SYM_COND);

    return LR_REDUCE;
}

LR_Action reduce_to_assign(LR1_Parser* p)
{
    AST_Node* n = p->stack[p->sp].node;

    p->sp--;
    goto_push(p, n, SYM_ASSIGN);

    return LR_REDUCE;
}

LR_Action reduce_to_expr(LR1_Parser* p)
{
    AST_Node* n = p->stack[p->sp].node;

    p->sp--;
    goto_push(p, n, SYM_EXPR);

    return LR_REDUCE;
}

/* ===========================================================
 *  Postfix reductions
 * =========================================================== */

LR_Action reduce_index(LR1_Parser* p)
{
    /* pop 3: postfix, TOK_LBRACKET, expr.  ] is the current token. */
    AST_Node* array = p->stack[p->sp - 2].node;
    AST_Node* idx   = p->stack[p->sp].node;
    AST_Node* n = ast_node_new(AST_INDEX, array->loc.line, array->loc.col);

    n->body.subscript.array = array;
    n->body.subscript.index = idx;
    p->sp -= 3;
    p->tok = p->tok->next;
    goto_push(p, n, SYM_POSTFIX);

    return LR_REDUCE;
}

LR_Action reduce_call_empty(LR1_Parser* p)
{
    /* pop 3: postfix, TOK_LPAREN, TOK_RPAREN */
    AST_Node* callee = p->stack[p->sp - 2].node;
    AST_Node* n = ast_node_new(AST_CALL, callee->loc.line, callee->loc.col);

    n->body.call.callee = callee;
    n->body.call.args = NULL;
    p->sp -= 3;
    goto_push(p, n, SYM_POSTFIX);

    return LR_REDUCE;
}

LR_Action reduce_call_args(LR1_Parser* p)
{
    /* pop 4: postfix, TOK_LPAREN, arg_list, TOK_RPAREN */
    AST_Node* callee = p->stack[p->sp - 3].node;
    AST_Node* args   = p->stack[p->sp - 1].node;
    AST_Node* n = ast_node_new(AST_CALL, callee->loc.line, callee->loc.col);

    n->body.call.callee = callee;
    n->body.call.args = args;
    p->sp -= 4;
    goto_push(p, n, SYM_POSTFIX);

    return LR_REDUCE;
}

LR_Action reduce_member_access(LR1_Parser* p)
{
    /* pop 3: postfix, op_token (DOT or ARROW), TOK_IDENT */
    AST_Node* record = p->stack[p->sp - 2].node;
    Token*    op_tok = p->stack[p->sp - 1].token;
    Token*    id_tok = p->stack[p->sp].token;
    AST_Node* n = ast_node_new(AST_MEMBER, record->loc.line, record->loc.col);

    n->body.member.record = record;
    n->body.member.member = id_tok->body.ident;
    n->body.member.op = op_tok->kind;
    p->sp -= 3;
    goto_push(p, n, SYM_POSTFIX);

    return LR_REDUCE;
}

LR_Action reduce_postfix_inc(LR1_Parser* p)
{
    /* pop 2: postfix, TOK_PLUSPLUS */
    AST_Node* operand = p->stack[p->sp - 1].node;

    AST_Node* n = ast_node_new(AST_POSTFIX, operand->loc.line, operand->loc.col);

    n->body.postfix.operand = operand;
    n->body.postfix.op = TOK_PLUSPLUS;
    p->sp -= 2;
    goto_push(p, n, SYM_POSTFIX);

    return LR_REDUCE;
}

LR_Action reduce_postfix_dec(LR1_Parser* p)
{
    /* pop 2: postfix, TOK_MINUSMINUS */
    AST_Node* operand = p->stack[p->sp - 1].node;

    AST_Node* n = ast_node_new(AST_POSTFIX, operand->loc.line, operand->loc.col);

    n->body.postfix.operand = operand;
    n->body.postfix.op = TOK_MINUSMINUS;
    p->sp -= 2;
    goto_push(p, n, SYM_POSTFIX);

    return LR_REDUCE;
}

/* ===========================================================
 *  Unary prefix reductions
 * =========================================================== */

LR_Action reduce_unary_prefix(LR1_Parser* p)
{
    /* pop 2: unary_op token, cast_expr */
    Token*    op_tok  = p->stack[p->sp - 1].token;
    AST_Node* operand = p->stack[p->sp].node;
    AST_Node* n = ast_node_new(AST_UNARY, op_tok->loc.line, op_tok->loc.col);

    n->body.unary.operand = operand;
    n->body.unary.op = op_tok->kind;
    p->sp -= 2;
    goto_push(p, n, SYM_UNARY);

    return LR_REDUCE;
}

LR_Action reduce_prefix_inc(LR1_Parser* p)
{
    /* pop 2: TOK_PLUSPLUS, unary -- rewrite as AST_UNARY */
    Token*    op_tok  = p->stack[p->sp - 1].token;
    AST_Node* operand = p->stack[p->sp].node;
    AST_Node* n = ast_node_new(AST_UNARY, op_tok->loc.line, op_tok->loc.col);

    n->body.unary.operand = operand;
    n->body.unary.op = TOK_PLUSPLUS;
    p->sp -= 2;
    goto_push(p, n, SYM_UNARY);

    return LR_REDUCE;
}

LR_Action reduce_prefix_dec(LR1_Parser* p)
{
    /* pop 2: TOK_MINUSMINUS, unary -- rewrite as AST_UNARY */
    Token*    op_tok  = p->stack[p->sp - 1].token;
    AST_Node* operand = p->stack[p->sp].node;
    AST_Node* n = ast_node_new(AST_UNARY, op_tok->loc.line, op_tok->loc.col);

    n->body.unary.operand = operand;
    n->body.unary.op = TOK_MINUSMINUS;
    p->sp -= 2;
    goto_push(p, n, SYM_UNARY);

    return LR_REDUCE;
}

LR_Action reduce_sizeof_expr(LR1_Parser* p)
{
    /* pop 2: TOK_SIZEOF, unary */
    AST_Node* expr = p->stack[p->sp].node;
    Token*    tok  = p->stack[p->sp - 1].token;
    AST_Node* n = ast_node_new(AST_SIZEOF_EXPR, tok->loc.line, tok->loc.col);

    n->body.sizeof_expr.expr = expr;
    p->sp -= 2;
    goto_push(p, n, SYM_UNARY);

    return LR_REDUCE;
}

/* ===========================================================
 *  Binary operator reduction (generic)
 * =========================================================== */

LR_Action reduce_binary(LR1_Parser* p, int lhs_sym)
{
    /* pop 3: left, op_token, right */
    AST_Node* left  = p->stack[p->sp - 2].node;
    Token*    op    = p->stack[p->sp - 1].token;
    AST_Node* right = p->stack[p->sp].node;
    AST_Node* n = ast_node_new(AST_BINARY, op->loc.line, op->loc.col);

    n->body.binary.left = left;
    n->body.binary.right = right;
    n->body.binary.op = op->kind;
    p->sp -= 3;
    goto_push(p, n, lhs_sym);

    return LR_REDUCE;
}

/* wrappers for each binary precedence level */

LR_Action reduce_mult(LR1_Parser* p)   { return reduce_binary(p, SYM_MULT); }
LR_Action reduce_add(LR1_Parser* p)    { return reduce_binary(p, SYM_ADD); }
LR_Action reduce_shift(LR1_Parser* p)  { return reduce_binary(p, SYM_SHIFT); }
LR_Action reduce_rel(LR1_Parser* p)    { return reduce_binary(p, SYM_REL); }
LR_Action reduce_eq(LR1_Parser* p)     { return reduce_binary(p, SYM_EQ); }
LR_Action reduce_band(LR1_Parser* p)   { return reduce_binary(p, SYM_BAND); }
LR_Action reduce_bxor(LR1_Parser* p)   { return reduce_binary(p, SYM_BXOR); }
LR_Action reduce_bor(LR1_Parser* p)    { return reduce_binary(p, SYM_BOR); }
LR_Action reduce_land(LR1_Parser* p)   { return reduce_binary(p, SYM_LAND); }
LR_Action reduce_lor(LR1_Parser* p)    { return reduce_binary(p, SYM_LOR); }
LR_Action reduce_assign(LR1_Parser* p) { return reduce_binary(p, SYM_ASSIGN); }
LR_Action reduce_comma(LR1_Parser* p)  { return reduce_binary(p, SYM_EXPR); }

/* ===========================================================
 *  Ternary reduction
 * =========================================================== */

LR_Action reduce_ternary(LR1_Parser* p)
{
    /* pop 5: cond, TOK_QUESTION, expr, TOK_COLON, cond */
    AST_Node* cond  = p->stack[p->sp - 4].node;
    AST_Node* then_expr = p->stack[p->sp - 2].node;
    AST_Node* else_expr = p->stack[p->sp].node;
    AST_Node* n = ast_node_new(AST_TERNARY, cond->loc.line, cond->loc.col);

    n->body.ternary.cond = cond;
    n->body.ternary.then_expr = then_expr;
    n->body.ternary.else_expr = else_expr;
    p->sp -= 5;
    goto_push(p, n, SYM_COND);

    return LR_REDUCE;
}

/* ===========================================================
 *  Argument list reductions
 * =========================================================== */

LR_Action reduce_empty_args(LR1_Parser* p)
{
    /* push NULL node for empty arg list (epsilon production) */

    goto_push(p, NULL, SYM_ARG_LIST);

    return LR_REDUCE;
}

LR_Action reduce_arg_single(LR1_Parser* p)
{
    /* pop 1: expr — it becomes the arg list head */
    AST_Node* expr = p->stack[p->sp].node;

    p->sp--;
    goto_push(p, expr, SYM_ARG_LIST);

    return LR_REDUCE;
}

LR_Action reduce_arg_append(LR1_Parser* p)
{
    /* pop 3: arg_list, TOK_COMMA, expr */
    AST_Node* list = p->stack[p->sp - 2].node;
    AST_Node* expr = p->stack[p->sp].node;

    /* append new argument to end of list */
    AST_Node* cur = list;

    while (cur->next)
        cur = cur->next;
    cur->next = expr;
    p->sp -= 3;
    goto_push(p, list, SYM_ARG_LIST);

    return LR_REDUCE;
}

/* ===========================================================
 *  Binary operator RHS action -- used from S_BINARY_RHS
 * =========================================================== */

int op_to_lhs(TokenKind k)
{
    switch (k) {
    case TOK_STAR: case TOK_SLASH: case TOK_PERCENT:
        return SYM_MULT;
    case TOK_PLUS: case TOK_MINUS:
        return SYM_ADD;
    case TOK_LTLT: case TOK_GTGT:
        return SYM_SHIFT;
    case TOK_LT: case TOK_GT: case TOK_LTEQ: case TOK_GTEQ:
        return SYM_REL;
    case TOK_EQEQ: case TOK_BANGEQ:
        return SYM_EQ;
    case TOK_AMP:
        return SYM_BAND;
    case TOK_CARET:
        return SYM_BXOR;
    case TOK_PIPE:
        return SYM_BOR;
    case TOK_AMPAMP:
        return SYM_LAND;
    case TOK_PIPEPIPE:
        return SYM_LOR;
    case TOK_COMMA:
        return SYM_EXPR;
    default:
        return SYM_ASSIGN;
    }
}

LR_Action reduce_binary_op(LR1_Parser* p)
{
    Token* op = p->stack[p->sp - 1].token;
    int lhs = op_to_lhs(op->kind);

    return reduce_binary(p, lhs);
}

LR_Action lr1_binary_rhs_action(LR1_Parser* p)
{
    /* stack: [..., LHS, S_BINARY_OP(op_token), S_BINARY_RHS(RHS_node)]
     * current token: a binary operator
     * Compare precedence of STACK_OP vs NEXT_OP. */
    Token* stack_op = p->stack[p->sp - 1].token;
    TokenKind next_op = p->tok->kind;
    int prec_stack = prec_of(stack_op->kind);
    int prec_next = prec_of(next_op);

    if (prec_next < prec_stack) {
        /* next binds tighter -- shift it (don't reduce stack binary) */
        return shift_binary_op(p);
    } else {
        /* stack binds tighter or equal (left-assoc) -- reduce stack binary */
        return reduce_binary_op(p);
    }
}

/* ===========================================================
 *  Unary RHS reduction (called from S_UNARY_RHS)
 * =========================================================== */

LR_Action reduce_unary_rhs(LR1_Parser* p)
{
    /* stack: [..., op_token (UNARY_OP/PREFIX_INC/PREFIX_DEC/SIZEOF), operand]
     * Pop 2 frames, build AST_UNARY or AST_SIZEOF_EXPR */
    Token*    op_tok  = p->stack[p->sp - 1].token;
    AST_Node* operand = p->stack[p->sp].node;
    TokenKind op = op_tok->kind;

    p->sp -= 2;

    if (op == TOK_SIZEOF) {
        AST_Node* n = ast_node_new(AST_SIZEOF_EXPR, op_tok->loc.line, op_tok->loc.col);

        n->body.sizeof_expr.expr = operand;
        goto_push(p, n, SYM_UNARY);

        return LR_REDUCE;
    }

    AST_Node* n = ast_node_new(AST_UNARY, op_tok->loc.line, op_tok->loc.col);

    n->body.unary.operand = operand;
    n->body.unary.op = op;
    goto_push(p, n, SYM_UNARY);

    return LR_REDUCE;
}

/* ===========================================================
 *  Context-aware , handler (arg separator vs binary comma)
 * =========================================================== */

LR_Action lr1_handle_comma(LR1_Parser* p)
{
    /* check if we're inside a function call argument list */
    for (int i = p->sp; i >= 0; i--) {
        if (p->stack[i].state == S_POSTFIX_LPAREN) {
            /* Inside call args -- current expr is an argument.
             * Reduce it to SYM_ARG_LIST and consume comma. */
            AST_Node* expr = p->stack[p->sp].node;

            p->sp--;
            goto_push(p, expr, SYM_ARG_LIST);
            p->tok = p->tok->next;

            return LR_REDUCE;
        }
    }

    /* Not in args -- regular comma operator */
    return shift_binary_op(p);
}

/* ===========================================================
 *  Context-aware : handler (ternary vs label)
 * =========================================================== */

LR_Action lr1_handle_colon(LR1_Parser* p)
{
    /* check if we're inside a ternary (? ... :) */
    for (int i = p->sp; i >= 0; i--) {
        if (p->stack[i].state == S_TERNARY_Q)
            return shift_ternary_colon(p);
    }

    return LR_ACCEPT;
}

/* ===========================================================
 *  Context-aware ) handler
 * =========================================================== */

LR_Action lr1_handle_rparen(LR1_Parser* p)
{
    int is_rbracket = (p->tok->kind == TOK_RBRACKET);
    TokenKind match_kind = is_rbracket ? TOK_LBRACKET : TOK_LPAREN;

    /* look for matching LPAREN or LBRACKET on the stack */
    for (int i = p->sp; i >= 0; i--) {
        if (p->stack[i].token && p->stack[i].token->kind == match_kind) {
            if (is_rbracket && p->stack[i].state == S_POSTFIX_LBRACK)
                return reduce_index(p);
            if (!is_rbracket && p->stack[i].state == S_LPAREN)
                return reduce_primary_paren_close(p);
            if (!is_rbracket && p->stack[i].state == S_POSTFIX_LPAREN)
                return reduce_call_close(p);
            return LR_ERROR;
        }
    }

    /* no matching bracket -- if caller allows, consume and accept */
    if (p->allow_unmatched_rparen) {
        p->allow_unmatched_rparen = 0;
        p->tok = p->tok->next;
        return LR_ACCEPT;
    }

    return LR_ERROR;
}

/* ===========================================================
 *  Accept and error
 * =========================================================== */

LR_Action lr1_accept(LR1_Parser* p)
{
    return LR_ACCEPT;
}

LR_Action lr1_error(LR1_Parser* p)
{
    return LR_ERROR;
}
