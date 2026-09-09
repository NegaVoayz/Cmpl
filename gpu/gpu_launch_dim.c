/* gpu_launch_dim.c -- compile-time evaluation of a kernel launch's block
 * dimensions.
 *
 * `<<<grid, block>>>` fixes the number of threads per block.  SPIR-V has to
 * carry that number (LocalSize + the WorkgroupSize builtin, i.e. blockDim),
 * so the emitter needs the value as a constant.  cmpl has no dim3 type: the
 * block entry is a single integer expression, normally already folded to a
 * literal by the AST optimizer.  Here it is evaluated with a small constant
 * evaluator covering the integer operators the front-end folds.
 */

#include "gpu.h"

#include <limits.h>

/* integer constant expression evaluator: 1 = *out holds the value, 0 = the
 * expression is not a compile-time constant. */
static int eval_int(AST_Node* n, long long* out);

static int
eval_binary(TokenKind op, long long a, long long b, long long* out)
{
    switch (op) {
    case TOK_PLUS:    *out = a + b; return 1;
    case TOK_MINUS:   *out = a - b; return 1;
    case TOK_STAR:    *out = a * b; return 1;
    case TOK_SLASH:   if (!b) return 0; *out = a / b; return 1;
    case TOK_PERCENT: if (!b) return 0; *out = a % b; return 1;
    case TOK_LTLT:    *out = a << (b & 63); return 1;
    case TOK_GTGT:    *out = a >> (b & 63); return 1;
    case TOK_AMP:     *out = a & b; return 1;
    case TOK_PIPE:    *out = a | b; return 1;
    case TOK_CARET:   *out = a ^ b; return 1;
    default:          return 0;
    }
}

static int
eval_int(AST_Node* n, long long* out)
{
    long long a, b;

    if (!n) return 0;

    switch (n->type) {
    case AST_INT_LIT:
    case AST_LONG_LIT:
        *out = n->body.literal.int_val;
        return 1;
    case AST_CHAR_LIT:
        *out = (long long)n->body.literal.char_val;
        return 1;
    case AST_UNARY:
        if (!eval_int(n->body.unary.operand, &a)) return 0;
        switch (n->body.unary.op) {
        case TOK_PLUS:  *out = a; return 1;
        case TOK_MINUS: *out = -a; return 1;
        case TOK_TILDE: *out = ~a; return 1;
        case TOK_BANG:  *out = !a; return 1;
        default:        return 0;
        }
    case AST_BINARY:
        if (!eval_int(n->body.binary.left, &a)) return 0;
        if (!eval_int(n->body.binary.right, &b)) return 0;
        return eval_binary(n->body.binary.op, a, b, out);
    default:
        return 0;
    }
}

/* Block dimensions of one launch site.  Returns 1 when the block size is a
 * compile-time constant; *bx/*by/*bz then hold it (y/z are always 1 because
 * cmpl has no dim3).  Returns 0 when it is not constant. */
int
gpu_launch_block_dim(const KernelLaunch* kl, int* bx, int* by, int* bz)
{
    long long v;

    *bx = 1; *by = 1; *bz = 1;
    if (!kl || !kl->block_dim) return 0;
    if (!eval_int(kl->block_dim, &v)) return 0;
    if (v < 1 || v > INT_MAX) return 0;
    *bx = (int)v;
    return 1;
}
