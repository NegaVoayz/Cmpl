/* main_batch.c -- compile many translation units in one process (cmpl batch).
 *
 * Reads a list of source files and runs the existing single-TU pipeline per
 * file with a fresh preprocessor context, writing one flat-named .ll per
 * source exactly as build_self_linux.sh names them (src//\//_, strip .c).
 * Kept separate from main.c so the argument parser stays small. */

#include "main_driver.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char* listfile;
    const char* outdir;
    const char* incs[MAX_INCLUDES];
    int         n_incs;
    int         opt_level;
} BatchOpts;

static int
batch_usage(void)
{
    fprintf(stderr,
            "Usage: cmpl batch [-I dir]... [-O0|-O1|-O2] [-emit-llvm]"
            " [-o outdir] <filelist>\n"
            "       cmpl @<filelist> [-I dir]... [-o outdir]\n");
    return 1;
}

/* Flat output name: every '/' or '\' maps to '_', trailing ".c" stripped.
 * Matches build_self_linux.sh's "${src//\//_}" + "${base%.c}". */
static void
flat_name(const char* src, char* out, int out_sz)
{
    int j = 0;

    for (const char* p = src; *p && j + 1 < out_sz; p++) {
        char c = *p;

        if (c == '/' || c == '\\')
            c = '_';
        out[j++] = c;
    }

    if (j >= 2 && out[j - 2] == '.' && out[j - 1] == 'c')
        j -= 2;
    out[j] = '\0';
}

/* Compile one source with a fresh preprocessor context.  compile_source
 * frees the PPCtx on every exit path, so we never free it here. */
static int
compile_one(const CmdOpts* base, const char* src, const char* outdir,
            const char** incs, int n_incs)
{
    CmdOpts opts = *base;
    char    flat[512];
    char    path[1024];
    PPCtx   pp;

    flat_name(src, flat, sizeof(flat));
    snprintf(path, sizeof(path), "%s/%s.ll", outdir ? outdir : ".", flat);

    opts.filename = src;
    opts.out_file = path;

    pp_ctx_init(&pp);
    cmpl_add_default_include_paths(&pp);

    for (int i = 0; i < n_incs; i++)
        pp_add_include_path(&pp, incs[i]);

    return compile_source(&pp, &opts);
}

/* Parse batch args (argv[1] is "batch" or "@file").  Returns 0 on success
 * with b filled, nonzero after printing a message. */
static int
batch_parse_args(int argc, char** argv, BatchOpts* b)
{
    int i = 2;

    if (argc < 2)
        return batch_usage();

    if (argv[1][0] == '@')
        b->listfile = argv[1] + 1;
    else if (strcmp(argv[1], "batch") == 0)
        b->listfile = NULL;
    else
        return batch_usage();

    for (; i < argc; i++) {
        const char* a = argv[i];

        if (strcmp(a, "-emit-llvm") == 0) {
            /* default mode; accepted for build-script compatibility */
        } else if (strcmp(a, "-o") == 0 && i + 1 < argc) {
            b->outdir = argv[++i];
        } else if (strncmp(a, "-O", 2) == 0 && a[2] >= '0'
                   && a[2] <= '2' && a[3] == '\0') {
            b->opt_level = a[2] - '0';
        } else if (a[0] == '-' && a[1] == 'I' && a[2] != '\0') {
            if (b->n_incs < MAX_INCLUDES)
                b->incs[b->n_incs++] = a + 2;
        } else if (a[0] == '-' && a[1] == 'I' && a[2] == '\0'
                   && i + 1 < argc) {
            if (b->n_incs < MAX_INCLUDES)
                b->incs[b->n_incs++] = argv[++i];
        } else if (a[0] == '-' && a[1] != '\0') {
            fprintf(stderr, "batch: unsupported flag '%s'\n", a);
            return 1;
        } else if (!b->listfile) {
            b->listfile = (a[0] == '@') ? a + 1 : a;
        } else {
            fprintf(stderr, "batch: unexpected argument '%s'\n", a);
            return 1;
        }
    }

    return b->listfile ? 0 : batch_usage();
}

/* Compile every source path in text (one per line; '#' or blank skipped). */
static void
batch_run_list(const char* text, int len, const CmdOpts* opts,
               const char* outdir, const char** incs, int n_incs,
               int* ok, int* fail)
{
    const char* p = text;
    const char* end = text + len;

    while (p < end) {
        const char* e = p;
        int         line_len;

        while (e < end && *e != '\n')
            e++;

        line_len = (int)(e - p);

        if (line_len > 0 && *p != '#') {
            if (p[line_len - 1] == '\r')
                line_len--;

            char line[512];

            if (line_len < (int)sizeof(line)) {
                snprintf(line, sizeof(line), "%.*s", line_len, p);

                if (compile_one(opts, line, outdir, incs, n_incs) != 0) {
                    fprintf(stderr, "cmpl:%s\n", line);
                    (*fail)++;
                } else {
                    (*ok)++;
                }
            }
        }
        p = (e < end) ? e + 1 : end;
    }
}

int
run_batch(int argc, char** argv)
{
    BatchOpts b;
    int       ok = 0;
    int       fail = 0;

    memset(&b, 0, sizeof(b));

    if (batch_parse_args(argc, argv, &b) != 0)
        return 1;

    int   len;
    char* text = read_file(b.listfile, &len);

    if (!text) {
        fprintf(stderr, "batch: cannot open file list '%s'\n", b.listfile);
        return 1;
    }

    CmdOpts opts;

    memset(&opts, 0, sizeof(opts));
    opts.quiet = 1;
    opts.codegen_mode = CG_OUT_LLVM_IR;
    opts.opt_level = b.opt_level;

    batch_run_list(text, len, &opts, b.outdir, b.incs, b.n_incs,
                   &ok, &fail);

    free(text);
    fprintf(stderr, "batch: OK %d FAIL %d\n", ok, fail);
    return fail ? 1 : 0;
}
