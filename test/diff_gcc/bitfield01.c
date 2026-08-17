/* bitfield01.c -- gcc-parity: C bit-field layout and access, x86-64 SysV.
 *
 * Prints sizeof/_Alignof, byte dumps of fully-masked fields, and field
 * values (incl. signed sign-extension) for the k (unsigned-first mixed
 * types) and b2 (char-first mixed types) cases.  cmpl must produce
 * byte-identical stdout + exit code.
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
    printf("b=%zu %zu\n", sizeof(struct b), _Alignof(struct b));
    printf("k=%zu %zu\n", sizeof(struct k), _Alignof(struct k));
    printf("b2=%zu %zu\n", sizeof(struct b2), _Alignof(struct b2));
    printf("i=%zu %zu\n", sizeof(struct i), _Alignof(struct i));
    printf("o=%zu %zu\n", sizeof(struct o), _Alignof(struct o));
    printf("z=%zu %zu\n", sizeof(struct z), _Alignof(struct z));
    printf("bb=%zu %zu\n", sizeof(struct bb), _Alignof(struct bb));
    printf("s16=%zu %zu\n", sizeof(struct s16), _Alignof(struct s16));
    printf("l64=%zu %zu\n", sizeof(struct l64), _Alignof(struct l64));
    printf("mix=%zu %zu\n", sizeof(struct mix), _Alignof(struct mix));
    printf("gk=%zu %zu\n", sizeof(struct gk), _Alignof(struct gk));
    printf("nest2=%zu %zu\n", sizeof(struct nest2), _Alignof(struct nest2));

    struct b b; struct k k; struct b2 b2; struct i i; struct o o; struct z z;
    struct bb bb; struct s16 s16; struct l64 l64; struct mix mix;
    struct gk gk1; struct nest2 n1 = { 0x55, { 5, 3, 2, 3 } };

    memset(&b, 0, sizeof b); memset(&k, 0, sizeof k);
    memset(&b2, 0, sizeof b2); memset(&i, 0, sizeof i);
    memset(&o, 0, sizeof o); memset(&z, 0, sizeof z);
    memset(&bb, 0, sizeof bb); memset(&s16, 0, sizeof s16);
    memset(&l64, 0, sizeof l64); memset(&mix, 0, sizeof mix);
    memset(&gk1, 0, sizeof gk1);

    b.a = 5; b.c = 2;
    k.a = 5; k.b = 3; k.c = 2; k.d = 3;
    b2.c = 5; b2.a = 3;
    i.a = -1; i.b = -1; i.c = -1; i.d = -1;
    o.a = 0x7fffffff; o.b = 5;
    z.a = 5; z.b = 5;
    bb.x = 1; bb.y = 1;
    s16.a = 7; s16.b = 5;
    l64.a = 3; l64.b = 0x1fffffff;
    mix.c = 5; mix.a = 3; mix.d = 6; mix.e = 4;
    gk1 = (struct gk){ 2, 17 };

    k.b += 2;   /* 3 -> 5 */
    k.a++;      /* 5 -> 6 */
    ++k.c;      /* 2 -> 3 */
    k.d--;      /* 3 -> 2 */

    dump("b", &b, sizeof b);
    dump("k", &k, sizeof k);
    dump("b2", &b2, sizeof b2);
    dump("i", &i, sizeof i);
    dump("o", &o, sizeof o);
    dump("z", &z, sizeof z);
    dump("bb", &bb, sizeof bb);
    dump("s16", &s16, sizeof s16);
    dump("l64", &l64, sizeof l64);
    dump("mix", &mix, sizeof mix);
    dump("gk1", &gk1, sizeof gk1);
    dump("n1", &n1, sizeof n1);

    printf("vals b.a=%u c=%d k.a=%u k.b=%u k.c=%d k.d=%d b2.c=%d b2.a=%u\n",
        b.a, b.c, k.a, k.b, k.c, k.d, b2.c, b2.a);
    printf("vals i.a=%d i.b=%d i.c=%d i.d=%d o.a=%u o.b=%u z.a=%u z.b=%u\n",
        i.a, i.b, i.c, i.d, o.a, o.b, z.a, z.b);
    printf("vals bb.x=%d bb.y=%d s16.a=%u s16.b=%u l64.a=%u l64.b=%u\n",
        bb.x, bb.y, s16.a, s16.b, l64.a, l64.b);
    printf("vals mix.c=%d mix.a=%u mix.d=%d mix.e=%u gk1.pad1=%u gk1.x=%u\n",
        mix.c, mix.a, mix.d, mix.e, gk1.pad1, gk1.x);
    printf("vals n1.y=%d n1.x.a=%u n1.x.b=%u n1.x.c=%d n1.x.d=%d\n",
        n1.y, n1.x.a, n1.x.b, n1.x.c, n1.x.d);
    printf("vals gk=%u,%u,%d,%d go=%u,%u\n", gk.a, gk.b, gk.c, gk.d,
        go.a, go.b);
    return 0;
}
