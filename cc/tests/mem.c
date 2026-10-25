/* memcpy and memset (libc/mem.s): every source and destination offset
   mod 4 and lengths 0-40, against a byte loop; bytes either side untouched */
#include <stdio.h>
#include <string.h>
static unsigned char a[96], b[96], ref[96];
int main(void)
{
    int so, dofs, n, i, bad = 0, checks = 0;
    for (so = 0; so < 4; so++)
        for (dofs = 0; dofs < 4; dofs++)
            for (n = 0; n <= 40; n++) {
                for (i = 0; i < 96; i++) { a[i] = i * 13 + 1; b[i] = ref[i] = 0xEE; }
                for (i = 0; i < n; i++) ref[8 + dofs + i] = a[8 + so + i];
                if (memcpy(b + 8 + dofs, a + 8 + so, n) != b + 8 + dofs) bad++;
                if (memcmp(b, ref, 96)) bad++;
                for (i = 0; i < n; i++) ref[8 + dofs + i] = 0xA5 + so;
                if (memset(b + 8 + dofs, 0xA5 + so + 0x100, n) != b + 8 + dofs) bad++;
                if (memcmp(b, ref, 96)) bad++;
                checks += 4;
            }
    printf("%d checks, %d bad\n", checks, bad);
    return bad != 0;
}
