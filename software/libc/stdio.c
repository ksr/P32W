/* stdio.c -- P32W C library (M2, D22): buffered streams on file descriptors.
 * Descriptors 0, 1, 2 are stdin, stdout, stderr. stdout is line-buffered
 * when it is a terminal, else fully buffered (decided at its first output);
 * stderr is unbuffered.
 *
 * A stream open for both (r+, w+, a+) uses its one buffer either way:
 * _F_RDING says it holds read-ahead. Writing after reading seeks back over
 * what was read ahead but not used; reading after writing flushes first. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

static char inbuf[BUFSIZ], outbuf[BUFSIZ];
static FILE files[FOPEN_MAX] = {
    { 0, _F_READ, inbuf, 0, 0, -1 },
    { 1, _F_WRITE | _F_LBF, outbuf, 0, 0, -1 },
    { 2, _F_WRITE | _F_NBF, NULL, 0, 0, -1 },
};
FILE *stdin = &files[0], *stdout = &files[1], *stderr = &files[2];
static int stdout_known;            /* stdout's buffering decided (at its first character) */

int fflush(FILE *f)
{
    int k = 0, n;
    if (!f) {
        int i;
        for (i = 0; i < FOPEN_MAX; i++)
            if (files[i].flags & _F_WRITE) fflush(&files[i]);
        return 0;
    }
    if (!(f->flags & _F_WRITE) || (f->flags & _F_RDING)) return 0;
    f->room = 0;                        /* (whatever comes next, fputc sets it again) */
    while (k < f->pos) {
        n = write(f->fd, f->buf + k, f->pos - k);
        if (n <= 0) { f->flags |= _F_ERR; f->pos = 0; return EOF; }
        k += n;
    }
    f->pos = 0;
    return 0;
}

/* a stream about to be written: drop any read-ahead (the file position
   goes back to the next unread byte) */
static void towrite(FILE *f)
{
    if (f->flags & _F_RDING) {
        int ahead = f->len - f->pos + (f->ungot >= 0);
        if (ahead) lseek(f->fd, -ahead, SEEK_CUR);
        f->pos = f->len = 0;
        f->ungot = -1;
        f->flags &= ~_F_RDING;
    }
}

int fputc(int c, FILE *f)
{
    unsigned char ch = c;
    if (!(f->flags & _F_WRITE)) { f->flags |= _F_ERR; return EOF; }
    towrite(f);
    if (f == stdout && !stdout_known) {
        stdout_known = 1;
        if (!isatty(1)) f->flags &= ~_F_LBF;    /* a file or a pipe: whole buffers */
    }
    if (f->flags & _F_NBF) {
        if (write(f->fd, &ch, 1) != 1) { f->flags |= _F_ERR; return EOF; }
        return ch;
    }
    if (f->pos == BUFSIZ && fflush(f)) return EOF;      /* (putc can leave it full) */
    f->buf[f->pos++] = ch;
    if (f->pos == BUFSIZ || ((f->flags & _F_LBF) && ch == '\n'))
        if (fflush(f)) return EOF;
    if (!(f->flags & _F_LBF)) f->room = BUFSIZ - f->pos;
    return ch;
}
int (putc)(int c, FILE *f) { return fputc(c, f); }
int (putchar)(int c) { return fputc(c, stdout); }

int fputs(const char *s, FILE *f)
{
    while (*s)
        if (putc(*s++, f) == EOF) return EOF;
    return 0;
}
int puts(const char *s)
{
    if (fputs(s, stdout) == EOF) return EOF;
    return fputc('\n', stdout) == EOF ? EOF : 0;
}

int fgetc(FILE *f)
{
    if (f->ungot >= 0) { int c = f->ungot; f->ungot = -1; return c; }
    if (!(f->flags & _F_READ)) { f->flags |= _F_ERR; return EOF; }
    if (!(f->flags & _F_RDING)) {      /* (after writing: the output out first) */
        if (fflush(f)) return EOF;
        f->pos = f->len = 0;
        f->flags |= _F_RDING;
    }
    if (f->pos >= f->len) {
        int n;
        if (f == stdin) fflush(stdout);
        n = read(f->fd, f->buf, BUFSIZ);
        if (n <= 0) { f->flags |= n == 0 ? _F_EOF : _F_ERR; return EOF; }
        f->len = n;
        f->pos = 0;
    }
    return (unsigned char)f->buf[f->pos++];
}
int getc(FILE *f) { return fgetc(f); }
int getchar(void) { return fgetc(stdin); }
int ungetc(int c, FILE *f)
{
    if (c == EOF) return EOF;
    if (f->ungot < 0 && (f->flags & _F_RDING) && f->pos > 0) {    /* (into the buffer it came from:
                                                                       scanf may give back several) */
        f->buf[--f->pos] = c;
        f->flags &= ~_F_EOF;
        return c;
    }
    f->ungot = (unsigned char)c;
    f->flags &= ~_F_EOF;
    return c;
}

char *fgets(char *s, int n, FILE *f)
{
    char *p = s;
    int c = 0;
    while (n > 1 && (c = fgetc(f)) != EOF) {
        *p++ = c;
        n--;
        if (c == '\n') break;
    }
    if (p == s) return NULL;
    *p = 0;
    return s;
}

size_t fread(void *ptr, size_t size, size_t n, FILE *f)
{
    unsigned char *p = ptr;
    size_t total = size * n, k = 0;
    int c;
    if (total && f->ungot >= 0) { p[k++] = f->ungot; f->ungot = -1; }
    while (k < total) {
        if ((f->flags & _F_RDING) && f->pos < f->len) {          /* what the buffer holds, in one copy */
            size_t m = f->len - f->pos;
            if (m > total - k) m = total - k;
            memcpy(p + k, f->buf + f->pos, m);
            f->pos += m;
            k += m;
            continue;
        }
        if ((c = fgetc(f)) == EOF) break;   /* (refills the buffer) */
        p[k++] = c;
    }
    return size ? k / size : 0;
}
size_t fwrite(const void *ptr, size_t size, size_t n, FILE *f)
{
    const unsigned char *p = ptr;
    size_t total = size * n, k = 0;
    if (f->flags & _F_WRITE) towrite(f);
    if ((f->flags & _F_WRITE) && !(f->flags & (_F_NBF | _F_LBF)) && f->buf) {
        f->room = 0;
        while (k < total) {             /* a fully buffered stream: whole runs into the buffer */
            size_t m = BUFSIZ - f->pos;
            if (m > total - k) m = total - k;
            memcpy(f->buf + f->pos, p + k, m);
            f->pos += m;
            k += m;
            if (f->pos == BUFSIZ && fflush(f)) break;
        }
        return size ? k / size : 0;
    }
    for (; k < total; k++)
        if (fputc(p[k], f) == EOF) break;
    return size ? k / size : 0;
}

int feof(FILE *f) { return (f->flags & _F_EOF) != 0; }
int ferror(FILE *f) { return (f->flags & _F_ERR) != 0; }
void clearerr(FILE *f) { f->flags &= ~(_F_EOF | _F_ERR); }
int fileno(FILE *f) { return f->fd; }

static FILE *newfile(int fd, int flags)
{
    int i;
    for (i = 3; i < FOPEN_MAX; i++)
        if (!files[i].flags) {
            FILE *f = &files[i];
            f->fd = fd; f->flags = flags | _F_MYBUF;
            f->buf = malloc(BUFSIZ);
            f->pos = f->len = 0;
            f->ungot = -1;
            if (!f->buf) { f->flags = 0; return NULL; }
            return f;
        }
    errno = EMFILE;
    return NULL;
}

static int mode_flags(const char *mode, int *oflags)
{
    int plus = strchr(mode, '+') != NULL;
    switch (mode[0]) {
    case 'r': *oflags = plus ? O_RDWR : O_RDONLY; return plus ? _F_READ | _F_WRITE : _F_READ;
    case 'w': *oflags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC; return plus ? _F_READ | _F_WRITE : _F_WRITE;
    case 'a': *oflags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND; return plus ? _F_READ | _F_WRITE : _F_WRITE;
    }
    return 0;
}

FILE *fopen(const char *path, const char *mode)
{
    int of, fl = mode_flags(mode, &of), fd;
    FILE *f;
    if (!fl) { errno = EINVAL; return NULL; }
    fd = open(path, of, 0666);
    if (fd < 0) return NULL;
    f = newfile(fd, fl);
    if (!f) close(fd);
    return f;
}
FILE *fdopen(int fd, const char *mode)
{
    int of, fl = mode_flags(mode, &of);
    return fl ? newfile(fd, fl) : NULL;
}

int fclose(FILE *f)
{
    int r = fflush(f);
    if (close(f->fd) < 0) r = EOF;
    if (f->flags & _F_MYBUF) free(f->buf);
    f->flags = 0;
    return r;
}

int fseek(FILE *f, long off, int whence)
{
    if (fflush(f)) return -1;
    if (whence == SEEK_CUR && (f->flags & _F_RDING)) off -= f->len - f->pos + (f->ungot >= 0);
    f->pos = f->len = 0;
    f->ungot = -1;
    f->flags &= ~(_F_EOF | _F_RDING);
    return lseek(f->fd, off, whence) < 0 ? -1 : 0;
}
long ftell(FILE *f)
{
    long p = lseek(f->fd, 0, SEEK_CUR);
    if (p < 0) return -1;
    if (f->flags & _F_RDING) return p - (f->len - f->pos) - (f->ungot >= 0);
    if (f->flags & _F_WRITE) return p + f->pos;
    return p;
}
void rewind(FILE *f) { fseek(f, 0, SEEK_SET); clearerr(f); }

void perror(const char *s)
{
    if (s && *s) { fputs(s, stderr); fputs(": ", stderr); }
    fputs(strerror(errno), stderr);
    fputc('\n', stderr);
}

/* a file, or an empty directory */
int remove(const char *path)
{
    if (unlink(path) == 0) return 0;
    return errno == EISDIR ? rmdir(path) : -1;
}

/* the stream f, now on path (the descriptor number is kept, so
   freopen(..., stdin) is still descriptor 0) */
FILE *freopen(const char *path, const char *mode, FILE *f)
{
    int of, fl = mode_flags(mode, &of), fd;
    fflush(f);
    close(f->fd);
    if (!fl || !path || (fd = open(path, of, 0666)) < 0) {
        if (!fl) errno = EINVAL;
        if (f->flags & _F_MYBUF) free(f->buf);
        f->flags = 0;
        return NULL;
    }
    if (fd != f->fd) {
        dup2(fd, f->fd);
        close(fd);
    }
    if (!f->buf) {                          /* (stderr has none) */
        if (!(f->buf = malloc(BUFSIZ))) { f->flags = 0; return NULL; }
        fl |= _F_MYBUF;
    }
    f->flags = fl | (f->flags & (_F_MYBUF | _F_LBF | _F_NBF));
    f->pos = f->len = 0;
    f->ungot = -1;
    return f;
}

/* full, line or no buffering. The stream keeps its own BUFSIZ buffer
   (C lets it ignore buf) */
int setvbuf(FILE *f, char *buf, int mode, size_t size)
{
    (void)buf; (void)size;
    if (mode != _IOFBF && mode != _IOLBF && mode != _IONBF) return -1;
    fflush(f);
    if (mode != _IONBF && !f->buf) {
        if (!(f->buf = malloc(BUFSIZ))) return -1;
        f->flags |= _F_MYBUF;
        f->pos = f->len = 0;
    }
    f->flags &= ~(_F_LBF | _F_NBF);
    if (mode == _IOLBF) f->flags |= _F_LBF;
    else if (mode == _IONBF) f->flags |= _F_NBF;
    if (f == stdout) stdout_known = 1;      /* (the program's choice, not the terminal's) */
    return 0;
}
void setbuf(FILE *f, char *buf) { setvbuf(f, buf, buf ? _IOFBF : _IONBF, BUFSIZ); }

/* a new file, removed when closed (or when the program ends) */
FILE *tmpfile(void)
{
    char name[] = "/tmp/tmpXXXXXX";
    int fd = mkstemp(name);
    FILE *f;
    if (fd < 0) return NULL;
    unlink(name);
    if (!(f = fdopen(fd, "w+"))) close(fd);
    return f;
}

/* a name no file has yet: in s (L_tmpnam bytes), or a static buffer */
char *tmpnam(char *s)
{
    static char buf[L_tmpnam];
    static int seq;
    int k;
    if (!s) s = buf;
    for (k = 0; k < TMP_MAX; k++) {
        sprintf(s, "/tmp/tmp%d.%d", getpid(), seq++);
        if (access(s, 0) < 0) return s;
    }
    return NULL;
}
