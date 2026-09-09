/* demo/vk_rt_time.c -- monotonic clock and the counters the demo reports.
 *
 * A dispatch is timed around the fence wait, so the device time excludes
 * host upload/readback and pipeline creation.  The byte counters record how
 * much data actually crossed the bus, which is what tells a compute-bound
 * run from a transfer-bound one.
 */

#include "vk_rt.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

double
rt_now_ms(void)
{
#ifdef _WIN32
    static double freq;             /* performance counter ticks per ms */
    LARGE_INTEGER c;

    if (freq == 0.0) {
        LARGE_INTEGER f;

        QueryPerformanceFrequency(&f);
        freq = (double)f.QuadPart / 1000.0;
    }
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / freq;
#else
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
#endif
}

void
rt_stat_dispatch(double ms)
{
    rt.ndispatch++;
    rt.dev_ms += ms;
}

void
rt_stat_bytes(unsigned long up, unsigned long down)
{
    rt.up_bytes += up;
    rt.down_bytes += down;
}

/* host-facing: how much device work has happened so far */
void
cmpl_demo_stats(int* dispatches, double* device_ms, unsigned long* up,
                unsigned long* down)
{
    if (dispatches) *dispatches = rt.ndispatch;
    if (device_ms)  *device_ms = rt.dev_ms;
    if (up)         *up = rt.up_bytes;
    if (down)       *down = rt.down_bytes;
}
