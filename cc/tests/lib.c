/* the C library */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
static int cmpstr(const void *a, const void *b) { return strcmp(*(char **)a, *(char **)b); }
int main(void)
{
    char buf[100], *p, *toks[10], *names[] = { "pear", "apple", "fig", "banana", "cherry" };
    int i, n, *blocks[50];
    unsigned sum = 0;
    strcpy(buf, "the quick  brown fox");
    for (n = 0, p = strtok(buf, " "); p; p = strtok(0, " ")) toks[n++] = p;
    for (i = 0; i < n; i++) printf("[%s]", toks[i]);
    printf(" %d\n", n);
    printf("%d %d %d %d\n", strcmp("abc", "abd") < 0, strncmp("abcx", "abcy", 3), (int)strspn("aaab", "a"), (int)strcspn("hello", "l"));
    printf("%s %s %s\n", strchr("hello", 'l'), strrchr("hello", 'l'), strstr("haystack", "st"));
    memset(buf, 'x', 5); buf[5] = 0; printf("%s\n", buf);
    memmove(buf + 1, buf, 4); buf[0] = 'y'; printf("%s %d\n", buf, memcmp("ab", "ac", 2) < 0);
    qsort(names, 5, sizeof names[0], cmpstr);
    for (i = 0; i < 5; i++) printf("%s ", names[i]);
    printf("\n");
    for (i = 0; i < 50; i++) { blocks[i] = malloc((i % 7 + 1) * 40); blocks[i][0] = i; }
    for (i = 0; i < 50; i += 2) free(blocks[i]);
    for (i = 0; i < 50; i += 2) { blocks[i] = calloc(10, 4); blocks[i][0] = i; }
    for (i = 0; i < 50; i++) sum += blocks[i][0];
    blocks[0] = realloc(blocks[0], 4000); blocks[0][999] = 5;
    printf("malloc sum %u %d\n", sum, blocks[0][0]);
    printf("%d %d %d %d %c %c\n", isdigit('7'), isalpha('7') != 0, isspace('\t') != 0, isupper('a'), toupper('q'), tolower('Q'));
    printf("%ld %lu %d %d\n", strtol("-0x10", 0, 16), strtoul("777", 0, 8), atoi("  42abc"), abs(-9));
    sprintf(buf, "%5.2s|%-4d|%04x|%#o|%#x", "xyz", 7, 255, 8, 255);
    printf("%s\n", buf);
    n = snprintf(buf, 6, "%s", "truncated");
    printf("%s %d\n", buf, n);
    return 0;
}
