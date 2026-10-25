/* stdio.c -- streams open for reading and writing (r+, w+, a+), ftell,
 * ungetc, and the scanf family. Standard C only: the expected output is
 * what the build machine's own library prints. */
#include <stdio.h>
#include <string.h>

static void show(const char *path)
{
    FILE *f = fopen(path, "r");
    int c;
    printf("  [");
    while ((c = getc(f)) != EOF) putchar(c == '\n' ? '|' : c);
    printf("]\n");
    fclose(f);
}

static void make(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    fputs(text, f);
    fclose(f);
}

int main(void)
{
    FILE *f;
    char buf[64], s1[32], s2[32];
    int a, b, c, n, k;
    short h;
    unsigned u;

    printf("r+: read, then write in place\n");
    make("t.dat", "hello world\n");
    f = fopen("t.dat", "r+");
    for (k = 0; k < 5; k++) putchar(getc(f));
    putchar('\n');
    fseek(f, 0, SEEK_CUR);
    fputs("XYZ", f);
    printf("  ftell %ld\n", ftell(f));
    fseek(f, 0, SEEK_CUR);
    printf("  next %c\n", getc(f));
    fclose(f);
    show("t.dat");

    printf("r+: write, then read\n");
    f = fopen("t.dat", "r+");
    fputs("J", f);
    fflush(f);
    fgets(buf, sizeof buf, f);
    printf("  rest %s", buf);
    rewind(f);
    fgets(buf, sizeof buf, f);
    printf("  all %s", buf);
    fclose(f);
    show("t.dat");

    printf("w+: write, rewind, read\n");
    f = fopen("t.dat", "w+");
    for (k = 0; k < 600; k++) putc('a' + k % 26, f);    /* (more than one buffer) */
    printf("  ftell %ld\n", ftell(f));
    rewind(f);
    n = 0;
    while ((c = getc(f)) != EOF) n += c;
    printf("  sum %d\n", n);
    fseek(f, 26, SEEK_SET);
    putc('#', f);
    fseek(f, 0, SEEK_END);
    printf("  end %ld\n", ftell(f));
    fseek(f, 20, SEEK_SET);
    fread(buf, 1, 10, f);
    buf[10] = 0;
    printf("  20..29 %s\n", buf);
    fclose(f);

    printf("a+: append, read from the start\n");
    make("t.dat", "one\n");
    f = fopen("t.dat", "a+");
    fputs("two\n", f);
    fseek(f, 0, SEEK_SET);
    while (fgets(buf, sizeof buf, f)) printf("  %s", buf);
    fputs("three\n", f);
    fclose(f);
    show("t.dat");

    printf("ungetc and ftell\n");
    make("t.dat", "abcdef");
    f = fopen("t.dat", "r");
    getc(f); getc(f);
    printf("  %ld", ftell(f));
    ungetc('b', f);
    printf(" %ld", ftell(f));
    printf(" %c", getc(f));
    printf(" %ld\n", ftell(f));
    fclose(f);

    printf("sscanf\n");
    n = sscanf("  42 -17 0x1F", "%d %d %i", &a, &b, &c);
    printf("  %d: %d %d %d\n", n, a, b, c);
    n = sscanf("017 0x10 -0", "%i %i %i", &a, &b, &c);
    printf("  %d: %d %d %d\n", n, a, b, c);
    n = sscanf("ff 777 12345", "%x %o %3d", &a, &b, &c);
    printf("  %d: %d %d %d\n", n, a, b, c);
    n = sscanf("name=ken;uid=7", "name=%[^;];uid=%d", s1, &a);
    printf("  %d: %s %d\n", n, s1, a);
    n = sscanf("hello world", "%s%n %s", s1, &k, s2);
    printf("  %d: %s %d %s\n", n, s1, k, s2);
    n = sscanf("abc123def", "%[a-z]%d%2c", s1, &a, s2);
    s2[2] = 0;
    printf("  %d: %s %d %s\n", n, s1, a, s2);
    n = sscanf("10 20 30", "%*d %d %d", &a, &b);
    printf("  %d: %d %d\n", n, a, b);
    n = sscanf("12:34", "%d:%d", &a, &b);
    printf("  %d: %d %d\n", n, a, b);
    n = sscanf("12-34", "%d:%d", &a, &b);
    printf("  %d: %d\n", n, a);
    n = sscanf("", "%d", &a);
    printf("  empty %d\n", n);
    n = sscanf("   ", "%s", s1);
    printf("  blank %d\n", n);
    n = sscanf("x", "%d", &a);
    printf("  x %d\n", n);
    n = sscanf("100%", "%d%%", &a);
    printf("  %d: %d\n", n, a);
    n = sscanf("-32768 4000000000", "%hd %u", &h, &u);
    printf("  %d: %d %u\n", n, h, u);
    n = sscanf("+5", "%d", &a);
    printf("  %d: %d\n", n, a);

    printf("fscanf\n");
    make("t.dat", "3 apples\n10 pears\n7 plums\n");
    f = fopen("t.dat", "r");
    while ((n = fscanf(f, "%d %31s", &a, s1)) == 2) printf("  %s %d\n", s1, a);
    printf("  last %d\n", n);
    fclose(f);
    remove("t.dat");
    return 0;
}
