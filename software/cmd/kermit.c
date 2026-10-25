/* kermit -- file transfer over a serial line, the Kermit protocol (D25).
 *
 *   kermit -s file ...      send files
 *   kermit -r               receive files
 *   kermit -x               be a server: the other Kermit sends, gets
 *                           (wildcards * ?), lists (remote dir), changes
 *                           directory, deletes, types, and finishes
 *   kermit -g name ...      get files from a Kermit server
 *   kermit -f               tell a Kermit server to finish
 *   options: -a name (send / receive as), -T (text: CRLF on the line),
 *            -e length (longest packet, 1000), -w window (3), -l device
 *            (another line than this terminal), -q (quiet)
 *
 * Runs on the terminal it was started from (the Mac's C-Kermit, connected
 * to that line, does the other side), in raw mode. Protocol: long packets
 * (up to 9024), sliding windows (up to 31), CRC-16 / 2- / 1-character
 * checks, control and 8th-bit prefixing as negotiated, repeat counts, and
 * attribute packets: binary or text, the exact size, the modification time.
 * Files arrive in binary unless the sender says text (then CRLF becomes LF).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <utime.h>
#include <dirent.h>
#include <sgtty.h>
#include <sys/stat.h>

#define SOH 1
#define MAXLONG 9024
#define MAXWIN 31
#define tochar(x) ((x) + 32)
#define unchar(x) ((x) - 32)
#define ctl(x) ((x) ^ 64)

/* ---------------------------------------------------------------- settings */
static int lfd = 0, ofd = 1;            /* the line */
static int quiet, text_mode;
static int want_len = 1000, want_win = 3;
static char *as_name;

/* negotiated */
static int chk = 1;                     /* block check type in use: 1, 2, 3 */
static int our_chk = 3;
static int spsiz = 80;                  /* longest packet we may send (the other's receive size) */
static int window = 1;
static int qctl = '#', qbin = 0, rept = 0;
static int attrs;                       /* both sides do attribute packets */
static int timeout_s = 7;

static int seq;                         /* next sequence number */
static struct sgttyb saved;
static int saved_ok;

static void restore(void)
{
    if (saved_ok) stty(lfd, &saved);
    saved_ok = 0;
}

static void msg(const char *fmt, const char *a)
{
    if (quiet || lfd == 0) return;      /* (on our own terminal, messages would corrupt the line) */
    fprintf(stderr, fmt, a);
    fputc('\n', stderr);
}

/* ---------------------------------------------------------------- the line */
static unsigned char ibuf[2048];
static int ipos, ilen;
static volatile int timed_out;

static void on_alarm(int sig)
{
    signal(SIGALRM, on_alarm);          /* (V7: a caught signal reverts to its default) */
    (void)sig;
    timed_out = 1;
}

static int getbyte(void)                /* -1: timeout */
{
    if (ipos >= ilen) {
        int n;
        timed_out = 0;
        alarm(timeout_s);
        n = read(lfd, ibuf, sizeof ibuf);
        alarm(0);
        if (n <= 0) return -1;
        ilen = n;
        ipos = 0;
    }
    return ibuf[ipos++];
}

static void flushin(void)
{
    ipos = ilen = 0;
}

static void put(const unsigned char *p, int n)
{
    while (n > 0) {
        int k = write(ofd, p, n);
        if (k <= 0) return;
        p += k;
        n -= k;
    }
}

/* ---------------------------------------------------------------- packets */
static unsigned crc16(const unsigned char *p, int n)    /* Kermit's CRC-CCITT */
{
    unsigned crc = 0, c, q;
    while (n--) {
        c = *p++;
        q = (crc ^ c) & 15;
        crc = (crc >> 4) ^ (q * 010201);
        q = (crc ^ (c >> 4)) & 15;
        crc = (crc >> 4) ^ (q * 010201);
    }
    return crc;
}

static int chklen(int t) { return t == 3 ? 3 : t == 2 ? 2 : 1; }

static int addcheck(unsigned char *dst, const unsigned char *p, int n, int t)
{
    unsigned s = 0;
    int i;
    if (t == 3) {
        unsigned c = crc16(p, n);
        dst[0] = tochar((c >> 12) & 15);
        dst[1] = tochar((c >> 6) & 63);
        dst[2] = tochar(c & 63);
        return 3;
    }
    for (i = 0; i < n; i++) s += p[i];
    if (t == 2) {
        dst[0] = tochar((s >> 6) & 63);
        dst[1] = tochar(s & 63);
        return 2;
    }
    dst[0] = tochar((s + ((s & 192) >> 6)) & 63);
    return 1;
}

/* a packet (data already encoded) to the line */
static void spack(int type, int n, const unsigned char *data, int len)
{
    static unsigned char p[MAXLONG + 32];
    int k = 0, t = (type == 'S' || type == 'I') ? 1 : chk;
    int cl = chklen(t);
    p[k++] = SOH;
    if (len + 2 + cl <= 94) {
        p[k++] = tochar(len + 2 + cl);
        p[k++] = tochar(n & 63);
        p[k++] = type;
    } else {                            /* a long packet: an extended header with its own check */
        unsigned s, total = len + cl;
        p[k++] = tochar(0);
        p[k++] = tochar(n & 63);
        p[k++] = type;
        p[k++] = tochar(total / 95);
        p[k++] = tochar(total % 95);
        s = p[1] + p[2] + p[3] + p[4] + p[5];
        p[k++] = tochar((s + ((s & 192) >> 6)) & 63);
    }
    memcpy(p + k, data, len);
    k += len;
    k += addcheck(p + k, p + 1, k - 1, t);
    p[k++] = '\r';
    put(p, k);
}

static unsigned char rdata[MAXLONG + 8];
static int rlen;

/* the next packet: its type ('T' timeout, 'Q' damaged); its sequence number
   in *n. The block check type is found by trying the expected one first, then
   the others (a server waiting for a command, or a NAK sent with a plain
   check, cannot be known in advance). */
static int rpack(int *n, int check_type)
{
    static unsigned char buf[MAXLONG + 16];
    int c, len, type, i, k, total, t, tries;
    int order[4];
    for (;;) {
        do {
            if ((c = getbyte()) < 0) return 'T';
        } while (c != SOH);
    again:
        if ((c = getbyte()) < 0) return 'T';
        if (c == SOH) goto again;
        buf[0] = c;
        len = unchar(c);
        if ((c = getbyte()) < 0) return 'T';
        if (c == SOH) goto again;
        buf[1] = c;
        if ((type = getbyte()) < 0) return 'T';
        if (type == SOH) goto again;
        buf[2] = type;
        if (len == 0) {                 /* long: an extended header with its own check */
            unsigned sum;
            for (i = 3; i < 6; i++) {
                if ((c = getbyte()) < 0) return 'T';
                if (c == SOH) goto again;
                buf[i] = c;
            }
            sum = buf[0] + buf[1] + buf[2] + buf[3] + buf[4];
            if (buf[5] != tochar((sum + ((sum & 192) >> 6)) & 63)) return 'Q';
            total = unchar(buf[3]) * 95 + unchar(buf[4]);  /* data + check */
            k = 6;
        } else {
            if (len < 3) return 'Q';
            total = len - 2;
            k = 3;
        }
        if (total < 1 || total > MAXLONG + 3) return 'Q';
        for (i = 0; i < total; i++) {
            if ((c = getbyte()) < 0) return 'T';
            if (c == SOH) goto again;
            buf[k + i] = c;
        }
        *n = unchar(buf[1]);
        order[0] = (type == 'S' || type == 'I') ? 1 : check_type;
        order[1] = 1; order[2] = 3; order[3] = 2;
        for (tries = 0; tries < 4; tries++) {
            unsigned char ck[3];
            int cl;
            t = order[tries];
            cl = chklen(t);
            if (total < cl) continue;
            addcheck(ck, buf, k + total - cl, t);
            if (!memcmp(ck, buf + k + total - cl, cl)) {
                rlen = total - cl;
                memcpy(rdata, buf + k, rlen);
                rdata[rlen] = 0;
                return type;
            }
        }
        return 'Q';
    }
}

/* ---------------------------------------------------------------- data encoding */
static int enc1(unsigned char *d, int c)
{
    int k = 0, c7 = c & 0x7F;
    if (qbin && (c & 0x80)) { d[k++] = qbin; c = c7; }
    if (c7 < 32 || c7 == 127) { d[k++] = qctl; c = ctl(c); }
    else if (c7 == qctl || (qbin && c7 == qbin) || (rept && c7 == rept)) d[k++] = qctl;
    d[k++] = c;
    return k;
}

/* src[0..n) into dst (at most room bytes); returns how much of src it took */
static int encode(const unsigned char *src, int n, unsigned char *dst, int room, int *used)
{
    int i = 0, k = 0;
    while (i < n) {
        unsigned char tmp[8];
        int run = 1, e;
        while (rept && i + run < n && src[i + run] == src[i] && run < 94) run++;
        if (rept && run >= 3) {
            tmp[0] = rept; tmp[1] = tochar(run);
            e = 2 + enc1(tmp + 2, src[i]);
        } else {
            run = 1;
            e = enc1(tmp, src[i]);
        }
        if (k + e > room) break;
        memcpy(dst + k, tmp, e);
        k += e;
        i += run;
    }
    *used = k;
    return i;
}

static int decode(const unsigned char *s, int n, unsigned char *dst)
{
    int i = 0, k = 0;
    while (i < n) {
        int c = s[i++], r = 1, b8 = 0;
        if (rept && c == rept && i < n) { r = unchar(s[i++]); c = s[i++]; }
        if (qbin && c == qbin && i < n) { b8 = 0x80; c = s[i++]; }
        if (c == qctl && i < n) {
            c = s[i++];
            if ((c & 0x7F) >= 0x3F && (c & 0x7F) <= 0x5F) c = ctl(c);
        }
        c |= b8;
        while (r-- > 0) dst[k++] = c;
    }
    return k;
}

/* a string as plain packet data (names, messages): encoded */
static int encstr(const char *s, unsigned char *d)
{
    int used;
    encode((const unsigned char *)s, strlen(s), d, 2000, &used);
    return used;
}

/* ---------------------------------------------------------------- send-init */
static int init_data(unsigned char *d)
{
    int capas = 0x02 | 0x04 | 0x08;     /* long packets, windows, attributes */
    d[0] = tochar(94);                  /* MAXL (short) */
    d[1] = tochar(timeout_s);
    d[2] = tochar(0);                   /* NPAD */
    d[3] = ctl(0);                      /* PADC */
    d[4] = tochar('\r');                /* EOL */
    d[5] = '#';                         /* QCTL */
    d[6] = 'Y';                         /* QBIN: on request (the line is 8-bit) */
    d[7] = '0' + our_chk;
    d[8] = '~';                         /* REPT */
    d[9] = tochar(capas);
    d[10] = tochar(want_win);
    d[11] = tochar(want_len / 95);
    d[12] = tochar(want_len % 95);
    return 13;
}

/* what they sent in S / I or its ACK; sets the agreed parameters */
static void take_init(const unsigned char *d, int n)
{
    int capas = 0, k, their_q8 = 'N', their_chk = '1', their_rept = ' ';
    spsiz = n > 0 ? unchar(d[0]) : 80;
    if (n > 1 && unchar(d[1]) > 0) timeout_s = unchar(d[1]) + 2;
    if (n > 5) qctl = d[5];
    if (n > 6) their_q8 = d[6];
    if (n > 7) their_chk = d[7];
    if (n > 8) their_rept = d[8];
    qbin = (their_q8 > 32 && their_q8 < 63) || (their_q8 > 95 && their_q8 < 127) ? their_q8 : 0;
    chk = (their_chk - '0' == our_chk) ? our_chk : 1;
    rept = their_rept == '~' ? '~' : 0;
    window = 1;
    attrs = 0;
    if (n > 9) {
        capas = unchar(d[9]);
        for (k = 9; k < n && (unchar(d[k]) & 1); k++) ;   /* (more CAPAS bytes) */
        attrs = (capas & 0x08) != 0;
        if ((capas & 0x04) && k + 1 < n) {
            window = unchar(d[k + 1]);
            if (window > want_win) window = want_win;
            if (window < 1) window = 1;
        }
        if ((capas & 0x02) && k + 3 < n) {
            int l = unchar(d[k + 2]) * 95 + unchar(d[k + 3]);
            spsiz = l ? l : 500;
            if (spsiz > want_len) spsiz = want_len;
        }
    }
    if (spsiz < 20) spsiz = 20;
    if (spsiz > MAXLONG) spsiz = MAXLONG;
}

/* ---------------------------------------------------------------- stop-and-wait exchange */
static int retries = 10;

/* send a packet and wait for its ACK (the ACK's data in rdata); 0, or -1 */
static int exchange(int type, const unsigned char *d, int len)
{
    int tries, n, t;
    for (tries = 0; tries < retries; tries++) {
        spack(type, seq, d, len);
        for (;;) {
            t = rpack(&n, chk);
            if (t == 'Y' && n == (seq & 63)) { seq = (seq + 1) & 63; return 0; }
            if (t == 'N' && n == ((seq + 1) & 63)) { seq = (seq + 1) & 63; rlen = 0; return 0; }
            if (t == 'E') { msg("kermit: the other side: %s", (char *)rdata); return -1; }
            if (t == 'T' || t == 'N' || t == 'Q') break;    /* send it again */
            /* (an old ACK: keep listening) */
        }
    }
    msg("kermit: %s", "too many retries");
    return -1;
}

static void error_pkt(const char *s)
{
    unsigned char d[200];
    spack('E', seq, d, encstr(s, d));
}

static void ack(int n, const unsigned char *d, int len)
{
    spack('Y', n, d, len);
}

/* ---------------------------------------------------------------- sending */
static int sfd;
static int s_eof;
static int s_cr;                        /* (text mode: a pending CR before LF) */

static int s_read(unsigned char *p, int n)
{
    int k = 0;
    if (!text_mode) {
        int r = read(sfd, p, n);
        return r < 0 ? 0 : r;
    }
    while (k < n) {                     /* LF -> CR LF */
        unsigned char c;
        if (s_cr) { p[k++] = '\n'; s_cr = 0; continue; }
        if (read(sfd, &c, 1) != 1) break;
        if (c == '\n') { p[k++] = '\r'; s_cr = 1; }
        else p[k++] = c;
    }
    return k;
}

/* the next D packet's encoded data; 0 at the end of the file */
static int next_data(unsigned char *d, int room)
{
    static unsigned char raw[MAXLONG];
    static int rpos, rlen2;
    int n = 0;
    if (s_eof == 2) { rpos = rlen2 = 0; s_eof = 0; }   /* (a new file) */
    while (n < room - 8) {
        int used, took;
        if (rpos >= rlen2) {
            if (s_eof) break;
            rlen2 = s_read(raw, sizeof raw);
            rpos = 0;
            if (rlen2 <= 0) { s_eof = 1; break; }
        }
        took = encode(raw + rpos, rlen2 - rpos, d + n, room - n, &used);
        if (!took) break;
        rpos += took;
        n += used;
    }
    return n;
}

struct slot { int seq, len, acked, tries; unsigned char d[MAXLONG]; };
static struct slot *slots;

/* the file's data in D packets, windowed; 0, -1 error, 1 cancelled by the receiver */
static int send_data(void)
{
    int room = spsiz - (spsiz > 94 ? 7 : 3) - chklen(chk), base = 0, nsent = 0, done = 0, w = window, i;
    if (!slots && !(slots = malloc(sizeof(struct slot) * (MAXWIN + 1)))) return -1;
    if (room > MAXLONG - 16) room = MAXLONG - 16;
    s_eof = 2;
    for (;;) {
        int t, n;
        /* fill the window */
        while (!done && nsent - base < w) {
            struct slot *s = &slots[nsent % (MAXWIN + 1)];
            s->len = next_data(s->d, room);
            if (s->len == 0) { done = 1; break; }
            s->seq = (seq + nsent) & 63;
            s->acked = 0;
            s->tries = 0;
            spack('D', s->seq, s->d, s->len);
            nsent++;
        }
        if (base == nsent && done) break;
        t = rpack(&n, chk);
        if (t == 'Y' || t == 'N') {
            int off = (n - (seq + base)) & 63;      /* relative to the window's base */
            if (t == 'Y' && off < nsent - base) {
                slots[(base + off) % (MAXWIN + 1)].acked = 1;
                if (rlen > 0 && (rdata[0] == 'X' || rdata[0] == 'Z')) { seq = (seq + nsent) & 63; return 1; }
            } else if (t == 'N' && off < nsent - base) {
                struct slot *s = &slots[(base + off) % (MAXWIN + 1)];
                if (++s->tries > retries) return -1;
                spack('D', s->seq, s->d, s->len);
            } else if (t == 'N' && off == nsent - base) {
                /* a NAK for the next one: everything before it arrived (their ACKs were lost) */
                for (i = base; i < nsent; i++) slots[i % (MAXWIN + 1)].acked = 1;
            } else if (t == 'N' && base < nsent) {
                /* a NAK we cannot place: send the oldest unacknowledged again */
                struct slot *s = &slots[base % (MAXWIN + 1)];
                if (++s->tries > retries) return -1;
                spack('D', s->seq, s->d, s->len);
            }
            while (base < nsent && slots[base % (MAXWIN + 1)].acked) base++;
        } else if (t == 'E') {
            msg("kermit: the other side: %s", (char *)rdata);
            return -1;
        } else if (t == 'T' || t == 'Q') {
            struct slot *s = &slots[base % (MAXWIN + 1)];     /* the oldest unacknowledged, again */
            if (base < nsent) {
                if (++s->tries > retries) return -1;
                spack('D', s->seq, s->d, s->len);
            }
        }
    }
    seq = (seq + nsent) & 63;
    return 0;
}

static int send_init(int type)
{
    unsigned char d[32];
    int n = init_data(d), tries, t, k;
    seq = 0;
    for (tries = 0; tries < retries; tries++) {
        spack(type, 0, d, n);
        t = rpack(&k, 1);
        if (t == 'Y' && k == 0) {
            take_init(rdata, rlen);
            seq = 1;
            return 0;
        }
        if (t == 'E') { msg("kermit: the other side: %s", (char *)rdata); return -1; }
    }
    return -1;
}

static int attr_packet(struct stat *st)
{
    unsigned char a[128];
    char v[40];
    int k = 0;
    struct tm *tm;
    time_t t = st->st_mtime;
    a[k++] = '"';                       /* type */
    if (text_mode) { a[k++] = tochar(3); memcpy(a + k, "AMJ", 3); k += 3; }
    else { a[k++] = tochar(2); memcpy(a + k, "B8", 2); k += 2; }
    sprintf(v, "%u", (unsigned)(st->st_size + 1023) / 1024);
    a[k++] = '!'; a[k++] = tochar(strlen(v)); memcpy(a + k, v, strlen(v)); k += strlen(v);
    sprintf(v, "%u", (unsigned)st->st_size);
    a[k++] = '1'; a[k++] = tochar(strlen(v)); memcpy(a + k, v, strlen(v)); k += strlen(v);
    tm = localtime(&t);                 /* (Kermit dates are local time) */
    sprintf(v, "%04d%02d%02d %02d:%02d:%02d", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
            tm->tm_hour, tm->tm_min, tm->tm_sec);
    a[k++] = '#'; a[k++] = tochar(strlen(v)); memcpy(a + k, v, strlen(v)); k += strlen(v);
    a[k++] = '.'; a[k++] = tochar(2); a[k++] = 'U'; a[k++] = '1';
    if (exchange('A', a, k) < 0) return -1;
    return rlen > 0 && rdata[0] == 'N' ? 1 : 0;     /* 1: refused */
}

/* one file (after S); 0, -1 */
static int send_file(const char *path, const char *as)
{
    unsigned char d[300];
    struct stat st;
    const char *name = as ? as : strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
    int r;
    if (stat(path, &st) < 0 || !S_ISREG(st.st_mode) || (sfd = open(path, O_RDONLY)) < 0) {
        msg("kermit: %s: cannot send", path);
        return 0;
    }
    if (exchange('F', d, encstr(name, d)) < 0) { close(sfd); return -1; }
    if (attrs && (r = attr_packet(&st)) != 0) {
        close(sfd);
        if (r < 0) return -1;
        msg("kermit: %s: refused", path);
        return exchange('Z', (const unsigned char *)"D", 1);
    }
    s_cr = 0;
    r = send_data();
    close(sfd);
    if (r < 0) return -1;
    if (exchange('Z', (const unsigned char *)(r ? "D" : ""), r ? 1 : 0) < 0) return -1;
    msg("sent %s", path);
    return 0;
}

static int send_files(char **files, int n, int type_x, const char *text)
{
    int i;
    if (send_init('S') < 0) return -1;
    if (type_x) {                       /* a text reply to a server command: X header, then the text */
        unsigned char d[300];
        if (exchange('X', d, encstr("", d)) < 0) return -1;
        {
            int pos = 0, len = strlen(text), room = spsiz - (spsiz > 94 ? 7 : 3) - chklen(chk);
            static unsigned char pk[MAXLONG];
            while (pos < len) {
                int used, took = encode((const unsigned char *)text + pos, len - pos, pk, room, &used);
                if (exchange('D', pk, used) < 0) return -1;
                pos += took;
            }
        }
        if (exchange('Z', 0, 0) < 0) return -1;
    } else
        for (i = 0; i < n; i++)
            if (send_file(files[i], n == 1 ? as_name : 0) < 0) return -1;
    return exchange('B', 0, 0);
}

/* ---------------------------------------------------------------- receiving */
static int rfd = -1;
static char rname[300];
static unsigned rmtime;
static int rtext, r_cr;

static void r_write(const unsigned char *p, int n)
{
    if (!rtext) { if (write(rfd, p, n) != n) {} return; }
    {
        unsigned char out[MAXLONG + 2];
        int i, k = 0;
        for (i = 0; i < n; i++) {       /* CR LF -> LF */
            if (r_cr) { if (p[i] != '\n') out[k++] = '\r'; r_cr = 0; }
            if (p[i] == '\r') { r_cr = 1; continue; }
            out[k++] = p[i];
        }
        if (k && write(rfd, out, k) != k) {}
    }
}

static void attributes(const unsigned char *a, int n)
{
    int i = 0;
    while (i + 2 <= n) {
        int tag = a[i], len = unchar(a[i + 1]);
        const unsigned char *v = a + i + 2;
        if (i + 2 + len > n) break;
        if (tag == '"' && len > 0) rtext = v[0] == 'A';
        if (tag == '#' && len >= 8) {   /* [yy]yymmdd[ hh:mm[:ss]] */
            struct tm tm;
            char b[32];
            int y, off = 0;
            memset(&tm, 0, sizeof tm);
            memcpy(b, v, len < 31 ? len : 31);
            b[len < 31 ? len : 31] = 0;
            if (len != 6 && b[6] != ' ') {     /* yyyymmdd, else yymmdd */
                y = (b[0] - '0') * 1000 + (b[1] - '0') * 100 + (b[2] - '0') * 10 + (b[3] - '0');
                off = 4;
            } else {
                y = 1900 + (b[0] - '0') * 10 + (b[1] - '0');
                off = 2;
            }
            tm.tm_year = y - 1900;
            tm.tm_mon = (b[off] - '0') * 10 + (b[off + 1] - '0') - 1;
            tm.tm_mday = (b[off + 2] - '0') * 10 + (b[off + 3] - '0');
            if ((int)strlen(b) >= off + 9) {
                tm.tm_hour = atoi(b + off + 5);
                tm.tm_min = atoi(b + off + 8);
                if ((int)strlen(b) >= off + 12) tm.tm_sec = atoi(b + off + 11);
            }
            rmtime = mktime(&tm);
        }
        i += 2 + len;
    }
}

static int safe_name(char *name)        /* the last component; all-uppercase names lowercased */
{
    char *s = strrchr(name, '/'), *p;
    int upper = 1;
    if (s) memmove(name, s + 1, strlen(s));
    if (!name[0] || !strcmp(name, ".") || !strcmp(name, "..")) return -1;
    for (p = name; *p; p++) if (*p >= 'a' && *p <= 'z') upper = 0;
    if (upper) for (p = name; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 32;
    if (strlen(name) > 28) name[28] = 0;    /* (P32W's names are at most 28 characters) */
    return 0;
}

struct rslot { int have, len; unsigned char d[MAXLONG]; };
static struct rslot *rslots;

/* after S was acknowledged: files until B; 0, -1 */
static int receive(void)
{
    int expect = seq, t, n, i, tries = 0;
    unsigned char dec[MAXLONG * 2 + 8];
    if (!rslots && !(rslots = malloc(sizeof(struct rslot) * (MAXWIN + 1)))) return -1;
    for (i = 0; i <= MAXWIN; i++) rslots[i].have = 0;
    for (;;) {
        t = rpack(&n, chk);
        if (t == 'T' || t == 'Q') {
            if (++tries > retries) { error_pkt("too many retries"); return -1; }
            spack('N', expect, 0, 0);
            continue;
        }
        tries = 0;
        if (t == 'E') { msg("kermit: the other side: %s", (char *)rdata); if (rfd >= 0) { close(rfd); unlink(rname); rfd = -1; } return -1; }
        if (n != expect && t != 'D') {  /* an old packet again: its ACK was lost */
            if (((expect - n) & 63) < 32) ack(n, 0, 0);
            continue;
        }
        switch (t) {
        case 'F': {
            int k = decode(rdata, rlen, dec);
            dec[k] = 0;
            strcpy(rname, as_name ? as_name : (char *)dec);
            if (safe_name(rname) < 0 || (rfd = open(rname, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0) {
                error_pkt("cannot create the file");
                return -1;
            }
            rtext = text_mode;
            r_cr = 0;
            rmtime = 0;
            {
                unsigned char d[300];
                ack(n, d, encstr(rname, d));
            }
            expect = (expect + 1) & 63;
            break;
        }
        case 'A':
            attributes(rdata, rlen);
            ack(n, (const unsigned char *)"Y", 1);
            expect = (expect + 1) & 63;
            break;
        case 'X': {                     /* text to show (a server's reply) */
            rfd = 1;
            ack(n, 0, 0);
            expect = (expect + 1) & 63;
            break;
        }
        case 'D': {
            int off = (n - expect) & 63;
            if (off >= 32) { ack(n, 0, 0); break; }         /* old: acknowledged again */
            if (off >= window) break;   /* (beyond the window: ignored) */
            if (off == 0) {
                int k = decode(rdata, rlen, dec);
                if (rfd >= 0) r_write(dec, k);
                ack(n, 0, 0);
                expect = (expect + 1) & 63;
                while (rslots[expect % (MAXWIN + 1)].have) {    /* the ones that came early */
                    struct rslot *s = &rslots[expect % (MAXWIN + 1)];
                    k = decode(s->d, s->len, dec);
                    if (rfd >= 0) r_write(dec, k);
                    s->have = 0;
                    expect = (expect + 1) & 63;
                }
            } else if (off < window) {
                struct rslot *s = &rslots[n % (MAXWIN + 1)];
                if (!s->have) { memcpy(s->d, rdata, rlen); s->len = rlen; s->have = 1; }
                ack(n, 0, 0);
                spack('N', expect, 0, 0);       /* the one missing */
            }
            break;
        }
        case 'Z':
            if (rfd > 1) {
                if (rtext && r_cr) { unsigned char cr = '\r'; if (write(rfd, &cr, 1) != 1) {} }
                close(rfd);
                if (rlen > 0 && rdata[0] == 'D') unlink(rname);     /* discarded by the sender */
                else {
                    if (rmtime) { struct utimbuf ut; ut.actime = ut.modtime = rmtime; utime(rname, &ut); }
                    msg("received %s", rname);
                }
            }
            rfd = -1;
            ack(n, 0, 0);
            expect = (expect + 1) & 63;
            break;
        case 'B':
            ack(n, 0, 0);
            seq = (expect + 1) & 63;
            return 0;
        default:
            error_pkt("unexpected packet");
            return -1;
        }
    }
}

/* wait for S (or, as a server, anything), then ack it; returns the type seen */
static int answer_init(const unsigned char *d, int n)
{
    unsigned char ours[32];
    int k;
    int agreed;
    take_init(d, n);
    k = init_data(ours);
    ours[10] = tochar(window);          /* (the window we agreed to) */
    agreed = chk;
    chk = 1;                            /* the ACK to S / I has the plain check */
    spack('Y', 0, ours, k);
    chk = agreed;
    seq = 1;
    return 0;
}

static int receive_files(void)
{
    int t, n, tries;
    chk = 1;
    for (tries = 0; tries < 20; tries++) {
        t = rpack(&n, 1);
        if (t == 'S') {
            answer_init(rdata, rlen);
            return receive();
        }
        if (t == 'E') { msg("kermit: the other side: %s", (char *)rdata); return -1; }
        if (t == 'T') spack('N', 0, 0, 0);
    }
    return -1;
}

/* ---------------------------------------------------------------- the server */
static int match(const char *p, const char *s)  /* * and ? */
{
    if (!*p) return !*s;
    if (*p == '*') return match(p + 1, s) || (*s && match(p, s + 1));
    if (*s && (*p == '?' || *p == *s)) return match(p + 1, s + 1);
    return 0;
}

static int server_get(const char *pattern)
{
    static char *list[256];
    static char names[256][32];
    int n = 0;
    if (strchr(pattern, '*') || strchr(pattern, '?')) {
        char dir[256], *slash;
        const char *pat = pattern;
        DIR *d;
        struct dirent *e;
        strcpy(dir, ".");
        if ((slash = strrchr(pattern, '/')) != 0) {
            memcpy(dir, pattern, slash - pattern);
            dir[slash - pattern] = 0;
            pat = slash + 1;
        }
        if ((d = opendir(dir)) != 0) {
            while ((e = readdir(d)) != 0 && n < 256) {
                struct stat st;
                char path[300];
                if (e->d_name[0] == '.' || !match(pat, e->d_name)) continue;
                sprintf(path, "%s/%s", dir, e->d_name);
                if (stat(path, &st) < 0 || !S_ISREG(st.st_mode)) continue;
                if (strlen(path) >= 32) continue;
                strcpy(names[n], path);
                list[n] = names[n];
                n++;
            }
            closedir(d);
        }
    } else if (access(pattern, 4) == 0 && strlen(pattern) < 32) {
        strcpy(names[0], pattern);
        list[n++] = names[0];
    }
    if (!n) { error_pkt("no such file"); return 0; }
    return send_files(list, n, 0, 0);
}

static void server_text(int n, const char *text)       /* a reply: short in the ACK, else sent as text */
{
    unsigned char d[400];
    if (strlen(text) < 60) {
        ack(n, d, encstr(text, d));
        return;
    }
    send_files(0, 0, 1, text);
}

static char *listing(const char *dir)
{
    static char out[16384];
    DIR *d = opendir(dir[0] ? dir : ".");
    struct dirent *e;
    int k = 0;
    out[0] = 0;
    if (!d) return "no such directory\n";
    while ((e = readdir(d)) != 0 && k < (int)sizeof out - 80) {
        struct stat st;
        char path[300];
        if (e->d_name[0] == '.') continue;
        sprintf(path, "%s/%s", dir[0] ? dir : ".", e->d_name);
        if (stat(path, &st) < 0) continue;
        k += sprintf(out + k, "%c %9u  %s\r\n", S_ISDIR(st.st_mode) ? 'd' : '-', (unsigned)st.st_size, e->d_name);
    }
    closedir(d);
    return out;
}

static int serve(void)
{
    for (;;) {
        int t, n, k;
        unsigned char dec[2048];
        chk = 1;
        timeout_s = 3600;               /* (a server waits as long as it takes) */
        seq = 0;
        t = rpack(&n, 1);
        timeout_s = 7;
        if (t == 'T' || t == 'Q') { if (t == 'Q') spack('N', 0, 0, 0); continue; }
        switch (t) {
        case 'I':
            answer_init(rdata, rlen);
            break;
        case 'S':
            answer_init(rdata, rlen);
            receive();
            break;
        case 'R':
            k = decode(rdata, rlen, dec);
            dec[k] = 0;
            server_get((char *)dec);
            break;
        case 'G': {
            char arg[300];
            k = decode(rdata, rlen, dec);
            dec[k] = 0;
            arg[0] = 0;
            if (k > 1) {                /* the first argument: its length, then its text */
                int l = unchar(dec[1]);
                if (l > k - 2) l = k - 2;
                memcpy(arg, dec + 2, l);
                arg[l] = 0;
            }
            switch (dec[0]) {
            case 'F': case 'L':
                ack(n, 0, 0);
                return 0;
            case 'D':
                server_text(n, listing(arg));
                break;
            case 'C':
                if (chdir(arg[0] ? arg : "/") < 0) error_pkt("no such directory");
                else { char m[300]; sprintf(m, "%s", arg[0] ? arg : "/"); server_text(n, m); }
                break;
            case 'E':
                if (unlink(arg) < 0) error_pkt("cannot delete it");
                else server_text(n, "deleted");
                break;
            case 'T': {
                static char buf[16384];
                int fd = open(arg, O_RDONLY), m;
                if (fd < 0) { error_pkt("no such file"); break; }
                m = read(fd, buf, sizeof buf - 1);
                close(fd);
                buf[m > 0 ? m : 0] = 0;
                server_text(n, buf);
                break;
            }
            case 'H':
                server_text(n, "P32W Kermit server: SEND, GET, REMOTE DIR / CD / DELETE / TYPE, FINISH, BYE\r\n"
                               "(long packets, sliding windows, attributes)\r\n");
                break;
            default:
                error_pkt("not implemented");
            }
            break;
        }
        case 'E':
            break;
        default:
            error_pkt("not implemented");
        }
    }
}

/* ---------------------------------------------------------------- a client of a server */
static int generic(char cmd)
{
    unsigned char d[8];
    d[0] = cmd;
    if (send_init('I') < 0) return -1;
    seq = 0;
    return exchange('G', d, 1);
}

static int get(const char *name)
{
    unsigned char d[300];
    int tries, t, n;
    if (send_init('I') < 0) return -1;
    for (tries = 0; tries < retries; tries++) {
        spack('R', 0, d, encstr(name, d));
        t = rpack(&n, 1);
        if (t == 'S') { answer_init(rdata, rlen); return receive(); }
        if (t == 'E') { msg("kermit: the server: %s", (char *)rdata); return -1; }
    }
    return -1;
}

/* ---------------------------------------------------------------- main */
static void stop(int sig)
{
    (void)sig;
    restore();
    _exit(1);
}

int main(int argc, char **argv)
{
    int i, action = 0, r = 0;
    char *line = 0;
    for (i = 1; i < argc && argv[i][0] == '-'; i++) {
        char *o = argv[i] + 1;
        switch (*o) {
        case 's': case 'r': case 'x': case 'g': case 'f': action = *o; break;
        case 'q': quiet = 1; break;
        case 'T': text_mode = 1; break;
        case 'a': if (++i < argc) as_name = argv[i]; break;
        case 'e': if (++i < argc) want_len = atoi(argv[i]); break;
        case 'w': if (++i < argc) want_win = atoi(argv[i]); break;
        case 'l': if (++i < argc) line = argv[i]; break;
        default: action = 0; i = argc; break;
        }
        if (action == 's' || action == 'g') { i++; break; }   /* (the names follow) */
    }
    if (!action) {
        fprintf(stderr, "usage: kermit -s file ... | -r | -x | -g name ... | -f   [-a name] [-T] [-e len] [-w win] [-l line] [-q]\n");
        return 2;
    }
    if (want_len < 40) want_len = 40;
    if (want_len > MAXLONG) want_len = MAXLONG;
    if (want_win < 1) want_win = 1;
    if (want_win > MAXWIN) want_win = MAXWIN;
    if (line) {
        if ((lfd = open(line, O_RDWR)) < 0) { perror(line); return 1; }
        ofd = lfd;
    }
    if (gtty(lfd, &saved) == 0) {
        struct sgttyb raw = saved;
        raw.sg_flags = RAW;
        saved_ok = 1;
        stty(lfd, &raw);
    }
    signal(SIGALRM, on_alarm);
    signal(SIGINT, stop);
    signal(SIGTERM, stop);
    signal(SIGHUP, stop);
    switch (action) {
    case 's':
        if (i >= argc) { restore(); fprintf(stderr, "kermit: -s needs files\n"); return 2; }
        r = send_files(argv + i, argc - i, 0, 0);
        break;
    case 'r': r = receive_files(); break;
    case 'x': r = serve(); break;
    case 'g':
        for (; i < argc && r == 0; i++) r = get(argv[i]);
        break;
    case 'f': r = generic('F'); break;
    }
    restore();
    flushin();
    return r < 0 ? 1 : 0;
}
