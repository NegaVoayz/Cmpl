#include "pp.h"

void
cond_init(CondStack* cs)
{
    cs->depth = 0;
}

void
cond_push(CondStack* cs, int taking)
{
    if (cs->depth >= COND_STACK_MAX) return;

    /* If any ancestor is skipping, we skip regardless */
    int ancestor_skip = 0;

    for (int i = 0; i < cs->depth; i++) {
        if (cs->states[i] == COND_SKIPPING) {
            ancestor_skip = 1;
            break;
        }
    }

    cs->states[cs->depth++] = (ancestor_skip || !taking)
                              ? COND_SKIPPING : COND_TAKING;
}

int
cond_is_skipping(CondStack* cs)
{
    for (int i = 0; i < cs->depth; i++) {
        if (cs->states[i] == COND_SKIPPING)
            return 1;
    }
    return 0;
}

static int
ancestor_skipping(CondStack* cs)
{
    for (int i = 0; i < cs->depth - 1; i++) {
        if (cs->states[i] == COND_SKIPPING)
            return 1;
    }
    return 0;
}

int
cond_else(CondStack* cs)
{
    if (cs->depth == 0) return -1;

    CondState* top = &cs->states[cs->depth - 1];

    if (*top == COND_TAKING) {
        *top = COND_SKIPPING;
    } else if (!ancestor_skipping(cs)) {
        *top = COND_TAKING;
    }

    return 0;
}

int
cond_elif(CondStack* cs, int taking)
{
    if (cs->depth == 0) return -1;

    CondState* top = &cs->states[cs->depth - 1];

    if (*top == COND_TAKING) {
        *top = COND_SKIPPING;
    } else if (!ancestor_skipping(cs) && taking) {
        *top = COND_TAKING;
    }

    return 0;
}

int
cond_endif(CondStack* cs)
{
    if (cs->depth == 0) return -1;
    cs->depth--;
    return 0;
}
