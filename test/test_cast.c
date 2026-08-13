typedef struct { const char* name; int kind; } Keyword;

int cmp(const void* a, const void* b)
{
    const Keyword* ka = (const Keyword*)a;
    const Keyword* kb = (const Keyword*)b;
    return ka->kind - kb->kind;
}
