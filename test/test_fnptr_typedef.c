/* test_fnptr_typedef.c — function pointer typedef arrays
 *
 * Verifies that typedef for function pointers (void (*T)(void))
 * are correctly parsed as typedefs, not function definitions,
 * and that enum-sized arrays of them get correct dimensions.
 *
 * Bug: parser treated "typedef void (*FnPtr)(void)" as FUNC_DEF.
 * Fix: detect TYPE_FUNC->TYPE_PTR->... in parse_var_list_decl.
 */
typedef enum { A, B, C, NUM } MyEnum;
typedef void (*FnPtr)(void);

FnPtr arr1[NUM];              /* should be [3 x ptr] */
FnPtr arr2[NUM][NUM];         /* should be [3 x [3 x ptr]] */

int main(void) {
    if (sizeof(arr1) != 3 * sizeof(FnPtr)) return 1;
    if (sizeof(arr2) != 3 * 3 * sizeof(FnPtr)) return 2;
    return 0;
}
