/* test_pp_multiline_comment_macro.c — a block comment that spans lines is
 * comment text, not a macro-expansion context.
 *
 * The expansion scan ran once per line, so the interior lines of a block
 * comment were treated as ordinary code: a macro name mentioned inside the
 * comment was substituted, which injected the macro body's own trailing
 * comment — and its terminator closed the surrounding comment early, so the
 * rest of the comment became garbage tokens (cmpl: "lr1: syntax error").
 * A '#' at the start of such a line was taken for a directive as well.
 *
 * gcc reference output: "ok 32 7".
 */

#include <stdio.h>

#define TPB 32 /* threads per block */
#define VAL 7

/* The lines below mention TPB and VAL inside a comment:
   the interior must stay text: TPB = 32 and VAL = 7.
   And this line looks like a directive but is comment text:
#include "this-header-must-not-be-included.h"
   end of the comment. */

int main(void)
{
    printf("ok %d %d\n", TPB, VAL);
    return 0;
}
