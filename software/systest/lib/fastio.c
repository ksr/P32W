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

/* printf writes runs of text at once: each kind of run stopping at the end of
   the room, with nothing past it touched, and the whole length returned */
static int snprintf_bad(void)
{
    char b[16];
    int bad = 0;
    memset(b, 'Z', sizeof b);
    if (snprintf(b, 6, "abcdefghij") != 10 || strcmp(b, "abcde") || b[6] != 'Z') bad |= 1;
    memset(b, 'Z', sizeof b);
    if (snprintf(b, 6, "%s", "abcdefghij") != 10 || strcmp(b, "abcde") || b[6] != 'Z') bad |= 2;
    memset(b, 'Z', sizeof b);
    if (snprintf(b, 6, "%8d", 1) != 8 || strcmp(b, "     ") || b[6] != 'Z') bad |= 4;
    memset(b, 'Z', sizeof b);
    if (snprintf(b, 6, "ab%-8sc", "x") != 11 || strcmp(b, "abx  ") || b[6] != 'Z') bad |= 8;
    memset(b, 'Z', sizeof b);
    if (snprintf(b, 1, "abc") != 3 || b[0] || b[1] != 'Z') bad |= 16;
    memset(b, 'Z', sizeof b);
    if (snprintf(b, 0, "abc") != 3 || b[0] != 'Z') bad |= 32;
    if (sprintf(b, "%.3s|%3s|%-3s|", "abcdef", "a", "b") != 12 || strcmp(b, "abc|  a|b  |")) bad |= 64;
    if (sprintf(b, "%d%%%c%s", 7, 'x', "") != 3 || strcmp(b, "7%x")) bad |= 128;
    memset(b, 'Z', sizeof b);
    if (snprintf(b, 4, "%d", 123456) != 6 || strcmp(b, "123") || b[4] != 'Z') bad |= 256;
    if (sprintf(b, "%d|%d", 0, -7) != 4 || strcmp(b, "0|-7")) bad |= 512;
    if (sprintf(b, "%d", -2147483647 - 1) != 11 || strcmp(b, "-2147483648")) bad |= 1024;
    return bad;
}

/* fputs and printf both ways: straight into the buffer, and past its end */
static int run_bad(void)
{
    static char big[BUFSIZ + 40], b[3 * BUFSIZ];
    FILE *f = fopen("fastio.tmp", "w");
    int bad = 0, k, n, m = sizeof big - 1;
    if (!f) return 1;
    memset(big, 'q', m);
    fputs("start ", f);
    fputs(big, f);
    fprintf(f, " %s ", big);
    fputs("end", f);
    fclose(f);
    if (!(f = fopen("fastio.tmp", "r"))) return 2;
    n = fread(b, 1, sizeof b, f);
    fclose(f);
    remove("fastio.tmp");
    if (n != 6 + 2 * m + 2 + 3) bad |= 4;
    if (memcmp(b, "start q", 7) || memcmp(b + n - 5, "q end", 5)) bad |= 8;
    for (k = 6; k < 6 + m; k++) if (b[k] != 'q' || b[k + m + 1] != 'q') bad |= 16;
    return bad;
}

int main(void)
{
    printf("ctype: %d wrong, once %d\n", ctype_bad(), once());
    printf("stdio: %d\n", stdio_bad());
    printf("runs: %d %d\n", snprintf_bad(), run_bad());
    putchar('o'); putc('k', stdout); putchar('\n');
    return 0;
}
