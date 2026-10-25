/* malloc.c -- P32W C library (M2, D22; D32): boundary tags and size bins.
 *
 * Memory comes from sbrk in chunks, 8-byte aligned. Each chunk starts with
 * two words:
 *
 *     prevsize   the size of the chunk before, valid only when that one is
 *                free (a free chunk's size is written at its end, here)
 *     head       this chunk's size | CINUSE | PINUSE (the one before in use)
 *
 * and the user's block follows (so it is 8-byte aligned too). A free chunk
 * holds two list pointers in its block. With the sizes at both ends, free()
 * finds its neighbours and merges with them in O(1), with no search.
 *
 * Free chunks wait in bins: one for each size up to 512 bytes (8-byte
 * steps; a request of that size takes the first, with no search), then a
 * bin per power of two. A bitmap says which bins have chunks. A request no
 * bin can meet is carved from "top", the free end of the heap, which sbrk
 * extends. (The old allocator, K&R's first fit, walked its whole free list
 * on every free: slow once a program had thousands of free blocks.) */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct chunk {
    unsigned prevsize;
    unsigned head;
    struct chunk *fd, *bk;     /* (free chunks only) the bin's list */
} Chunk;

#define CINUSE   1u
#define PINUSE   2u
#define SIZE(c)  ((c)->head & ~7u)
#define AT(c, n) ((Chunk *)((char *)(c) + (n)))
#define MEM(c)   ((void *)((char *)(c) + 8))
#define CHUNK(p) ((Chunk *)((char *)(p) - 8))
#define MINCHUNK 16u            /* header and the two list pointers */

#define NSMALL   65             /* bins 2..64: chunks of 16..512 bytes, exactly */
#define NBINS    (NSMALL + 23)  /* then one per power of two: 2^9.. up to 2^31 */

static Chunk *bins[NBINS];
static unsigned binmap[(NBINS + 31) / 32];
static Chunk *top;              /* the heap's free end (0 before the first malloc) */
static unsigned topsize;

static int binof(unsigned size)
{
    int k = 9;
    if (size <= 512) return size >> 3;
    while (k < 31 && size >> (k + 1)) k++;
    return NSMALL + k - 9;
}

static void link_free(Chunk *c, unsigned size)
{
    int b = binof(size);
    c->head = size | PINUSE;            /* (a free chunk's neighbours are in use) */
    AT(c, size)->prevsize = size;
    AT(c, size)->head &= ~PINUSE;
    c->bk = 0;
    c->fd = bins[b];
    if (c->fd) c->fd->bk = c;
    bins[b] = c;
    binmap[b >> 5] |= 1u << (b & 31);
}

static void unlink_free(Chunk *c)
{
    int b = binof(SIZE(c));
    if (c->bk) c->bk->fd = c->fd;
    else if (!(bins[b] = c->fd)) binmap[b >> 5] &= ~(1u << (b & 31));
    if (c->fd) c->fd->bk = c->bk;
}

/* the first bin at or above b with chunks in it, or -1 */
static int nextbin(int b)
{
    unsigned w;
    while (b < NBINS) {
        w = binmap[b >> 5] >> (b & 31);
        if (w) {
            while (!(w & 1)) { w >>= 1; b++; }
            return b;
        }
        b = (b | 31) + 1;
    }
    return -1;
}

/* make top at least need bytes (with MINCHUNK to spare, so top always
   has room for a fence: below) */
static int grow(unsigned need)
{
    char *p, *end;
    unsigned more;
    if (need > 0x7FFF0000u) return 0;
    more = (need + MINCHUNK - topsize + 4095) & ~4095u;     /* (topsize < need + MINCHUNK here) */
    if (more < 8192) more = 8192;
    p = sbrk(0);
    if (top && p == (char *)top + topsize) {        /* the heap goes on */
        if (sbrk((int)more) == (char *)-1) return 0;
        topsize += more;
        top->head = topsize | (top->head & PINUSE);
        return 1;
    }
    /* the first time, or someone else moved the break: a new region. The
       old top ends in a fence, an in-use header, so nothing merges past it */
    if ((unsigned)p & 7 && sbrk(8 - ((unsigned)p & 7)) == (char *)-1) return 0;
    more = (need + MINCHUNK + 4095) & ~4095u;
    if (more < 8192) more = 8192;
    p = sbrk((int)more);
    if (p == (char *)-1) return 0;
    if (top) {
        Chunk *fence = AT(top, topsize - 8);
        fence->head = 8 | CINUSE;
        if (topsize - 8 >= MINCHUNK)                /* (the chunk before top is never free: */
            link_free(top, topsize - 8);            /*  free() merges it into top) */
        else
            top->head = (topsize - 8) | CINUSE | (top->head & PINUSE);
    }
    end = p + more;
    top = (Chunk *)p;
    topsize = (unsigned)(end - p);
    top->head = topsize | PINUSE;                   /* (nothing before it to merge with) */
    return 1;
}

/* the chunk c (size, in use) cut to want bytes: the rest freed */
static void trim(Chunk *c, unsigned size, unsigned want)
{
    Chunk *rest;
    if (size - want < MINCHUNK) return;
    c->head = want | (c->head & 7);
    rest = AT(c, want);
    rest->head = (size - want) | PINUSE | CINUSE;
    free(MEM(rest));
}

static unsigned request(size_t n)
{
    if (n > 0x7FFF0000u) return 0;
    n = (n + 8 + 7) & ~7u;
    return n < MINCHUNK ? MINCHUNK : n;
}

void *malloc(size_t n)
{
    unsigned want = request(n), size;
    Chunk *c;
    int b;
    if (!want) return NULL;
    b = binof(want);
    if (b < NSMALL && bins[b]) {                    /* exact size: no search */
        c = bins[b];
        unlink_free(c);
        c->head |= CINUSE;
        AT(c, want)->head |= PINUSE;
        return MEM(c);
    }
    if (b >= NSMALL)                                /* the request's own bin: the first that fits */
        for (c = bins[b]; c; c = c->fd)
            if (SIZE(c) >= want) goto found;
    if ((b = nextbin(b + 1)) >= 0) {                /* any chunk of a bigger bin fits */
        c = bins[b];
        goto found;
    }
    if (topsize < want + MINCHUNK && !grow(want))   /* from top */
        return NULL;
    c = top;
    top = AT(c, want);
    topsize -= want;
    top->head = topsize | PINUSE;
    c->head = want | CINUSE | (c->head & PINUSE);
    return MEM(c);
found:
    unlink_free(c);
    size = SIZE(c);
    c->head |= CINUSE;
    AT(c, size)->head |= PINUSE;
    trim(c, size, want);
    return MEM(c);
}

void free(void *p)
{
    Chunk *c, *next;
    unsigned size;
    if (!p) return;
    c = CHUNK(p);
    size = SIZE(c);
    if (!(c->head & PINUSE)) {                      /* merge with the free chunk before */
        Chunk *prev = AT(c, -(int)c->prevsize);
        unlink_free(prev);
        size += SIZE(prev);
        c = prev;
    }
    next = AT(c, size);
    if (next == top) {                              /* into top */
        top = c;
        topsize += size;
        top->head = topsize | PINUSE;
        return;
    }
    if (!(next->head & CINUSE)) {                   /* merge with the free chunk after */
        unlink_free(next);
        size += SIZE(next);
    }
    link_free(c, size);
}

void *calloc(size_t n, size_t size)
{
    void *p;
    if (size && n > 0x7FFF0000u / size) return NULL;
    p = malloc(n * size);
    if (p) memset(p, 0, n * size);
    return p;
}

void *realloc(void *p, size_t n)
{
    Chunk *c, *next;
    unsigned want, size;
    void *q;
    if (!p) return malloc(n);
    if (!(want = request(n))) return NULL;
    c = CHUNK(p);
    size = SIZE(c);
    if (want <= size) {                             /* shrinking: give back the rest */
        trim(c, size, want);
        return p;
    }
    next = AT(c, size);
    if (next == top && topsize >= want - size + MINCHUNK) {    /* growing into top */
        topsize -= want - size;
        top = AT(c, want);
        top->head = topsize | PINUSE;
        c->head = want | (c->head & 7);
        return p;
    }
    if (next != top && !(next->head & CINUSE) && size + SIZE(next) >= want) {
        unsigned all = size + SIZE(next);           /* growing into the free chunk after */
        unlink_free(next);
        c->head = all | (c->head & 7);
        AT(c, all)->head |= PINUSE;
        trim(c, all, want);
        return p;
    }
    q = malloc(n);
    if (q) {
        memcpy(q, p, size - 8);
        free(p);
    }
    return q;
}
