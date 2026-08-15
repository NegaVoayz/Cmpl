/* a NAMED struct referenced only inside an ANONYMOUS struct/union member.
 *
 * resolve_struct_refs_type used to recurse into members only while resolving
 * a *named* struct reference, so a named struct nested inside an anonymous
 * struct (struct { struct IN in; } or a union whose largest member is such an
 * anonymous struct) kept params==NULL.  That produced an incomplete
 * %struct.IN (no members), so its type definition was never emitted and the
 * module referenced an undefined %struct.IN.  Punning views print via %a so
 * the gcc differential catches any value-space mismatch.
 */
#include <stdio.h>

struct IN { double z; };

/* named struct inside an anonymous struct (plain global) */
struct { struct IN in; double y; } g = { .in.z = 2.5, .y = 3.5 };

/* named struct inside an anonymous struct that is a union's largest member */
union U { int a; struct { struct IN in; double y; } s; };
union U u = { .a = 7 };

/* deeper nesting: anonymous -> anonymous -> named */
struct { struct { struct IN in; double y; } mid; } deep = { .mid.in.z = 4.5 };

int main(void)
{
    int rc = 0;

    if (g.in.z != 2.5) { printf("g.in.z=%a\n", g.in.z); rc |= 1; }
    if (g.y   != 3.5) { printf("g.y=%a\n",   g.y);   rc |= 2; }

    if (u.a != 7) { printf("u.a=%d\n", u.a); rc |= 4; }
    printf("u.s.in.z=%a\n", u.s.in.z);

    if (deep.mid.in.z != 4.5) { printf("deep=%a\n", deep.mid.in.z); rc |= 8; }

    if (!rc) printf("named struct in anonymous aggregate OK\n");
    return rc;
}
