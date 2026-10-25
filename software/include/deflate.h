/* deflate.h -- P32W C library (D25): Deflate compression (RFC 1951), as
 * used by gzip and zip, and CRC-32. Streams through callbacks, so neither
 * side needs the whole file in memory. */
#ifndef _DEFLATE_H
#define _DEFLATE_H

/* the CRC-32 of zip and gzip: crc32(0, buf, n), or continued over pieces */
unsigned crc32(unsigned crc, const void *buf, unsigned n);

/* in(ctx): the next compressed byte, or -1 at the end of the input */
typedef int (*zin_fn)(void *ctx);
/* read(ctx, p, n): up to n bytes into p; 0 at the end, -1 on an error */
typedef int (*zread_fn)(void *ctx, unsigned char *p, unsigned n);
/* out(ctx, p, n): n bytes of output; 0, or -1 to stop */
typedef int (*zout_fn)(void *ctx, const unsigned char *p, unsigned n);

/* raw Deflate data to its original bytes, to the end of the final block.
   It may read a few bytes beyond it (a gzip trailer's): inflate_rest hands
   them back. 0, or one of the Z_ errors. */
int inflate_raw(zin_fn in, void *ictx, zout_fn out, void *octx);
/* after inflate_raw: the bytes it read past the end of the data (at most
   4), copied to rest; their count */
int inflate_rest(unsigned char *rest);

/* bytes to raw Deflate data; level 0 (stored) to 9 (smallest, slowest),
   6 the usual. 0, or one of the Z_ errors. */
int deflate_raw(zread_fn read, void *ictx, zout_fn out, void *octx, int level);

#define Z_DATA   -1         /* the compressed data is damaged */
#define Z_EOF    -2         /* it ends too soon */
#define Z_IO     -3         /* read or out reported an error */
#define Z_MEM    -4         /* no memory */

#endif
