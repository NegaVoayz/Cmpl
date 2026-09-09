/* pp/inc/pp_comment.c -- comment handling shared by the expansion scan
 * (pp_line.c) and the #define body reader (pp_define.c).
 *
 * C removes comments before macro replacement runs (C11 5.1.1.2 phase 3):
 * a comment is never a macro context, and a trailing comment is not part of
 * a macro body.  cmpl used to expand macros inside the interior lines of a
 * block comment — injecting the macro body's own comment, whose terminator
 * closed the outer comment early — and kept `#define X 1 /* doc * /` in the
 * body of X.
 */

#include "../pp.h"

#include <string.h>

/* Advance *pp past a block comment body, entered just past its opening
 * delimiter.  Returns 1 when the terminator was found on this line and 0
 * when the comment continues on the next one. */
int
pp_skip_block_comment(const char** pp, const char* end)
{
    while (*pp + 1 < end && !((*pp)[0] == '*' && (*pp)[1] == '/'))
        (*pp)++;

    if (*pp + 1 < end) { *pp += 2; return 1; }
    if (*pp < end) (*pp)++;
    return 0;
}

static void
skip_line_comment(const char** pp, const char* end)
{
    while (*pp < end) (*pp)++;
}

/* Copy one comment verbatim — a comment is never a macro context.  Returns
 * 1 when a block comment is still open at the end of the line, so the next
 * line is copied as comment text too. */
int
pp_copy_comment(PPCtx* ctx, const char** pp, const char* end, Buffer* out)
{
    const char* c = *pp;
    int closed;

    if (!ctx->in_comment) {
        if ((*pp)[1] == '/') {              /* line comment: to the end */
            skip_line_comment(pp, end);
            buf_append(out, c, (int)(*pp - c));
            return 0;
        }
        *pp += 2;                           /* past the opening delimiter */
    }

    closed = pp_skip_block_comment(pp, end);
    buf_append(out, c, (int)(*pp - c));
    return !closed;
}

/* Remove comments from a macro body in place; literals are skipped.  *len
 * is updated and *open is set when a block comment is still open at the end
 * of the logical line, so the caller can carry the state to the next line. */
void
pp_strip_comments(char* buf, int* len, int* open)
{
    int n = *len;
    int i = 0;

    *open = 0;

    while (i < n) {
        if (buf[i] == '"' || buf[i] == '\'') {
            char q = buf[i++];

            while (i < n && buf[i] != q)
                i += (buf[i] == '\\' && i + 1 < n) ? 2 : 1;
            if (i < n) i++;
            continue;
        }

        if (buf[i] != '/') { i++; continue; }

        if (i + 1 < n && buf[i + 1] == '/') {   /* line comment: to the end */
            n = i;
            break;
        }

        if (i + 1 < n && buf[i + 1] == '*') {
            const char* p = buf + i + 2;

            if (!pp_skip_block_comment(&p, buf + n)) {
                n = i;                          /* open comment: body ends */
                *open = 1;
                break;
            }

            /* splice the comment out, leaving one space separator */
            memmove(buf + i + 1, p, (size_t)(buf + n - p));
            n -= (int)(p - (buf + i)) - 1;
            buf[i] = ' ';
            i++;
            continue;
        }
        i++;
    }

    *len = n;
    buf[n] = '\0';
}
