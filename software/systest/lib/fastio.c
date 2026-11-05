/* fastio.c -- what goes round a function call for speed (D89) must do what
 * the functions do: the ctype macros against the functions and the rules, and
 * putc into the buffer across every way a stream changes state. */
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int same(int a, int b) { return !a == !b; }

static int ctype_bad(void)
{
    int c, bad = 0;
    for (c = -1; c < 256; c++) {
        int up = c >= 'A' && c <= 'Z', lo = c >= 'a' && c <= 'z', dg = c >= '0' && c <= '9';
        int sp = c == ' ' || (c >= 9 && c <= 13);
        if (!same(isdigit(c), dg) || !same((isdigit)(c), dg)) bad++;
        if (!same(isupper(c), up) || !same((isupper)(c), up)) bad++;
        if (!same(islower(c), lo) || !same((islower)(c), lo)) bad++;
        if (!same(isalpha(c), up || lo) || !same((isalpha)(c), up || lo)) bad++;
        if (!same(isalnum(c), up || lo || dg) || !same((isalnum)(c), up || lo || dg)) bad++;
        if (!same(isspace(c), sp) || !same((isspace)(c), sp)) bad++;
    }
    return bad;
}

static int once(void)                   /* a macro uses its argument once */
{
    const char *s = "a1 ";
    const char *p = s;
    int n = 0;
    n += !!isalpha(*p++); n += !!isdigit(*p++); n += !!isspace(*p++);
    return n == 3 && p == s + 3;
}

static int stdio_bad(void)
{
    static char b[4 * BUFSIZ];
    FILE *f = fopen("fastio.tmp", "w");
    int k, c, bad = 0, n = 2 * BUFSIZ + 17;
    if (!f) return 1;
    for (k = 0; k < n; k++) putc('a' + k % 26, f);     /* across two full buffers */
    fputs("MID", f);
    fwrite("fw", 1, 2, f);
    for (k = 0; k < BUFSIZ; k++) fputc('0' + k % 10, f);
    fprintf(f, "%d|", 123);
    fseek(f, 5, SEEK_SET);                              /* back: what is buffered goes first */
    putc('X', f);
    fclose(f);
    if (!(f = fopen("fastio.tmp", "r"))) return 2;
    k = fread(b, 1, sizeof b, f);
    fclose(f);
    if (k != n + 3 + 2 + BUFSIZ + 4) bad |= 4;
    if (b[4] != 'e' || b[5] != 'X' || b[6] != 'g') bad |= 8;
    if (memcmp(b + n, "MIDfw0", 6)) bad |= 16;
    if (memcmp(b + k - 4, "123|", 4) || b[k - 5] != '0' + (BUFSIZ - 1) % 10) bad |= 32;
    for (k = 6; k < n; k++) if (b[k] != 'a' + k % 26) bad |= 64;
    if (!(f = fopen("fastio.tmp", "r+"))) return 128;   /* read, write, read on one stream */
    c = getc(f);
    fseek(f, 1, SEEK_SET);
    putc('Y', f);
    fseek(f, 0, SEEK_SET);
    if (getc(f) != c || getc(f) != 'Y') bad |= 256;
    fclose(f);
    remove("fastio.tmp");
    return bad;
}

int main(void)
{
    printf("ctype: %d wrong, once %d\n", ctype_bad(), once());
    printf("stdio: %d\n", stdio_bad());
    putchar('o'); putc('k', stdout); putchar('\n');
    return 0;
}
