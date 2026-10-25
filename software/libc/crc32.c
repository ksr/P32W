/* crc32.c -- P32W C library (D25): the CRC-32 of zip, gzip and PNG
 * (polynomial 0xEDB88320, reflected), a byte at a time from a table. */
#include <deflate.h>

static unsigned table[256];

unsigned crc32(unsigned crc, const void *buf, unsigned n)
{
    const unsigned char *p = buf;
    if (!table[1]) {
        unsigned c, k, b;
        for (k = 0; k < 256; k++) {
            c = k;
            for (b = 0; b < 8; b++)
                c = c & 1 ? 0xEDB88320 ^ (c >> 1) : c >> 1;
            table[k] = c;
        }
    }
    crc = ~crc;
    while (n--)
        crc = table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}
