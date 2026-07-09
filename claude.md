# Project C Compiler

## Basic Idea

### Token Design

```c
typedef struct {
    TokenKind kind;
    SourceLoc loc;
    Token*    next;
    union {
        //...
    } body;
} Token;
```

Here the next pointer is used to build a chain of tokens.

### AST Design

```c
typedef struct {
   	AST_Type type;
    AST_Node* next;
    union {
        //...
    } body;
} AST_Node;
```

Here the next pointer is used to connect tree chain. The last expression's next points to the parent node for convenience.

### Reduce Design

The reduction will be a hybrid method.

For one single expression, we call the LR(1) analyzer to build a node.

For bigger structures (like `if`, `brace`), the LL analyzer is good enough.

#### Function Design

What we need for each run:

* up / forward
* the expression itself
* right / wrong

How it works:

* single normal expression
  1. go forward with LR(1) rules.
  2. return **before** the ending token (extra ')', extra '}', extra ']', or ';')
* special expression(if)
  1. for each branch down, call normal expression (for modern C, test the embraced define expression first)
  2. for '{}' block, call normal expression until the end is not ';'(test gives not forward but up)
* multiple expressions
  1. test the first token to see whether special or normal.
  2. call the expression processor
  3. test the top token to see if is any end brace. if so, it's 'up', or it's 'forward'

## Rules

* Each file less than 200 lines.
* Each function less than 80 lines.
* Empty line is REQUIRED between any functional blocks.
* The braces style is K&R style.