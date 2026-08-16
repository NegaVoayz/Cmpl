/* test_pp_paste.c -- `##` token paste: concatenate adjacent tokens. */

#define CAT(a, b) a##b
#define CAT3(a, b, c) a##b##c

int main(void)
{
    int xy = 42;
    int xyz = 7;

    return (CAT(x, y) == 42 && CAT3(x, y, z) == 7) ? 0 : 1;
}
