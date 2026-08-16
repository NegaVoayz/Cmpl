/* pp_expand_ops.c -- `#` stringize and `__VA_ARGS__` join helpers.
 *
 * Split out of pp_expand.c.  stringize_arg quotes an argument's text,
 * escaping " and \; append_va_args joins the trailing variadic arguments
 * with ", " the way gcc -E renders __VA_ARGS__.
 */

#include "../pp.h"

/* Emit `"` + arg text (escaping " and \) + `"` to out. */
void
stringize_arg(const char* s, int slen, Buffer* out)
{
    buf_append(out, "\"", 1);
    for (int i = 0; i < slen; i++) {
        char c = s[i];
        if (c == '"' || c == '\\') buf_append(out, "\\", 1);
        buf_append(out, &c, 1);
    }
    buf_append(out, "\"", 1);
}

/* Append variadic arguments [start, argc) joined by ", " to out. */
void
append_va_args(const char** arg_starts, const int* arg_lens,
               int start, int argc, Buffer* out)
{
    for (int i = start; i < argc; i++) {
        if (i > start) buf_append(out, ", ", 2);
        buf_append(out, arg_starts[i], arg_lens[i]);
    }
}
