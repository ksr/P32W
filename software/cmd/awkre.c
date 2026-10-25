/* awkre.c -- awk's regular expressions (awk.h): POSIX extended syntax
 * (| + ? * {n,m} ( ) [...] with [:class:], . ^ $) and awk's escapes (\n \t
 * \/ \" \\ \ddd ..., any other \c is c itself). Adapted from sed's sedre.c
 * (the same compile-to-instructions design, without groups' positions or
 * back-references, which awk does not have):
 *
 * The pattern is parsed into a tree (alternation, concatenation,
 * repetition, atoms), compiled to instructions CHAR, ANY, SET, BOL, EOL,
 * SPLIT (try x, then y), JMP and MATCH; {m,n} becomes m copies of the
 * operand and n-m optional ones (or a loop). A loop whose body can match
 * the empty string carries a MARK/CHKP pair so that it always ends.
 *
 * The matcher keeps its own stack and remembers each (instruction,
 * position) state already tried, so a search is linear in the text's
 * length and can explore every path: the match found is POSIX's leftmost
 * longest. A pattern that is only literal characters is found with a
 * plain search instead. */
#include "awk.h"

enum { N_CHR, N_ANY_, N_SET, N_BOL, N_EOL, N_GRP, N_CAT_, N_ALT, N_REP, N_EMPTY };
enum { O_SPLIT = 10, O_JMP, O_MARK, O_CHKP, O_MATCH };

struct rnode { int t, x, y, l, r; };
struct inst { int op, x, y; };
struct Re {
    struct inst *prog;
    int n;                      /* instructions */
    unsigned char *sets;        /* 32 bytes (256 bits) per set */
    int nmark;
    int first;                  /* the character every match starts with, or -1 */
    int bol;                    /* every match is at the start */
    char *lit;                  /* only literal characters: them */
    int litlen;
    int *mark;
};

/* the compiler's state */
static const char *P;
static int depth;
static struct rnode *nd;
static int nnd, ndcap;
static struct inst *pg;
static int npg, pgcap;
static unsigned char *sets;
static int nsets, setcap;
static int nmark;
static jmp_buf jb;
static char *errmsg;

static void fail(char *msg)
{
    errmsg = msg;
    longjmp(jb, 1);
}

static void *grow(void *p, int *cap, int need, int size)
{
    if (need <= *cap) return p;
    *cap = need * 2 + 16;
    p = realloc(p, *cap * size);
    if (!p) fail("out of memory");
    return p;
}

static int mk(int t, int x, int y, int l, int r)
{
    nd = grow(nd, &ndcap, nnd + 1, sizeof(struct rnode));
    nd[nnd].t = t; nd[nnd].x = x; nd[nnd].y = y; nd[nnd].l = l; nd[nnd].r = r;
    return nnd++;
}

static int newset(void)
{
    sets = grow(sets, &setcap, (nsets + 1) * 32, 1);
    memset(sets + nsets * 32, 0, 32);
    return nsets++;
}

static void setbit(int s, int c)
{
    c &= 255;
    sets[s * 32 + (c >> 3)] |= 1 << (c & 7);
}

static char *cnames[] = { "alpha", "digit", "alnum", "upper", "lower", "space",
                          "blank", "punct", "print", "graph", "cntrl", "xdigit", 0 };

static int inclass(int k, int c)
{
    switch (k) {
    case 0: return isalpha(c);
    case 1: return isdigit(c);
    case 2: return isalnum(c);
    case 3: return isupper(c);
    case 4: return islower(c);
    case 5: return isspace(c);
    case 6: return c == ' ' || c == '\t';
    case 7: return ispunct(c);
    case 8: return isprint(c);
    case 9: return isgraph(c);
    case 10: return iscntrl(c);
    }
    return isxdigit(c);
}

/* an escape: P just after the backslash */
static int escape(void)
{
    int c = (unsigned char)*P++, n;
    switch (c) {
    case 0: P--; return '\\';
    case 'n': return '\n';
    case 't': return '\t';
    case 'r': return '\r';
    case 'f': return '\f';
    case 'b': return '\b';
    case 'v': return '\v';
    case 'a': return '\007';
    }
    if (c >= '0' && c <= '7') {
        n = c - '0';
        if (*P >= '0' && *P <= '7') { n = n * 8 + *P++ - '0'; if (*P >= '0' && *P <= '7') n = n * 8 + *P++ - '0'; }
        return n & 255;
    }
    return c;
}

/* one member of a bracket expression */
static int brchar(void)
{
    int c = (unsigned char)*P;
    if (c == '[' && (P[1] == '.' || P[1] == '=') && P[2] && P[3] == P[1] && P[4] == ']') {
        c = (unsigned char)P[2];
        P += 5;
        return c;
    }
    P++;
    if (c == '\\') return escape();
    return c;
}

/* [...] or [^...], P just after the [ */
static int bracket(void)
{
    int s = newset(), neg = 0, first = 1, lo, hi, c, k;
    if (*P == '^') { neg = 1; P++; }
    for (;;) {
        if (!*P) fail("unmatched [");
        if (*P == ']' && !first) { P++; break; }
        first = 0;
        if (*P == '[' && P[1] == ':') {
            const char *e = strstr(P + 2, ":]");
            if (!e) fail("unmatched [:");
            for (k = 0; cnames[k]; k++)
                if ((int)strlen(cnames[k]) == e - P - 2 && !strncmp(cnames[k], P + 2, e - P - 2))
                    break;
            if (!cnames[k]) fail("invalid character class");
            for (c = 1; c < 256; c++)
                if (c < 128 && inclass(k, c)) setbit(s, c);
            P = e + 2;
            continue;
        }
        lo = hi = brchar();
        if (*P == '-' && P[1] && P[1] != ']') {
            P++;
            hi = brchar();
            if (hi < lo) fail("invalid range");
        }
        for (c = lo; c <= hi; c++) setbit(s, c);
    }
    if (neg)
        for (c = 0; c < 32; c++) sets[s * 32 + c] ^= 0xff;
    return s;
}

static int alt(void);

static int atom(void)
{
    int c = (unsigned char)*P, a;
    if (c == '(') {
        P++;
        depth++;
        a = alt();
        depth--;
        if (*P != ')') fail("missing )");
        P++;
        return mk(N_GRP, 0, 0, a, -1);
    }
    if (c == ')') fail("unmatched )");
    P++;
    switch (c) {
    case '.': return mk(N_ANY_, 0, 0, -1, -1);
    case '[': return mk(N_SET, bracket(), 0, -1, -1);
    case '^': return mk(N_BOL, 0, 0, -1, -1);
    case '$': return mk(N_EOL, 0, 0, -1, -1);
    case '\\': c = escape(); break;
    }
    return mk(N_CHR, c, 0, -1, -1);
}

static int num(void)
{
    int n = 0;
    while (isdigit((unsigned char)*P)) {
        n = n * 10 + *P++ - '0';
        if (n > 255) fail("repetition count too big");
    }
    return n;
}

/* {m}, {m,}, {m,n} or {,n} at P? */
static int isinterval(void)
{
    const char *q = P + 1;
    if (!isdigit((unsigned char)*q) && !(*q == ',' && isdigit((unsigned char)q[1]))) return 0;
    while (isdigit((unsigned char)*q)) q++;
    if (*q == ',') { q++; while (isdigit((unsigned char)*q)) q++; }
    return *q == '}';
}

static int piece(void)
{
    int a, m, n;
    if (*P == '*' || *P == '+' || *P == '?')    /* (nothing to repeat: literal) */
        a = mk(N_CHR, (unsigned char)*P++, 0, -1, -1);
    else
        a = atom();
    for (;;) {
        if (*P == '*') {
            P++;
            m = 0; n = -1;
        } else if (*P == '+' || *P == '?') {
            m = *P == '+';
            n = *P == '+' ? -1 : 1;
            P++;
        } else if (*P == '{' && isinterval()) {
            P++;
            m = isdigit((unsigned char)*P) ? num() : 0;
            n = m;
            if (*P == ',') {
                P++;
                n = isdigit((unsigned char)*P) ? num() : -1;
            }
            P++;                        /* } */
            if (n >= 0 && n < m) fail("invalid repetition count");
        } else
            return a;
        a = mk(N_REP, m, n, a, -1);
    }
}

static int cat(void)
{
    int l = -1, a;
    while (*P && *P != '|' && !(depth && *P == ')')) {
        a = piece();
        l = l < 0 ? a : mk(N_CAT_, 0, 0, l, a);
    }
    return l < 0 ? mk(N_EMPTY, 0, 0, -1, -1) : l;
}

static int alt(void)
{
    int l = cat(), r;
    while (*P == '|') {
        P++;
        r = cat();
        l = mk(N_ALT, 0, 0, l, r);
    }
    return l;
}

static int nullable(int i)
{
    switch (nd[i].t) {
    case N_CHR: case N_ANY_: case N_SET: return 0;
    case N_GRP: return nullable(nd[i].l);
    case N_CAT_: return nullable(nd[i].l) && nullable(nd[i].r);
    case N_ALT: return nullable(nd[i].l) || nullable(nd[i].r);
    case N_REP: return nd[i].x == 0 || nullable(nd[i].l);
    }
    return 1;
}

static int emit(int op, int x, int y)
{
    if (npg >= 20000) fail("regular expression too big");
    pg = grow(pg, &pgcap, npg + 1, sizeof(struct inst));
    pg[npg].op = op; pg[npg].x = x; pg[npg].y = y;
    return npg++;
}

static void gen(int i)
{
    int t = nd[i].t, x = nd[i].x, y = nd[i].y, l = nd[i].l, k, j, s, nl, mk_;
    int fix[256];
    switch (t) {
    case N_EMPTY:
        break;
    case N_GRP:
        gen(l);
        break;
    case N_CAT_:
        gen(l);
        gen(nd[i].r);
        break;
    case N_ALT:
        s = emit(O_SPLIT, npg + 1, 0);
        gen(l);
        j = emit(O_JMP, 0, 0);
        pg[s].y = npg;
        gen(nd[i].r);
        pg[j].x = npg;
        break;
    case N_REP:
        for (k = 0; k < x; k++) gen(l);
        if (y < 0) {
            nl = nullable(l);
            s = emit(O_SPLIT, npg + 1, 0);
            mk_ = nmark;
            if (nl) emit(O_MARK, nmark++, 0);
            gen(l);
            if (nl) emit(O_CHKP, mk_, 0);
            emit(O_JMP, s, 0);
            pg[s].y = npg;
        } else {
            for (k = x; k < y; k++) {
                fix[k - x] = emit(O_SPLIT, npg + 1, 0);
                gen(l);
            }
            for (k = x; k < y; k++) pg[fix[k - x]].y = npg;
        }
        break;
    default:
        emit(t, x, 0);
    }
}

Re *re_comp(const char *pat, char **err)
{
    static char msg[200];
    Re *re;
    int root, i;
    P = pat;
    depth = nmark = 0;
    nnd = npg = nsets = 0;
    if (setjmp(jb)) {
        sprintf(msg, "syntax error in regular expression %.80s at %.40s", pat, P);
        *err = msg;
        return 0;
    }
    root = alt();
    if (*P) fail("unmatched )");
    gen(root);
    emit(O_MATCH, 0, 0);
    re = emalloc(sizeof(Re));
    re->prog = emalloc(npg * sizeof(struct inst));
    re->sets = emalloc(nsets * 32 + 1);
    re->mark = emalloc((nmark + 1) * sizeof(int));
    memcpy(re->prog, pg, npg * sizeof(struct inst));
    memcpy(re->sets, sets, nsets * 32);
    re->n = npg;
    re->nmark = nmark;
    re->first = pg[0].op == N_CHR ? pg[0].x : -1;
    re->bol = pg[0].op == N_BOL;
    re->lit = 0;
    for (i = 0; i < npg - 1 && pg[i].op == N_CHR; i++) ;
    if (i == npg - 1 && i > 0) {        /* only characters */
        re->lit = emalloc(i + 1);
        for (i = 0; i < npg - 1; i++) re->lit[i] = pg[i].x;
        re->lit[i] = 0;
        re->litlen = i;
    }
    return re;
}

static void re_free(Re *re)
{
    free(re->prog);
    free(re->sets);
    free(re->mark);
    free(re->lit);
    free(re);
}

/* ---------------------------------------------------------------- the matcher */
struct bt { int pc, pos; };     /* pc < 0: restore mark -1-pc to pos */
static struct bt *stk;
static int nstk, stkcap;
static unsigned char *vis;      /* states tried: bit pos * n + pc */
static long viscap;

static void push(int pc, int pos)
{
    if (nstk >= stkcap) {
        stkcap = stkcap * 2 + 64;
        stk = erealloc(stk, stkcap * sizeof(struct bt));
    }
    stk[nstk].pc = pc;
    stk[nstk].pos = pos;
    nstk++;
}

static int litfind(Re *re, const char *s, int len, int start, int *ms, int *me)
{
    const char *p = s + start, *end = s + len - re->litlen;
    int c = (unsigned char)re->lit[0];
    while (p <= end) {
        p = memchr(p, c, end - p + 1);
        if (!p) return 0;
        if (!memcmp(p, re->lit, re->litlen)) {
            *ms = p - s;
            *me = *ms + re->litlen;
            return 1;
        }
        p++;
    }
    return 0;
}

/* the leftmost-longest match in s[start..len) (^ only at 0): *ms, *me;
   any: the first match found will do */
static int rmatch(Re *re, const char *s, int len, int start, int *ms, int *me, int any)
{
    struct inst *ip;
    int st, pc, pos, memo, n = re->n, hi = start, bestend = -1;
    long bit, nbytes = ((long)n * (len + 1) + 7) / 8;
    const unsigned char *u = (const unsigned char *)s;
    const char *f;

    if (re->lit) return litfind(re, s, len, start, ms, me);
    memo = nbytes <= 1024L * 1024;
    if (memo && nbytes > viscap) {
        free(vis);
        vis = calloc(nbytes, 1);
        viscap = vis ? nbytes : 0;
        if (!vis) memo = 0;
    }
    for (st = start; st <= len; st++) {
        if (re->bol && st > 0) break;
        if (re->first >= 0) {
            f = st < len ? memchr(s + st, re->first, len - st) : 0;
            if (!f) break;
            st = f - s;
        }
        nstk = 0;
        push(0, st);
        while (nstk > 0) {
            nstk--;
            pc = stk[nstk].pc;
            pos = stk[nstk].pos;
            if (pc < 0) {
                re->mark[-1 - pc] = pos;
                continue;
            }
            for (;;) {
                if (memo) {
                    bit = (long)pos * n + pc;
                    if (vis[bit >> 3] & 1 << (bit & 7)) goto fail;
                    vis[bit >> 3] |= 1 << (bit & 7);
                    if (pos > hi) hi = pos;
                }
                ip = &re->prog[pc];
                switch (ip->op) {
                case N_CHR:
                    if (pos < len && u[pos] == ip->x) { pos++; pc++; continue; }
                    goto fail;
                case N_ANY_:
                    if (pos < len) { pos++; pc++; continue; }
                    goto fail;
                case N_SET:
                    if (pos < len && re->sets[ip->x * 32 + (u[pos] >> 3)] & 1 << (u[pos] & 7)) {
                        pos++; pc++; continue;
                    }
                    goto fail;
                case N_BOL:
                    if (pos != 0) goto fail;
                    pc++;
                    continue;
                case N_EOL:
                    if (pos != len) goto fail;
                    pc++;
                    continue;
                case O_MARK:
                    push(-1 - ip->x, re->mark[ip->x]);
                    re->mark[ip->x] = pos;
                    pc++;
                    continue;
                case O_CHKP:
                    if (re->mark[ip->x] == pos) goto fail;
                    pc++;
                    continue;
                case O_SPLIT:
                    push(ip->y, pos);
                    pc = ip->x;
                    continue;
                case O_JMP:
                    pc = ip->x;
                    continue;
                }
                /* O_MATCH */
                if (pos > bestend) bestend = pos;
                if (any || !memo || pos == len) goto done;
                goto fail;
            }
        fail:
            ;
        }
        if (bestend >= 0) break;
    }
done:
    if (memo) {
        long b0 = (long)start * n / 8, b1 = ((long)(hi + 1) * n + 7) / 8;
        if (b1 > viscap) b1 = viscap;
        if (b1 > b0) memset(vis + b0, 0, b1 - b0);
    }
    if (bestend < 0) return 0;
    *ms = st;
    *me = bestend;
    return 1;
}

int re_exec(Re *re, const char *s, int len, int start, int *ms, int *me)
{
    return rmatch(re, s, len, start, ms, me, 0);
}

int re_test(Re *re, const char *s)
{
    int ms, me;
    if (re->lit) return strstr(s, re->lit) != 0;
    return rmatch(re, s, strlen(s), 0, &ms, &me, 1);
}

/* ---------------------------------------------------------------- dynamic regular expressions */
#define NCACHE 16
static struct { char *pat; Re *re; } cache[NCACHE];
static int nextvictim, lasthit;

Re *re_dyn(const char *pat)
{
    int i;
    char *err;
    Re *re;
    if (cache[lasthit].pat && !strcmp(cache[lasthit].pat, pat)) return cache[lasthit].re;
    for (i = 0; i < NCACHE; i++)
        if (cache[i].pat && !strcmp(cache[i].pat, pat)) { lasthit = i; return cache[i].re; }
    re = re_comp(pat, &err);
    if (!re) fatal("%s", err);
    i = nextvictim;
    nextvictim = (nextvictim + 1) % NCACHE;
    if (cache[i].pat) { sfree(cache[i].pat); re_free(cache[i].re); }
    cache[i].pat = sdup(pat);
    cache[i].re = re;
    lasthit = i;
    return re;
}
