/* test_pp_stringize.c -- `#` stringize: quote an argument's text. */

#define STR(x) #x
#define XSTR(x) STR(x)

int main(void)
{
    const char* a = STR(hello);
    const char* b = XSTR(world);

    return (a[0] == 'h' && a[1] == 'e' && a[4] == 'o' && a[5] == '\0'
         && b[0] == 'w' && b[1] == 'o' && b[4] == 'd' && b[5] == '\0')
        ? 0 : 1;
}
