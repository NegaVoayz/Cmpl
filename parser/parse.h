/* parse.h -- hybrid parser: LR(1) expressions + LL statements/declarations */

#ifndef PARSER_PARSE_H
#define PARSER_PARSE_H

#include "lr1.h"
#include "ll.h"

/* parse_program -- parse an entire translation unit
 *
 * Tokenizes the source, then drives the hybrid parser:
 *   - ll_parse_program() calls lr1_parse_expr() for expressions
 *   - ll_parse_program() dispatches statements/declarations
 */
AST_Node* parse_program(const char* code);

#endif /* PARSER_PARSE_H */
