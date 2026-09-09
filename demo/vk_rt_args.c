/* demo/vk_rt_args.c -- decoding the variadic kernel-launch record.
 *
 * The compiler emits the arguments of `matmul<<<2, 8>>>(A, B, C, n)` as the
 * variadic tail of cmpl_vk_launch(), which carries no type information.
 * The host program declares the signature once with cmpl_demo_sig()
 * ('P' pointer, 'I' int, 'L' long long, 'F' float, 'D' double) and this
 * module lays the values out in the push-constant block exactly as
 * doc/gpu-bridge.md specifies: a pointer argument is an 8-byte buffer
 * device address, a scalar is the scalar itself, at natural alignment.
 */

#include "vk_rt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char name[64];
    char sig[64];
} RtKernel;

static RtKernel kern[RT_MAX_KERNEL];
static int      nkern;

void
cmpl_demo_sig(const char* kernel, const char* sig)
{
    if (nkern >= RT_MAX_KERNEL) return;

    snprintf(kern[nkern].name, sizeof(kern[nkern].name), "%s", kernel);
    snprintf(kern[nkern].sig, sizeof(kern[nkern].sig), "%s", sig);
    nkern++;
    printf("  [vk] kernel '%s' argument signature '%s'\n", kernel, sig);
}

void
cmpl_demo_buf(const void* host, unsigned long bytes)
{
    rt_host_buf(host, bytes);
}

const char*
rt_sig_of(const char* kernel)
{
    for (int i = 0; i < nkern; i++)
        if (strcmp(kern[i].name, kernel) == 0)
            return kern[i].sig;
    return NULL;
}

static unsigned
align_up(unsigned off, unsigned a)
{
    return (off + a - 1) & ~(a - 1);
}

/* one pointer argument: upload the host array, remember it for copy-back */
static void
push_ptr(va_list* ap, unsigned char* pc, unsigned* off, RtSlot* slots,
         int* nslot)
{
    const void* p = va_arg(*ap, const void*);
    const RtHostBuf* hb = rt_host_find(p);
    RtSlot* s;

    if (!hb) {
        fprintf(stderr, "demo runtime: pointer argument %p is not a "
                        "registered buffer (call cmpl_demo_buf)\n", p);
        exit(1);
    }
    if (*nslot >= RT_MAX_ARG) rt_die("too many pointer arguments");

    *off = align_up(*off, 8);
    s = &slots[(*nslot)++];
    s->host = p;
    if (!rt_dev_buf(s, hb->bytes))
        rt_die("rt_dev_buf");
    memcpy(pc + *off, &s->addr, 8);
    *off += 8;
}

/* the variadic tail carries no types: decode it with the declared signature */
void
rt_write_args(const char* sig, va_list* ap, unsigned char* pc, unsigned* off,
              RtSlot* slots, int* nslot)
{
    for (const char* c = sig; *c; c++) {
        switch (*c) {
        case 'P':
            push_ptr(ap, pc, off, slots, nslot);
            break;
        case 'I': {
            int v = va_arg(*ap, int);

            *off = align_up(*off, 4);
            memcpy(pc + *off, &v, 4); *off += 4;
            break; }
        case 'L': {
            long long v = va_arg(*ap, long long);

            *off = align_up(*off, 8);
            memcpy(pc + *off, &v, 8); *off += 8;
            break; }
        case 'F': {
            float f = (float)va_arg(*ap, double);   /* vararg promotion */

            *off = align_up(*off, 4);
            memcpy(pc + *off, &f, 4); *off += 4;
            break; }
        case 'D': {
            double d = va_arg(*ap, double);

            *off = align_up(*off, 8);
            memcpy(pc + *off, &d, 8); *off += 8;
            break; }
        default:
            fprintf(stderr, "demo runtime: bad signature char '%c'\n", *c);
            exit(1);
        }
    }
}
