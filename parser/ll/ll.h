/* ll.h -- LL recursive-descent parser for statements and declarations */

#ifndef LL_H
#define LL_H

#include "lr1.h"

/* parse a full translation unit (program) */
AST_Node* ll_parse_program(LR1_Parser* p);

/* parse a single statement (if/while/for/return/block/etc.) */
AST_Node* ll_parse_stmt(LR1_Parser* p);

/* parse a declaration (var/func/struct/union/enum/typedef) */
AST_Node* ll_parse_decl(LR1_Parser* p);

/* parse a type expression, returns Type tree */
Type* ll_parse_type(LR1_Parser* p);

#endif /* LL_H */
