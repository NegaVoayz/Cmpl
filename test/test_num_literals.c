/* test_num_literals.c -- integer/float literal value semantics.
 *
 * Locks the base-prefix split (0x / 0 / plain), U/L/LL suffixes, and
 * float parsing to their exact values.  Each failed check ORs a bit
 * into rc so the failure mode is visible in the exit code.
 */

int main(void) {
    int rc = 0;
    if (123 != 123LL) rc |= 1;
    if (0 != 0LL) rc |= 2;
    if (0xFF != 255LL) rc |= 4;
    if (0x10 != 16LL) rc |= 8;
    if (010 != 8LL) rc |= 16;
    if (10 != 10LL) rc |= 32;
    if (08 != 0LL) rc |= 64;
    if (09 != 0LL) rc |= 128;
    if (00 != 0LL) rc |= 256;
    if (0x7FFFFFFF != 2147483647LL) rc |= 512;
    if (123U != 123LL) rc |= 1024;
    if (123L != 123LL) rc |= 2048;
    if (123ULL != 123LL) rc |= 4096;
    if (9223372036854775807LL != 0x7FFFFFFFFFFFFFFFLL) rc |= 8192;
    if (1.5 != 1.5) rc |= 16384;
    if (1e5 != 100000.0) rc |= 32768;
    if (0x1p3 != 8.0) rc |= 65536;
    return rc;
}
