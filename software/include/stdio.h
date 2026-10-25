/* stdio.h -- P32W C library (M2, D22): buffered streams on file descriptors. */
#ifndef _STDIO_H
#define _STDIO_H
#include <stddef.h>
#include <stdarg.h>
#define EOF (-1)
#define BUFSIZ 512
#define FOPEN_MAX 16
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2
#define L_tmpnam 32
#define TMP_MAX 10000
typedef struct _FILE {
    int fd;
    int flags;                  /* _F_READ _F_WRITE _F_EOF _F_ERR _F_LBF _F_NBF _F_RDING */
    char *buf;
    int pos;                    /* next byte in buf */
    int len;                    /* bytes in buf (reading) */
    int ungot;                  /* ungetc'd character or -1 */
} FILE;
#define _F_READ  1
#define _F_WRITE 2
#define _F_EOF   4
#define _F_ERR   8
#define _F_LBF   16
#define _F_NBF   32
#define _F_MYBUF 64
#define _F_RDING 128            /* buf holds read-ahead (else, if writable, unwritten output) */
extern FILE *stdin, *stdout, *stderr;
FILE *fopen(const char *path, const char *mode);
FILE *fdopen(int fd, const char *mode);
int fclose(FILE *f);
int fflush(FILE *f);
int fgetc(FILE *f);
int getc(FILE *f);
int getchar(void);
int ungetc(int c, FILE *f);
char *fgets(char *s, int n, FILE *f);
int fputc(int c, FILE *f);
int putc(int c, FILE *f);
int putchar(int c);
int fputs(const char *s, FILE *f);
int puts(const char *s);
size_t fread(void *p, size_t size, size_t n, FILE *f);
size_t fwrite(const void *p, size_t size, size_t n, FILE *f);
int feof(FILE *f);
int ferror(FILE *f);
void clearerr(FILE *f);
int fileno(FILE *f);
int fseek(FILE *f, long off, int whence);
long ftell(FILE *f);
void rewind(FILE *f);
int printf(const char *fmt, ...);
int fprintf(FILE *f, const char *fmt, ...);
int sprintf(char *s, const char *fmt, ...);
int snprintf(char *s, size_t n, const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int vfprintf(FILE *f, const char *fmt, va_list ap);
int vsprintf(char *s, const char *fmt, va_list ap);
int vsnprintf(char *s, size_t n, const char *fmt, va_list ap);
int scanf(const char *fmt, ...);
int fscanf(FILE *f, const char *fmt, ...);
int sscanf(const char *s, const char *fmt, ...);
int vscanf(const char *fmt, va_list ap);
int vfscanf(FILE *f, const char *fmt, va_list ap);
int vsscanf(const char *s, const char *fmt, va_list ap);
void perror(const char *s);
int remove(const char *path);
int rename(const char *old, const char *new_);      /* (a system call: rename(2)) */
FILE *freopen(const char *path, const char *mode, FILE *f);
int setvbuf(FILE *f, char *buf, int mode, size_t size);
void setbuf(FILE *f, char *buf);
FILE *tmpfile(void);
char *tmpnam(char *s);
FILE *popen(const char *cmd, const char *mode);    /* sh -c cmd, its output ("r") or input ("w") */
int pclose(FILE *f);                                /* wait's status */
#endif
