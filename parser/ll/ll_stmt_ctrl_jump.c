/* ll_stmt_ctrl_jump.c -- LL parser for jump and switch statements
 *
 * return, break, continue, switch / case / default.
 * Control-flow (if, while, do-while, for) are in ll_stmt_ctrl.c.
 */

#include "ll.h"

extern void ll_expect(LR1_Parser* p, TokenKind k);

/* ===========================================================
 *  return
 * =========================================================== */

AST_Node* ll_parse_return(LR1_Parser* p)
{
    AST_Node* n = ll_stmt_begin(p, AST_RETURN);

    if (p->tok->kind != TOK_SEMI)
        n->body.ret.expr = ll_parse_expr(p);
    else
        n->body.ret.expr = NULL;

    ll_expect(p, TOK_SEMI);

    return n;
}

/* ===========================================================
 *  break / continue
 * =========================================================== */

AST_Node* ll_parse_break(LR1_Parser* p)
{
    AST_Node* n = ll_stmt_begin(p, AST_BREAK);

    ll_expect(p, TOK_SEMI);

    return n;
}

AST_Node* ll_parse_continue(LR1_Parser* p)
{
    AST_Node* n = ll_stmt_begin(p, AST_CONTINUE);

    ll_expect(p, TOK_SEMI);

    return n;
}

/* ===========================================================
 *  switch / case / default
 * =========================================================== */

AST_Node* ll_parse_switch(LR1_Parser* p)
{
    AST_Node* n = ll_stmt_begin(p, AST_SWITCH);

    ll_expect(p, TOK_LPAREN);
    p->allow_unmatched_rparen = 1;
    n->body.switch_stmt.condition = ll_parse_expr(p);
    n->body.switch_stmt.body = ll_parse_stmt(p);

    return n;
}

AST_Node* ll_parse_case(LR1_Parser* p)
{
    AST_Node* n = ll_stmt_begin(p, AST_CASE);

    n->body.case_stmt.value = ll_parse_expr(p);
    ll_expect(p, TOK_COLON);

    /* parse all statements until next case/default/closing brace.
     * chain them via ->next (a single stmt field holds the head). */
    { AST_Node* head = NULL;
      AST_Node** tail = &head;
      while (p->tok->kind != TOK_CASE && p->tok->kind != TOK_DEFAULT &&
             p->tok->kind != TOK_RBRACE && p->tok->kind != TOK_EOF) {
          AST_Node* stmt = ll_parse_stmt(p);
          if (!stmt) break;
          *tail = stmt;
          tail = &stmt->next;
      }
      n->body.case_stmt.stmt = head;
    }

    return n;
}

AST_Node* ll_parse_default(LR1_Parser* p)
{
    AST_Node* n = ll_stmt_begin(p, AST_DEFAULT);

    ll_expect(p, TOK_COLON);

    /* parse all statements until next case/default/closing brace */
    { AST_Node* head = NULL;
      AST_Node** tail = &head;
      while (p->tok->kind != TOK_CASE && p->tok->kind != TOK_DEFAULT &&
             p->tok->kind != TOK_RBRACE && p->tok->kind != TOK_EOF) {
          AST_Node* stmt = ll_parse_stmt(p);
          if (!stmt) break;
          *tail = stmt;
          tail = &stmt->next;
      }
      n->body.case_stmt.stmt = head;
    }

    return n;
}
