/* inflate.c -- P32W C library (D25): Deflate decompression (RFC 1951).
 *
 * A 32 KiB window, written out as it fills. Huffman codes decode through a
 * 9-bit table (one lookup for the common short codes) and fall back to the
 * canonical bit-by-bit decode for longer ones. Bits come LSB first from
 * in(); the bytes read past the end of the final block (at most a few) are
 * handed back through rest / nrest for the caller's trailer. */
#include <deflate.h>
#include <string.h>

#define FAST 9                          /* bits looked up at once */
#define WSIZE 32768

struct huff {
    unsigned short fast[1 << FAST];     /* len << 9 | symbol, for codes up to FAST bits; 0 = longer */
    short count[16];                    /* codes of each length */
    short symbol[288];                  /* symbols in canonical order */
};

static zin_fn in_;
static void *ictx_;
static zout_fn out_;
static void *octx_;
static unsigned bitbuf, bitcnt, padded; /* padded: zero bits added after the input ended */
static unsigned char window[WSIZE];
static unsigned wpos;
static int err;

static const unsigned short lbase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const unsigned char lext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const unsigned short dbase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const unsigned char dext[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

/* at least n (<= 24) bits in the buffer; zeros past the end of the input */
static void need(unsigned n)
{
    while (bitcnt < n) {
        int c = padded ? -1 : in_(ictx_);
        if (c < 0) { c = 0; padded += 8; }
        bitbuf |= (unsigned)c << bitcnt;
        bitcnt += 8;
    }
}

static void drop(unsigned n)
{
    if (n + padded > bitcnt) err = Z_EOF;   /* (into the padding: the input ended too soon) */
    bitbuf >>= n;
    bitcnt -= n;
}

static unsigned bits(unsigned n)
{
    unsigned v;
    if (!n) return 0;
    need(n);
    v = bitbuf & ((1u << n) - 1);
    drop(n);
    return v;
}

static int flush(void)
{
    if (wpos && out_(octx_, window, wpos) < 0) { err = Z_IO; return -1; }
    wpos = 0;
    return 0;
}

/* a code from lengths[0..n-1]: 0, or Z_DATA if they over-subscribe */
static int build(struct huff *h, const unsigned char *lengths, int n)
{
    short offs[16];
    unsigned short next[16];
    int len, sym, left = 1;
    unsigned code = 0;
    memset(h->count, 0, sizeof h->count);
    for (sym = 0; sym < n; sym++) h->count[lengths[sym]]++;
    for (len = 1; len < 16; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return Z_DATA;
    }
    offs[1] = 0;
    for (len = 1; len < 15; len++) offs[len + 1] = offs[len] + h->count[len];
    for (sym = 0; sym < n; sym++)
        if (lengths[sym]) h->symbol[offs[lengths[sym]]++] = sym;
    /* canonical codes, and the fast table for the short ones (bit-reversed: the stream is LSB first) */
    memset(h->fast, 0, sizeof h->fast);
    h->count[0] = 0;
    for (len = 1; len < 16; len++) {
        code = (code + h->count[len - 1]) << 1;
        next[len] = code;
    }
    for (sym = 0; sym < n; sym++) {
        unsigned l = lengths[sym], c, r = 0, k;
        if (!l) continue;
        c = next[l]++;
        if (l > FAST) continue;
        for (k = 0; k < l; k++) r |= ((c >> k) & 1) << (l - 1 - k);
        for (k = r; k < (1u << FAST); k += 1u << l)
            h->fast[k] = (unsigned short)(l << 9 | sym);
    }
    return 0;
}

static int decode(struct huff *h)
{
    int len, code = 0, first = 0, index = 0, count;
    unsigned e;
    need(FAST);
    e = h->fast[bitbuf & ((1 << FAST) - 1)];
    if (e) {
        drop(e >> 9);
        return e & 511;
    }
    for (len = 1; len < 16; len++) {   /* a longer code: canonical, a bit at a time */
        code |= bits(1);
        count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    err = Z_DATA;
    return -1;
}

static int codes(struct huff *lc, struct huff *dc)
{
    int sym;
    for (;;) {
        sym = decode(lc);
        if (err) return err;
        if (sym < 256) {
            window[wpos++] = sym;
            if (wpos == WSIZE && flush()) return err;
        } else if (sym == 256)
            return 0;
        else {
            unsigned len, dist, from;
            sym -= 257;
            if (sym >= 29) return Z_DATA;
            len = lbase[sym] + bits(lext[sym]);
            sym = decode(dc);
            if (err) return err;
            if (sym >= 30) return Z_DATA;
            dist = dbase[sym] + bits(dext[sym]);
            if (err) return err;
            from = (wpos - dist) & (WSIZE - 1);  /* (a distance past the start reads zeros: harmless) */
            while (len--) {
                window[wpos++] = window[from];
                from = (from + 1) & (WSIZE - 1);
                if (wpos == WSIZE && flush()) return err;
            }
        }
    }
}

static struct huff lcode, dcode;
static int fixed_built;
static struct huff flcode, fdcode;

static int dynamic(void)
{
    static const unsigned char order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    unsigned char lengths[320];
    int nlen, ndist, ncode, i, sym, r;
    nlen = bits(5) + 257;
    ndist = bits(5) + 1;
    ncode = bits(4) + 4;
    if (err) return err;
    if (nlen > 286 || ndist > 30) return Z_DATA;
    memset(lengths, 0, sizeof lengths);
    for (i = 0; i < ncode; i++) lengths[order[i]] = bits(3);
    if ((r = build(&lcode, lengths, 19)) != 0) return r;
    for (i = 0; i < nlen + ndist; ) {
        sym = decode(&lcode);
        if (err) return err;
        if (sym < 16)
            lengths[i++] = sym;
        else {
            int rep, val = 0;
            if (sym == 16) {
                if (i == 0) return Z_DATA;
                val = lengths[i - 1];
                rep = 3 + bits(2);
            } else if (sym == 17)
                rep = 3 + bits(3);
            else
                rep = 11 + bits(7);
            if (i + rep > nlen + ndist) return Z_DATA;
            while (rep--) lengths[i++] = val;
        }
    }
    if (lengths[256] == 0) return Z_DATA;   /* no end-of-block code */
    if ((r = build(&lcode, lengths, nlen)) != 0) return r;
    if ((r = build(&dcode, lengths + nlen, ndist)) != 0) return r;
    return codes(&lcode, &dcode);
}

static int fixed(void)
{
    if (!fixed_built) {
        unsigned char l[288];
        int i;
        for (i = 0; i < 144; i++) l[i] = 8;
        for (; i < 256; i++) l[i] = 9;
        for (; i < 280; i++) l[i] = 7;
        for (; i < 288; i++) l[i] = 8;
        build(&flcode, l, 288);
        for (i = 0; i < 30; i++) l[i] = 5;
        build(&fdcode, l, 30);
        fixed_built = 1;
    }
    return codes(&flcode, &fdcode);
}

static int stored(void)
{
    unsigned len, nlen;
    bitbuf >>= bitcnt & 7;                  /* to a byte boundary */
    bitcnt &= ~7u;
    len = bits(16);
    nlen = bits(16);
    if (err) return err;
    if (len != (~nlen & 0xFFFF)) return Z_DATA;
    while (len--) {
        window[wpos++] = bits(8);
        if (err) return err;
        if (wpos == WSIZE && flush()) return err;
    }
    return 0;
}

int inflate_raw(zin_fn in, void *ictx, zout_fn out, void *octx)
{
    int last, type, r;
    in_ = in; ictx_ = ictx; out_ = out; octx_ = octx;
    bitbuf = bitcnt = padded = 0;
    wpos = 0;
    err = 0;
    do {
        last = bits(1);
        type = bits(2);
        if (err) return err;
        if (type == 0) r = stored();
        else if (type == 1) r = fixed();
        else if (type == 2) r = dynamic();
        else r = Z_DATA;
        if (r) return r;
        if (err) return err;
    } while (!last);
    if (flush()) return err;
    return 0;
}

/* the whole bytes read past the end of the data (call after inflate_raw):
   their count, copied to rest (at most 4) */
int inflate_rest(unsigned char *rest)
{
    int n = 0;
    bitbuf >>= bitcnt & 7;
    bitcnt &= ~7u;
    while (bitcnt > padded) {
        rest[n++] = bitbuf & 0xFF;
        bitbuf >>= 8;
        bitcnt -= 8;
    }
    return n;
}
