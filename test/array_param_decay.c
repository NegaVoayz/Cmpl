/* Array parameters must decay to pointers (C11 6.7.6.3p7):
 *   int a[]   == int* a
 *   int a[N]  == int* a
 *   T*  a[]   == T** a
 * This file compiled as an array-typed parameter (not decayed) before
 * the fix, producing `ptrtoint ptr ... to [0 x ptr]` and a clang failure. */

typedef int Elem;

int sum_sized(int a[4], int n)
{
    int s = 0;

    for (int i = 0; i < n; i++)
        s += a[i];

    return s;
}

int sum_unsized(int a[], int n)
{
    int s = 0;

    for (int i = 0; i < n; i++)
        s += a[i];

    return s;
}

int first_ptr(Elem* a[], int n)
{
    return a[0][0] + a[1][0];
}

int main(void)
{
    int arr[4] = {1, 2, 3, 4};
    Elem e0[2] = {10, 20};
    Elem e1[2] = {30, 40};
    Elem* ptrs[2] = {e0, e1};

    int a = sum_sized(arr, 4);     /* 10 */
    int b = sum_unsized(arr, 4);   /* 10 */
    int c = first_ptr(ptrs, 2);    /* 40 */

    return (a - 10) + (b - 10) + (c - 40);  /* 0 on success */
}
