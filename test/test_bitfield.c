/* test_bitfield.c — C bit-fields end-to-end, gcc x86-64 SysV layout.
 *
 * Covers: same-unit packing (unsigned a:3; char c:3), mixed base types
 * in both orders (k / b2 / mix), overflow into a fresh unit (o), zero-
 * width :0 alignment (z), unnamed fields (gk), _Bool 1-bit fields (bb),
 * 16/64-bit units (s16 / l64), signed sign-extension on load (i),
 * read-modify-write (+=, ++, --) with wrap, const initializers
 * (positional + designated) for locals and globals, and sizeof/align.
 */
#include <stdio.h>
#include <string.h>

struct b  { unsigned a:3; char c:3; };
struct k  { unsigned a:3; unsigned b:3; char c:3; char d:3; };
struct b2 { char c:3; unsigned a:3; };
struct i  { signed a:3; signed b:5; signed c:7; signed d:17; };
struct o  { unsigned a:31; unsigned b:5; };
struct z  { unsigned a:3; int :0; unsigned b:5; };
struct bb { _Bool x:1; _Bool y:1; };
struct s16 { unsigned short a:3; unsigned short b:5; };
struct l64 { unsigned long a:3; unsigned long b:29; };
struct mix { char c:3; unsigned a:3; char d:3; unsigned e:3; };
struct gk { unsigned pad1:3; unsigned :4; unsigned x:5; };
struct nest2 { char y; struct k x; };

static struct k gk = { 5, 3, 2, 3 };
static struct mix gm = { .d = 6, .a = 3, .e = 4, .c = 5 };
static struct z gz = { 5, 7 };
static struct o go = { 0x7fffffff, 5 };

static void dump(const char* tag, const void* p, size_t n)
{
    const unsigned char* b = (const unsigned char*)p;
    printf("%s:", tag);
    for (size_t i = 0; i < n; i++) printf(" %02x", b[i]);
    printf("\n");
}

int main(void)
{
    /* layout: sizeof + alignment must match gcc (x86-64 SysV) */
    if (sizeof(struct b) != 4 || _Alignof(struct b) != 4) return 1;
    if (sizeof(struct k) != 4 || _Alignof(struct k) != 4) return 2;
    if (sizeof(struct b2) != 4 || _Alignof(struct b2) != 4) return 4;
    if (sizeof(struct i) != 4 || _Alignof(struct i) != 4) return 8;
    if (sizeof(struct o) != 8 || _Alignof(struct o) != 4) return 16;
    if (sizeof(struct z) != 8 || _Alignof(struct z) != 4) return 32;
    if (sizeof(struct bb) != 1 || _Alignof(struct bb) != 1) return 64;
    if (sizeof(struct s16) != 2 || _Alignof(struct s16) != 2) return 128;
    if (sizeof(struct l64) != 8 || _Alignof(struct l64) != 8) return 256;
    if (sizeof(struct mix) != 4 || _Alignof(struct mix) != 4) return 512;
    if (sizeof(struct gk) != 4 || _Alignof(struct gk) != 4) return 1024;

    struct b b; struct k k; struct b2 b2; struct i i; struct o o; struct z z;
    struct bb bb; struct s16 s16; struct l64 l64; struct mix mix; struct gk gk1;
    struct nest2 n1 = { 0x55, { 5, 3, 2, 3 } };

    memset(&b, 0, sizeof b); memset(&k, 0, sizeof k);
    memset(&b2, 0, sizeof b2); memset(&i, 0, sizeof i);
    memset(&o, 0, sizeof o); memset(&z, 0, sizeof z);
    memset(&bb, 0, sizeof bb); memset(&s16, 0, sizeof s16);
    memset(&l64, 0, sizeof l64); memset(&mix, 0, sizeof mix);
    memset(&gk1, 0, sizeof gk1);

    /* same-unit packing: a and c share byte 0 */
    b.a = 5; b.c = 2;
    if (b.a != 5 || b.c != 2) return 2048;
    if (((unsigned char*)&b)[0] != 0x15) return 4096;

    /* mixed base types, unsigned-first order: a,b in byte0; c,d in byte1 */
    k.a = 5; k.b = 3; k.c = 2; k.d = 3;
    if (((unsigned char*)&k)[0] != 0x1d || ((unsigned char*)&k)[1] != 0x1a)
        return 8192;

    /* mixed base types, char-first order: both pack into byte0 */
    b2.c = 5; b2.a = 3;
    if (((unsigned char*)&b2)[0] != 0x1d) return 16384;

    /* signed fields sign-extend on load (3+5+7+17 bits, all -1) */
    i.a = -1; i.b = -1; i.c = -1; i.d = -1;
    if (i.a != -1 || i.b != -1 || i.c != -1 || i.d != -1) return 32768;
    if (i.a == 7) return 65536;   /* would be 7 if not sign-extended */

    /* overflow: 31+5 bits needs a fresh unit -> size 8 */
    o.a = 0x7fffffff; o.b = 5;
    if (o.a != 0x7fffffff || o.b != 5) return 131072;
    if (((unsigned char*)&o)[4] != 0x05) return 262144;

    /* zero-width :0 aligns the next field to its type boundary */
    z.a = 5; z.b = 5;
    if (((unsigned char*)&z)[0] != 0x05 || ((unsigned char*)&z)[4] != 0x05)
        return 524288;

    /* unnamed :4 gap: positional initializer skips it */
    gk1 = (struct gk){ 2, 17 };
    if (gk1.pad1 != 2 || gk1.x != 17) return 1048576;

    /* _Bool 1-bit fields: values stay 0/1 */
    bb.x = 1; bb.y = 1;
    if (bb.x != 1 || bb.y != 1) return 2097152;

    /* 16-bit and 64-bit units */
    s16.a = 7; s16.b = 5;
    if (s16.a != 7 || s16.b != 5) return 4194304;
    l64.a = 3; l64.b = 0x1fffffff;
    if (l64.a != 3 || l64.b != 0x1fffffff) return 8388608;

    /* read-modify-write with wrap: b += 2 overflows 3 bits */
    k.b += 2;                       /* 3 + 2 = 5 */
    k.a++;                          /* 5 + 1 = 6 */
    ++k.c;                          /* 2 + 1 = 3 */
    k.d--;                          /* 3 - 1 = 2 */
    if (k.a != 6 || k.b != 5 || k.c != 3 || k.d != 2) return 16777216;

    /* signed RMW: -1 - 1 wraps in 3 bits to -1 again */
    mix.c = -1; mix.d = -2;
    mix.c -= 1;
    mix.d += 3;
    if (mix.c != -1 || mix.d != 1) return 33554432;

    /* designated initializer (locals + globals) */
    struct k k2 = { .b = 3, .d = 3, .a = 5, .c = 2 };
    if (k2.a != 5 || k2.b != 3 || k2.c != 2 || k2.d != 3) return 67108864;
    if (gm.c != 5 || gm.a != 3 || gm.d != 6 || gm.e != 4) return 134217728;
    if (gz.a != 5 || gz.b != 7 || go.a != 0x7fffffff || go.b != 5)
        return 268435456;

    /* nested struct with a bit-field member, padding zeroed */
    n1.y = 0x55;
    if (((unsigned char*)&n1)[1] != 0x00) return 536870912;
    if (n1.x.a != 5 || n1.x.d != 3) return 1073741824;

    printf("all bitfield checks passed\n");
    dump("b", &b, sizeof b);
    dump("k", &k, sizeof k);
    dump("gk1", &gk1, sizeof gk1);
    printf("gk=%u,%u,%d,%d gm=%d,%u,%d,%u gz=%u,%u go=%u,%u\n",
        gk.a, gk.b, gk.c, gk.d, gm.c, gm.a, gm.d, gm.e, gz.a, gz.b,
        go.a, go.b);
    return 0;
}
