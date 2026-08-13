/* test_enum_val.c — enum explicit values and gaps
 *
 * Verifies that enum explicit values (= N) and gaps (= 30) are
 * correctly resolved by both opt_enum and resolve_array_sizes.
 *
 * Expected: B=10, C=11, NUM=12, not sequential B=1, C=2, NUM=3.
 * After fix: lr1_parse_expr stop_at_comma returned before reduce.
 */
typedef enum { A, B = 10, C, NUM } TE;
typedef enum { S0, S1 = 30, S2, NUM_S } GapEnum;

int val_b = B;
int val_n = NUM;
int val_s2 = S2;
int arr[NUM];      /* should be 12 */
int arr_gap[NUM_S]; /* should be 32 */

int main(void) {
    if (val_b != 10) return 1;
    if (val_n != 12) return 2;
    if (val_s2 != 31) return 3;
    if (sizeof(arr) != 12 * sizeof(int)) return 4;
    if (sizeof(arr_gap) != 32 * sizeof(int)) return 5;
    return 0;
}
