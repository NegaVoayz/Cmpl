/* test_enum_arr.c — array-of-typedef with enum-sized dimension
 *
 * Verifies that global arrays whose element type is a function-pointer
 * typedef get the correct array size when the dimension is an enum sentinel.
 *
 * Expected: both arr and arr2 have size 3 (NUM_A == C == 3).
 * Bug: arr was [0 x ptr] because the fixup in ir_gen.c rebuilt the
 *      array type with hardcoded size 0, clobbering the correctly
 *      resolved dimension.
 */
typedef enum { A, B, C, NUM_A } MyEnum;
typedef void (*FnPtr)(void);

FnPtr arr[NUM_A];
int   arr2[NUM_A];

int main(void) {
    /* if arr is [0 x ptr], sizeof is 0; if correct [3 x ptr], sizeof is 24 */
    if (sizeof(arr) != 3 * sizeof(FnPtr)) return 1;
    if (sizeof(arr2) != 3 * sizeof(int))   return 2;
    return 0;
}
