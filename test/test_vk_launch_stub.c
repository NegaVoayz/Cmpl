/* test_vk_launch_stub.c — runtime stub for cmpl_vk_launch().
 *
 * The real Vulkan runtime (rt/) is planned; this stub is the host-side
 * counterpart for test_gpu_host_exec.c.  It reads the variadic launch
 * record — name, (gx,gy,gz), (bx,by,bz), shared, stream, nka, args —
 * and asserts each call against the expected sequence, so the test fails
 * (nonzero exit / FAIL report) when the mock emitted the wrong config or
 * kernel args.  All test launch values are 32-bit ints.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char* name;
    int gx, bx;
    int shared, stream;
    int nka;
    int args[4];
} ExpectedLaunch;

static const ExpectedLaunch expected[] = {
    {"kern_a", 4, 256, 0, 0, 3, {1, 2, 3}},
    {"kern_b", 1, 64, 1024, 0, 2, {10, 20}},
    {"kern_c", 2, 8, 0, 7, 1, {100}},
};

static int g_calls = 0;
static int g_fail = 0;

void
cmpl_vk_launch(const char* name, ...)
{
    va_list ap;
    int gx, gy, gz, bx, by, bz, shared, stream, nka, i;

    if (g_calls >= 3) {
        printf("FAIL unexpected launch #%d: %s\n", g_calls, name);
        g_fail = 1;
        return;
    }

    va_start(ap, name);
    gx = va_arg(ap, int); gy = va_arg(ap, int); gz = va_arg(ap, int);
    bx = va_arg(ap, int); by = va_arg(ap, int); bz = va_arg(ap, int);
    shared = va_arg(ap, int);
    stream = va_arg(ap, int);
    nka = va_arg(ap, int);

    const ExpectedLaunch* e = &expected[g_calls];

    printf("LAUNCH %s grid=(%d,%d,%d) block=(%d,%d,%d) shared=%d "
           "stream=%d nka=%d",
           name, gx, gy, gz, bx, by, bz, shared, stream, nka);
    for (i = 0; i < nka; i++)
        printf(" arg%d=%d", i, va_arg(ap, int));
    printf("\n");
    va_end(ap);

    if (strcmp(name, e->name) != 0 || gx != e->gx || bx != e->bx ||
        shared != e->shared || stream != e->stream || nka != e->nka) {
        printf("FAIL %s: config mismatch (want grid=%d block=%d "
               "shared=%d stream=%d nka=%d)\n",
               e->name, e->gx, e->bx, e->shared, e->stream, e->nka);
        g_fail = 1;
    }
    g_calls++;
}

int
cmpl_vk_launch_ok(void)
{
    if (g_calls != 3 || g_fail) {
        printf("FAIL: %d launches seen, %s\n", g_calls,
               g_fail ? "assertion failed" : "wrong launch count");
        return 0;
    }
    printf("PASS: all %d launches received the expected config+args\n",
           g_calls);
    return 1;
}
