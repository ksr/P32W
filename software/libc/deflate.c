/* deflate.c -- P32W C library (D25): Deflate compression (RFC 1951).
 *
 * The method of zlib, smaller: a 64 KiB sliding window (32 KiB of history,
 * slid down when it fills), 3-byte hash chains, lazy matching, and blocks of
 * up to 16 K symbols, each sent as whichever is shortest of stored, fixed
 * Huffman and dynamic Huffman. The level sets how hard matches are looked
 * for (the chain length and when a match is good enough). */
#include <deflate.h>
#include <string.h>

#define WSIZE 32768
#define WMASK (WSIZE - 1)
#define MIN_MATCH 3
#define MAX_MATCH 258
#define MIN_LOOKAHEAD (MAX_MATCH + MIN_MATCH + 1)
#define MAX_DIST (WSIZE - MIN_LOOKAHEAD)
#define HBITS 13
#define HSIZE (1 << HBITS)
#define HSHIFT 5
#define LITBUF 16384
#define TOO_FAR 4096                    /* a 3-byte match farther than this is not worth it */

static unsigned char win[2 * WSIZE + MAX_MATCH];
static unsigned short head[HSIZE], prev[WSIZE];
static unsigned strstart, lookahead, match_start, ins_h;
static int block_start, eof;

static unsigned short sym_dist[LITBUF];  /* 0: a literal */
static unsigned char sym_lc[LITBUF];     /* the literal, or the length - 3 */
static unsigned nsym;
static unsigned lfreq[286], dfreq[30];

static unsigned max_chain, max_lazy, nice_match;

static zread_fn read_;
static void *ictx_;
static zout_fn out_;
static void *octx_;
static int err;

static unsigned char obuf[4096];
static unsigned opos, bitbuf, bitcnt;

static const unsigned short lbase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const unsigned char lext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const unsigned short dbase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const unsigned char dext[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
static unsigned char lcode_of[256];     /* length - 3 -> its code (0..28) */
static unsigned char dcode_of[512];     /* distance - 1 (< 256), or 256 + (distance - 1) / 128 */

static int dcode(unsigned dist)
{
    dist--;
    return dist < 256 ? dcode_of[dist] : dcode_of[256 + (dist >> 7)];
}

static void tables(void)
{
    unsigned i, c;
    if (lcode_of[255]) return;
    for (i = 0; i < 256; i++) {
        for (c = 28; lbase[c] > i + 3; c--) ;
        lcode_of[i] = c;
    }
    for (i = 0; i < 512; i++) {
        unsigned d = i < 256 ? i + 1 : ((i - 256) << 7) + 1;
        for (c = 29; dbase[c] > d; c--) ;
        dcode_of[i] = c;
    }
}

/* ---------------------------------------------------------------- output bits */
static void outbyte(unsigned c)
{
    obuf[opos++] = c;
    if (opos == sizeof obuf) {
        if (!err && out_(octx_, obuf, opos) < 0) err = Z_IO;
        opos = 0;
    }
}

static void send(unsigned value, unsigned n)   /* n <= 16 bits, LSB first */
{
    bitbuf |= value << bitcnt;
    bitcnt += n;
    while (bitcnt >= 8) {
        outbyte(bitbuf & 0xFF);
        bitbuf >>= 8;
        bitcnt -= 8;
    }
}

static void align(void)
{
    if (bitcnt) outbyte(bitbuf & 0xFF);
    bitbuf = bitcnt = 0;
}

/* ---------------------------------------------------------------- Huffman codes */
/* code lengths for freq[0..n-1], none longer than maxbits */
static void lengths(const unsigned *freq, int n, unsigned char *len, int maxbits)
{
    static unsigned f[2 * 288];
    static short parent[2 * 288], heap[288];
    unsigned scaled[288];
    int i, nh, nodes, used, maxlen;
    for (i = 0; i < n; i++) scaled[i] = freq[i];
    for (;;) {
        memset(len, 0, n);
        nh = 0;
        for (i = 0; i < n; i++)
            if (scaled[i]) {
                int k = nh++;           /* push i: a min-heap on f */
                f[i] = scaled[i];
                while (k > 0 && f[heap[(k - 1) / 2]] > f[i]) { heap[k] = heap[(k - 1) / 2]; k = (k - 1) / 2; }
                heap[k] = i;
            }
        used = nh;
        if (used == 0) return;
        if (used == 1) { len[heap[0]] = 1; return; }
        nodes = n;
        while (nh > 1) {
            int a, b, k, x;
            for (k = 0; k < 2; k++) {   /* pop the two smallest */
                int top = heap[0], j = 0;
                x = heap[--nh];
                for (;;) {
                    int c = 2 * j + 1;
                    if (c >= nh) break;
                    if (c + 1 < nh && f[heap[c + 1]] < f[heap[c]]) c++;
                    if (f[heap[c]] >= f[x]) break;
                    heap[j] = heap[c];
                    j = c;
                }
                heap[j] = x;
                if (k == 0) a = top; else b = top;
            }
            f[nodes] = f[a] + f[b];
            parent[a] = parent[b] = nodes;
            k = nh++;                   /* push the new node */
            while (k > 0 && f[heap[(k - 1) / 2]] > f[nodes]) { heap[k] = heap[(k - 1) / 2]; k = (k - 1) / 2; }
            heap[k] = nodes;
            nodes++;
        }
        parent[nodes - 1] = -1;
        maxlen = 0;
        for (i = 0; i < n; i++)
            if (scaled[i]) {
                int d = 0, p;
                for (p = i; parent[p] >= 0; p = parent[p]) d++;
                len[i] = d;
                if (d > maxlen) maxlen = d;
            }
        if (maxlen <= maxbits) return;
        for (i = 0; i < n; i++)         /* too long: flatten the frequencies and again */
            if (scaled[i]) scaled[i] = (scaled[i] + 1) / 2;
    }
}

/* canonical codes for len[0..n-1], bit-reversed for sending LSB first */
static void codes(const unsigned char *len, int n, unsigned short *code)
{
    unsigned short next[16];
    unsigned short count[16];
    unsigned c = 0;
    int i, b;
    memset(count, 0, sizeof count);
    for (i = 0; i < n; i++) count[len[i]]++;
    count[0] = 0;
    for (b = 1; b < 16; b++) {
        c = (c + count[b - 1]) << 1;
        next[b] = c;
    }
    for (i = 0; i < n; i++) {
        unsigned l = len[i], v, r = 0, k;
        if (!l) continue;
        v = next[l]++;
        for (k = 0; k < l; k++) r |= ((v >> k) & 1) << (l - 1 - k);
        code[i] = r;
    }
}

/* ---------------------------------------------------------------- blocks */
static unsigned char llen[286], dlen[30], fllen[288], fdlen[30];
static unsigned short lcodes[286], dcodes[30], flcodes[288], fdcodes[30];

static void send_symbols(const unsigned short *lc, const unsigned char *ll,
                         const unsigned short *dc, const unsigned char *dl)
{
    unsigned i;
    for (i = 0; i < nsym; i++) {
        unsigned d = sym_dist[i], v = sym_lc[i];
        if (!d)
            send(lc[v], ll[v]);
        else {
            int c = lcode_of[v], e;
            send(lc[257 + c], ll[257 + c]);
            if (lext[c]) send(v + 3 - lbase[c], lext[c]);
            e = dcode(d);
            send(dc[e], dl[e]);
            if (dext[e]) send(d - dbase[e], dext[e]);
        }
    }
    send(lc[256], ll[256]);
}

static void stored_block(const unsigned char *p, unsigned n, int last)
{
    do {                                /* (stored blocks hold at most 65535 bytes) */
        unsigned k = n > 65535 ? 65535 : n, i;
        send((last && k == n) ? 1 : 0, 1);
        send(0, 2);
        align();
        outbyte(k & 0xFF); outbyte(k >> 8);
        outbyte(~k & 0xFF); outbyte((~k >> 8) & 0xFF);
        for (i = 0; i < k; i++) outbyte(p[i]);
        p += k;
        n -= k;
    } while (n);
}

static void flush_block(int last)
{
    static const unsigned char order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    unsigned char all[286 + 30], clen[19];
    unsigned short ccodes[19];
    unsigned cfreq[19], rle[316], i, nl, nd, nc, nrle = 0, k;
    unsigned long dyn = 0, fix = 0, sto;
    int stored_len = (int)strstart - block_start;
    lfreq[256] = 1;
    {   /* at least two codes in each tree (one-code trees trouble some decoders) */
        unsigned used = 0;
        for (i = 0; i < 30; i++) used += dfreq[i] != 0;
        if (used < 2) { if (!dfreq[0]) dfreq[0] = 1; else dfreq[1] = 1; }
        if (used == 0) dfreq[1] = 1;
        used = 0;
        for (i = 0; i < 286; i++) used += lfreq[i] != 0;
        if (used < 2) lfreq[0] = lfreq[0] ? lfreq[0] : 1;
    }
    lengths(lfreq, 286, llen, 15);
    lengths(dfreq, 30, dlen, 15);
    for (nl = 286; nl > 257 && !llen[nl - 1]; nl--) ;
    for (nd = 30; nd > 1 && !dlen[nd - 1]; nd--) ;
    /* the code lengths, run-length coded (16: repeat the last 3-6 times, 17 / 18: zeros) */
    memcpy(all, llen, nl);
    memcpy(all + nl, dlen, nd);
    memset(cfreq, 0, sizeof cfreq);
    for (i = 0; i < nl + nd; ) {
        unsigned v = all[i], run = 1;
        while (i + run < nl + nd && all[i + run] == v) run++;
        if (v == 0 && run >= 3) {
            k = run > 138 ? 138 : run;
            if (k >= 11) { rle[nrle++] = 18 | (k - 11) << 8; cfreq[18]++; }
            else { rle[nrle++] = 17 | (k - 3) << 8; cfreq[17]++; }
            i += k;
        } else if (v != 0 && run >= 4) {
            rle[nrle++] = v; cfreq[v]++;
            k = run - 1 > 6 ? 6 : run - 1;
            rle[nrle++] = 16 | (k - 3) << 8; cfreq[16]++;
            i += 1 + k;
        } else {
            rle[nrle++] = v; cfreq[v]++;
            i++;
        }
    }
    lengths(cfreq, 19, clen, 7);
    for (nc = 19; nc > 4 && !clen[order[nc - 1]]; nc--) ;
    /* sizes in bits: dynamic, fixed, stored */
    dyn = 5 + 5 + 4 + 3 * nc;
    for (i = 0; i < nrle; i++) {
        unsigned s = rle[i] & 0xFF;
        dyn += clen[s] + (s == 16 ? 2 : s == 17 ? 3 : s == 18 ? 7 : 0);
    }
    for (i = 0; i < 286; i++)
        if (lfreq[i]) {
            unsigned x = i > 256 ? lext[i - 257] : 0;
            dyn += (unsigned long)lfreq[i] * (llen[i] + x);
            fix += (unsigned long)lfreq[i] * ((i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8) + x);
        }
    for (i = 0; i < 30; i++)
        if (dfreq[i]) {
            dyn += (unsigned long)dfreq[i] * (dlen[i] + dext[i]);
            fix += (unsigned long)dfreq[i] * (5 + dext[i]);
        }
    sto = block_start >= 0 ? (unsigned long)(stored_len + 5 * (stored_len / 65535 + 1)) * 8 + 7 : (unsigned long)-1;
    if (sto <= dyn && sto <= fix) {
        stored_block(win + block_start, stored_len, last);
    } else if (fix <= dyn) {
        if (!fllen[0]) {
            for (i = 0; i < 288; i++) fllen[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
            for (i = 0; i < 30; i++) fdlen[i] = 5;
            codes(fllen, 288, flcodes);
            codes(fdlen, 30, fdcodes);
        }
        send(last, 1);
        send(1, 2);
        send_symbols(flcodes, fllen, fdcodes, fdlen);
    } else {
        codes(llen, 286, lcodes);
        codes(dlen, 30, dcodes);
        codes(clen, 19, ccodes);
        send(last, 1);
        send(2, 2);
        send(nl - 257, 5);
        send(nd - 1, 5);
        send(nc - 4, 4);
        for (i = 0; i < nc; i++) send(clen[order[i]], 3);
        for (i = 0; i < nrle; i++) {
            unsigned s = rle[i] & 0xFF, x = rle[i] >> 8;
            send(ccodes[s], clen[s]);
            if (s == 16) send(x, 2);
            else if (s == 17) send(x, 3);
            else if (s == 18) send(x, 7);
        }
        send_symbols(lcodes, llen, dcodes, dlen);
    }
    block_start = strstart;
    nsym = 0;
    memset(lfreq, 0, sizeof lfreq);
    memset(dfreq, 0, sizeof dfreq);
}

static void tally(unsigned dist, unsigned lc)  /* dist 0: a literal lc; else a match of length lc + 3 */
{
    sym_dist[nsym] = dist;
    sym_lc[nsym] = lc;
    nsym++;
    if (!dist)
        lfreq[lc]++;
    else {
        lfreq[257 + lcode_of[lc]]++;
        dfreq[dcode(dist)]++;
    }
}

/* ---------------------------------------------------------------- matching */
static void fill(void)
{
    unsigned more, i;
    int n;
    if (strstart >= WSIZE + MAX_DIST) {     /* slide the upper half down */
        memcpy(win, win + WSIZE, WSIZE);
        match_start -= WSIZE;
        strstart -= WSIZE;
        block_start -= WSIZE;
        for (i = 0; i < HSIZE; i++) head[i] = head[i] >= WSIZE ? head[i] - WSIZE : 0;
        for (i = 0; i < WSIZE; i++) prev[i] = prev[i] >= WSIZE ? prev[i] - WSIZE : 0;
    }
    while (!eof && lookahead < MIN_LOOKAHEAD) {
        more = 2 * WSIZE - lookahead - strstart;
        if (!more) break;
        n = read_(ictx_, win + strstart + lookahead, more);
        if (n < 0) { err = Z_IO; eof = 1; break; }
        if (n == 0) { eof = 1; break; }
        lookahead += n;
    }
}

static unsigned insert(unsigned s)
{
    ins_h = ((ins_h << HSHIFT) ^ win[s + MIN_MATCH - 1]) & (HSIZE - 1);
    prev[s & WMASK] = head[ins_h];
    head[ins_h] = s;
    return prev[s & WMASK];
}

static unsigned longest(unsigned cur, unsigned best)
{
    unsigned chain = max_chain, limit = strstart > MAX_DIST ? strstart - MAX_DIST : 0;
    unsigned nice = nice_match < lookahead ? nice_match : lookahead;
    unsigned char *scan = win + strstart;
    unsigned maxlen = lookahead < MAX_MATCH ? lookahead : MAX_MATCH;
    if (best >= maxlen) return best;
    do {
        unsigned char *m = win + cur;
        unsigned len;
        if (m[best] != scan[best] || m[0] != scan[0] || m[1] != scan[1]) continue;
        for (len = 2; len < maxlen && m[len] == scan[len]; len++) ;
        if (len > best) {
            match_start = cur;
            best = len;
            if (len >= nice) break;
        }
    } while ((cur = prev[cur & WMASK]) > limit && --chain != 0);
    return best;
}

int deflate_raw(zread_fn read, void *ictx, zout_fn out, void *octx, int level)
{
    static const unsigned short cfg[10][3] = {      /* chain, lazy, nice */
        { 0, 0, 0 }, { 4, 4, 8 }, { 8, 5, 16 }, { 16, 6, 32 }, { 16, 16, 32 },
        { 32, 32, 64 }, { 64, 64, 128 }, { 128, 128, 128 }, { 512, 258, 258 }, { 2048, 258, 258 } };
    unsigned match_length = MIN_MATCH - 1, prev_length, prev_match;
    int match_available = 0;
    read_ = read; ictx_ = ictx; out_ = out; octx_ = octx;
    err = 0; eof = 0;
    opos = bitbuf = bitcnt = 0;
    strstart = lookahead = match_start = ins_h = 0;
    block_start = 0;
    nsym = 0;
    memset(lfreq, 0, sizeof lfreq);
    memset(dfreq, 0, sizeof dfreq);
    memset(head, 0, sizeof head);
    tables();
    if (level < 0 || level > 9) level = 6;
    if (level == 0) {                   /* stored: the input in 64 KiB pieces */
        int n, last = 0;
        do {
            unsigned have = 0;
            while (have < 65535 && (n = read(ictx, win + have, 65535 - have)) > 0) have += n;
            if (n < 0) return Z_IO;
            last = have < 65535;
            stored_block(win, have, last);
        } while (!last && !err);
        align();
        if (!err && opos && out(octx, obuf, opos) < 0) err = Z_IO;
        return err;
    }
    max_chain = cfg[level][0];
    max_lazy = cfg[level][1];
    nice_match = cfg[level][2];
    fill();
    if (lookahead >= 2) ins_h = ((win[0] << HSHIFT) ^ win[1]) & (HSIZE - 1);
    for (;;) {
        unsigned hash_head = 0;
        if (lookahead < MIN_LOOKAHEAD) {
            fill();
            if (lookahead == 0) break;
        }
        if (lookahead >= MIN_MATCH) hash_head = insert(strstart);
        prev_length = match_length;
        prev_match = match_start;
        match_length = MIN_MATCH - 1;
        if (hash_head && prev_length < max_lazy && strstart - hash_head <= MAX_DIST) {
            match_length = longest(hash_head, prev_length);
            if (match_length == MIN_MATCH && strstart - match_start > TOO_FAR) match_length = MIN_MATCH - 1;
            if (match_length > lookahead) match_length = lookahead;
        }
        if (prev_length >= MIN_MATCH && match_length <= prev_length) {
            unsigned max_insert = strstart + lookahead - MIN_MATCH;
            tally(strstart - 1 - prev_match, prev_length - MIN_MATCH);
            lookahead -= prev_length - 1;
            prev_length -= 2;
            do {
                if (++strstart <= max_insert) insert(strstart);
            } while (--prev_length != 0);
            match_available = 0;
            match_length = MIN_MATCH - 1;
            strstart++;
            if (nsym == LITBUF) flush_block(0);
        } else if (match_available) {
            tally(0, win[strstart - 1]);
            if (nsym == LITBUF) flush_block(0);
            strstart++;
            lookahead--;
        } else {
            match_available = 1;
            strstart++;
            lookahead--;
        }
        if (err) return err;
    }
    if (match_available) tally(0, win[strstart - 1]);
    flush_block(1);
    align();
    if (!err && opos && out(octx, obuf, opos) < 0) err = Z_IO;
    return err;
}
