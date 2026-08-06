# Tokenizer & Tokens

## Token Kinds

```c
typedef enum {
    // Keywords
    TOK_IF, TOK_ELSE, TOK_WHILE, TOK_FOR, TOK_RETURN, TOK_INT, TOK_CHAR,
    TOK_VOID, TOK_STRUCT, TOK_TYPEDEF, TOK_SIZEOF, TOK_BREAK, TOK_CONTINUE,
    TOK_SWITCH, TOK_CASE, TOK_DEFAULT, TOK_DO, TOK_GOTO, TOK_ENUM, TOK_UNION,
    TOK_CONST, TOK_VOLATILE, TOK_STATIC, TOK_EXTERN, TOK_REGISTER,
    TOK_SIGNED, TOK_UNSIGNED, TOK_SHORT, TOK_LONG, TOK_DOUBLE, TOK_FLOAT,

    // Literals
    TOK_INT_LIT, TOK_LONG_LIT, TOK_CHAR_LIT, TOK_STRING_LIT,
    TOK_FLOAT_LIT, TOK_DOUBLE_LIT,

    // Identifier
    TOK_IDENT,

    // Operators (+, -, *, /, %, =, ==, !=, <, >, <=, >=, &&, ||, !, &, |,
    //           ^, ~, <<, >>, +=, -=, *=, /=, ++, --, <<<, >>>, ->, .)

    // Separators
    TOK_LPAREN, TOK_RPAREN, TOK_LBRACKET, TOK_RBRACKET, TOK_LBRACE, TOK_RBRACE,
    TOK_SEMI, TOK_COMMA, TOK_COLON, TOK_QUESTION,

    // Special
    TOK_EOF, TOK_ERROR
} TokenKind;
```

## Token Structure

Tokens form a singly-linked list:

```c
typedef struct Token Token;
struct Token {
    TokenKind kind;
    SourceLoc loc;     // line, col
    Token*    next;    // next token in chain
    union {
        long   int_val;    // TOK_INT_LIT
        char   char_val;   // TOK_CHAR_LIT
        double float_val;  // TOK_FLOAT_LIT
        String str_val;    // TOK_STRING_LIT
        String ident;      // TOK_IDENT
    } body;
};
```

Source locations use `SourceLoc { int line; int col; }`. String data uses
`String { const char* data; int length; }` — non-owning pointers into the source buffer.

## Lexer Internals

```c
typedef struct {
    const char* src;    // full source text
    const char* cur;    // current position
    int line, col;      // current location
    Token* head;        // first token in output chain
    Token* tail;        // last token (for append)
} Lexer;
```

The lexer cursor abstraction (`lexer.h`) provides:
- `peek()` / `peek_next()` — lookahead without advancing
- `advance()` — consume one character, update line/col
- `skip_ws_and_comments()` — skip whitespace and `//` / `/* */` comments

## File Split

| File | Responsibility |
|---|---|
| `parse.c` | Driver: `parse(const char* code)` creates a `Lexer`, runs the scan loop, returns the token chain |
| `lexer.c` | Core lexing: `read_ident_or_keyword()`, `read_operator()`, `read_char_or_string()`, `token_new()` |
| `number.c` | Numeric literals: `read_number()` handles int, long, float, double, hex, octal |
| `ast.c` | Node/type allocators (see [AST doc](ast.md)) |

## Example

```c
Token* tokens = parse("int x = 42;");
// Produces: TOK_INT → TOK_IDENT("x") → TOK_EQ → TOK_INT_LIT(42) → TOK_SEMI → TOK_EOF
```
