/* test_vk_launch_wide_stub.c — runtime stub for cmpl_vk_launch().
 *
 * Host-side counterpart for test_gpu_many_kernel_args.c: the launch passes
 * the 4-slot config header, the arg count and then 15 kernel args (14 ints
 * plus the output pointer).  The stub checks that every one of them arrives,
 * which is what the fixed-size 16-slot argument buffer used to break.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int g_calls = 0;
static int g_fail = 0;

void
cmpl_vk_launch(const char* name, ...)
{
    va_list ap;
    int gx, gy, gz, bx, by, bz, shared, stream, nka, i;
    static const int want[14] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};

    va_start(ap, name);
    gx = va_arg(ap, int); gy = va_arg(ap, int); gz = va_arg(ap, int);
    bx = va_arg(ap, int); by = va_arg(ap, int); bz = va_arg(ap, int);
    shared = va_arg(ap, int);
    stream = va_arg(ap, int);
    nka = va_arg(ap, int);

    printf("LAUNCH %s grid=(%d,%d,%d) block=(%d,%d,%d) shared=%d stream=%d "
           "nka=%d\n", name, gx, gy, gz, bx, by, bz, shared, stream, nka);

    if (strcmp(name, "wide") != 0 || gx != 1 || bx != 1 || nka != 15) {
        printf("FAIL: expected wide<<<1,1>>> with 15 args\n");
        g_fail = 1;
    }
    for (i = 0; i < 14; i++) {
        int v = va_arg(ap, int);

        printf("  arg%d=%d\n", i, v);
        if (v != want[i]) {
            printf("FAIL: arg%d is %d, want %d\n", i, v, want[i]);
            g_fail = 1;
        }
    }
    (void)va_arg(ap, void*);   /* the out pointer */
    va_end(ap);
    g_calls++;
}

int
cmpl_vk_launch_ok(void)
{
    if (g_calls != 1 || g_fail) {
        printf("FAIL: %d launches, %s\n", g_calls,
               g_fail ? "assertion failed" : "wrong launch count");
        return 0;
    }
    printf("PASS: all 15 kernel args arrived\n");
    return 1;
}
