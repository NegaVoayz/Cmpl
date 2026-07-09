#define VALUE 42
#define GREETING "Hello from macro"
#define ADD(a, b) ((a) + (b))
#define SQUARE(x) ((x) * (x))

#ifdef VALUE
int defined_test = VALUE;
#endif

#ifndef UNDEFINED
int not_defined = 1;
#endif

#undef VALUE
#ifdef VALUE
int should_not_appear;
#else
int value_was_undefed = 1;
#endif

#if 1
int if_test = 1;
#else
int should_not_appear2;
#endif

#if defined(GREETING)
int greeting_defined = 1;
#endif

#if ADD(2, 3) == 5
int macro_expr_test = 1;
#endif

#pragma once
#pragma something ignored

int main() {
    int x = SQUARE(4);
    int y = ADD(10, 20);
    return 0;
}
