/* strasm.c -- the assembly strlen, strcmp, strncmp and strchr (D91) against
 * plain C versions of what the standard says, over the awkward cases: empty
 * strings, bytes above 127, n of 0, a difference past n, searching for NUL. */
#include <stdio.h>
#include <string.h>

static size_t len(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
static int cmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
static int ncmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        if (*a != *b) return (unsigned char)*a - (unsigned char)*b;
        if (!*a) return 0;
    }
    return 0;
}
static char *chr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c) return (char *)s;
        if (!*s) return NULL;
    }
}
static int sign(int v) { return v < 0 ? -1 : v > 0; }

int main(void)
{
    static const char *const s[] = { "", "a", "ab", "abc", "abd", "abcd", "b", "\377", "\200a", "a\377", "a\200", "ABC", 0 };
    static const int cs[] = { 0, 'a', 'b', 'c', 'z', 255, 128, 0x161, -1, -159 };
    int i, j, k, bad = 0, tests = 0;
    for (i = 0; s[i]; i++) {
        if (strlen(s[i]) != len(s[i])) bad++;
        tests++;
        for (k = 0; k < 10; k++, tests++)
            if (strchr(s[i], cs[k]) != chr(s[i], cs[k])) bad++;
        for (j = 0; s[j]; j++) {
            if (sign(strcmp(s[i], s[j])) != sign(cmp(s[i], s[j]))) bad++;
            tests++;
            for (k = 0; k < 7; k++, tests++)
                if (sign(strncmp(s[i], s[j], k)) != sign(ncmp(s[i], s[j], k))) bad++;
        }
    }
    printf("%d wrong in %d\n", bad, tests);
    return 0;
}
