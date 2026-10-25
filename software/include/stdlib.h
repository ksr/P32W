/* stdlib.h -- P32W C library (M2, D22) */
#ifndef _STDLIB_H
#define _STDLIB_H
#include <stddef.h>
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 32767
void *malloc(size_t n);
void *calloc(size_t n, size_t size);
void *realloc(void *p, size_t n);
void free(void *p);
void exit(int status);
void _exit(int status);
void abort(void);
int atexit(void (*fn)(void));
int atoi(const char *s);
long atol(const char *s);
long strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
long long strtoll(const char *s, char **end, int base);
unsigned long long strtoull(const char *s, char **end, int base);
long long atoll(const char *s);
long long llabs(long long v);
double strtod(const char *s, char **end);
double atof(const char *s);
int abs(int v);
long labs(long v);
int rand(void);
void srand(unsigned seed);
void qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t n, size_t size,
              int (*cmp)(const void *, const void *));
char *getenv(const char *name);
int system(const char *cmd);                /* sh -c cmd; wait's status */
int mkstemp(char *template);                /* ...XXXXXX: a new file's descriptor */
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);
extern char **environ;
#endif
