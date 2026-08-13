/* parse.h -- hybrid parser: LR(1) expressions + LL statements/declarations */

#ifndef PARSER_PARSE_H
#define PARSER_PARSE_H

#include "lr1.h"
#include "ll.h"

typedef struct Arena Arena;

AST_Node* parse_program(const char* code, Arena* a);

#endif /* PARSER_PARSE_H */
