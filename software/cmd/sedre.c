/* sedre.c -- regular expressions for sed: POSIX basic (BRE) and extended
 * (ERE) syntax, compiled to a small program and run by a backtracking
 * matcher.
 *
 * The pattern is parsed into a tree (alternation, concatenation, repetition,
 * groups, atoms), which is then compiled to instructions: CHAR, ANY, SET,
 * the anchors, BREF, SAVE (a group's start or end), SPLIT (try x, then y),
 * JMP and MATCH. A repetition {m,n} is compiled as m copies of its operand
 * followed by n-m optional ones (or a loop when n is unbounded); a loop
 * whose body can match the empty string carries a MARK/CHKP pair that stops
 * an iteration matching nothing, so loops always end.
 *
 * The matcher keeps its own stack (no recursion, so long lines are safe).
 * Unless the pattern has back-references, it also remembers each
 * (instruction, position) state already tried: that state cannot lead to a
 * match the first visit did not find, so the search is linear in the line's
 * length, and it can afford to explore every path, giving the POSIX
 * leftmost-longest match (a|ab matches all of "ab"). With back-references
 * the first match found (greedy, left alternative first) is taken. */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <setjmp.h>
#include "sed.h"

/* tree nodes; the first ten are also instruction codes */
enum { N_CHAR, N_ANY, N_SET, N_BOL, N_EOL, N_WB, N_NWB, N_WBEG, N_WEND, N_BREF,
       N_GROUP, N_CAT, N_ALT, N_REP, N_EMPTY };
enum { O_SAVE = 10, O_SPLIT, O_JMP, O_MARK, O_CHKP, O_MATCH };

struct node { int t, x, y, l, r; };
struct inst { int op, x, y; };
struct re {
    struct inst *prog;
    int n;                      /* instructions */
    unsigned char *sets;        /* 32 bytes (256 bits) per set */
    int nsub, nmark, bref, icase;
    int first;                  /* the character every match starts with, or -1 */
    int *mark;
};

/* the compiler's state */
static const char *P;           /* the pattern, as far as it is parsed */
static int ext, icase, depth, nsub, closed, hasbref;
static struct node *nd;
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
    nd = grow(nd, &ndcap, nnd + 1, sizeof(struct node));
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
    if (icase && isalpha(c)) {
        c = isupper(c) ? tolower(c) : toupper(c);
        sets[s * 32 + (c >> 3)] |= 1 << (c & 7);
    }
}

static int isword(int c)
{
    return isalnum(c) || c == '_';
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

/* one member of a bracket expression: c, [.c.], [=c=], \n, \t or \\ */
static int brchar(void)
{
    int c = (unsigned char)*P;
    if (c == '[' && (P[1] == '.' || P[1] == '=') && P[2] && P[3] == P[1] && P[4] == ']') {
        c = (unsigned char)P[2];
        P += 5;
        return c;
    }
    if (c == '\\' && (P[1] == 'n' || P[1] == 't' || P[1] == '\\')) {
        c = P[1] == 'n' ? '\n' : P[1] == 't' ? '\t' : '\\';
        P += 2;
        return c;
    }
    P++;
    return c;
}

/* [...] or [^...], P just after the [ */
static int bracket(void)
{
    int s = newset(), neg = 0, first = 1, lo, hi, c, k;
    if (*P == '^') { neg = 1; P++; }
    for (;;) {
        if (!*P) fail("Unmatched [, [^, [:, [., or [=");
        if (*P == ']' && !first) { P++; break; }
        first = 0;
        if (*P == '[' && P[1] == ':') {
            const char *e = strstr(P + 2, ":]");
            if (!e) fail("Unmatched [, [^, [:, [., or [=");
            for (k = 0; cnames[k]; k++)
                if ((int)strlen(cnames[k]) == e - P - 2 && !strncmp(cnames[k], P + 2, e - P - 2))
                    break;
            if (!cnames[k]) fail("Invalid character class name");
            for (c = 1; c < 128; c++)
                if (inclass(k, c)) setbit(s, c);
            P = e + 2;
            continue;
        }
        lo = hi = brchar();
        if (*P == '-' && P[1] && P[1] != ']') {
            P++;
            hi = brchar();
            if (hi < lo) fail("Invalid range end");
        }
        for (c = lo; c <= hi; c++) setbit(s, c);
    }
    if (neg)
        for (c = 0; c < 32; c++) sets[s * 32 + c] ^= 0xff;
    return s;
}

static int isalt(void) { return ext ? *P == '|' : P[0] == '\\' && P[1] == '|'; }
static int isclose(void) { return ext ? *P == ')' : P[0] == '\\' && P[1] == ')'; }

static int alt(void);

static int atom(int start)
{
    int c = (unsigned char)*P, a, n, s, i;
    if (ext ? c == '(' : c == '\\' && P[1] == '(') {
        P += ext ? 1 : 2;
        n = ++nsub;
        depth++;
        a = alt();
        depth--;
        if (!isclose()) fail("Unmatched ( or \\(");
        P += ext ? 1 : 2;
        if (n < RE_NSUB) closed |= 1 << n;
        return mk(N_GROUP, n < RE_NSUB ? n : 0, 0, a, -1);
    }
    if (isclose()) fail("Unmatched ) or \\)");
    P++;
    switch (c) {
    case '.':
        return mk(N_ANY, 0, 0, -1, -1);
    case '[':
        return mk(N_SET, bracket(), 0, -1, -1);
    case '^':
        if (ext || start) return mk(N_BOL, 0, 0, -1, -1);
        break;
    case '$':
        if (ext || !*P || (P[0] == '\\' && ((P[1] == ')' && depth) || P[1] == '|')))
            return mk(N_EOL, 0, 0, -1, -1);
        break;
    case '\\':
        c = (unsigned char)*P++;
        if (!c) fail("Trailing backslash");
        if (c >= '1' && c <= '9') {
            if (!(closed & 1 << (c - '0'))) fail("Invalid back reference");
            hasbref = 1;
            return mk(N_BREF, c - '0', 0, -1, -1);
        }
        switch (c) {
        case 'n': c = '\n'; break;
        case 't': c = '\t'; break;
        case 'w': case 'W': case 's': case 'S':
            s = newset();
            for (i = 1; i < 256; i++)
                if ((c == 'w' || c == 'W') ? isword(i) : isspace(i))
                    sets[s * 32 + (i >> 3)] |= 1 << (i & 7);
            if (c == 'W' || c == 'S')
                for (i = 0; i < 32; i++) sets[s * 32 + i] ^= 0xff;
            return mk(N_SET, s, 0, -1, -1);
        case '<': return mk(N_WBEG, 0, 0, -1, -1);
        case '>': return mk(N_WEND, 0, 0, -1, -1);
        case 'b': return mk(N_WB, 0, 0, -1, -1);
        case 'B': return mk(N_NWB, 0, 0, -1, -1);
        case '`': return mk(N_BOL, 0, 0, -1, -1);
        case '\'': return mk(N_EOL, 0, 0, -1, -1);
        }
        break;
    }
    return mk(N_CHAR, c, 0, -1, -1);
}

static int num(void)
{
    int n = 0;
    while (isdigit(*P)) {
        n = n * 10 + *P++ - '0';
        if (n > 255) fail("Invalid content of \\{\\}");
    }
    return n;
}

static int piece(int start)
{
    int a = atom(start), m, n;
    if (!ext && nd[a].t == N_BOL) return a;     /* ^* : the * is literal */
    for (;;) {
        if (*P == '*') {
            P++;
            m = 0; n = -1;
        } else if (ext ? (*P == '+' || *P == '?') : (P[0] == '\\' && (P[1] == '+' || P[1] == '?'))) {
            if (!ext) P++;
            m = *P == '+';
            n = *P == '+' ? -1 : 1;
            P++;
        } else if (ext ? *P == '{' : (P[0] == '\\' && P[1] == '{')) {
            P += ext ? 1 : 2;
            if (isdigit(*P)) m = num();
            else if (*P == ',') m = 0;
            else fail("Invalid content of \\{\\}");
            n = m;
            if (*P == ',') {
                P++;
                n = isdigit(*P) ? num() : -1;
            }
            if (ext ? *P != '}' : (P[0] != '\\' || P[1] != '}')) fail("Unmatched \\{");
            P += ext ? 1 : 2;
            if (n >= 0 && n < m) fail("Invalid content of \\{\\}");
        } else
            return a;
        a = mk(N_REP, m, n, a, -1);
    }
}

static int cat(void)
{
    int l = -1, a, start = 1;
    while (*P && !isalt() && !(depth && isclose())) {
        a = piece(start);
        start = !ext && nd[a].t == N_BOL;
        l = l < 0 ? a : mk(N_CAT, 0, 0, l, a);
    }
    return l < 0 ? mk(N_EMPTY, 0, 0, -1, -1) : l;
}

static int alt(void)
{
    int l = cat(), r;
    while (isalt()) {
        P += ext ? 1 : 2;
        r = cat();
        l = mk(N_ALT, 0, 0, l, r);
    }
    return l;
}

/* can node i match the empty string? */
static int nullable(int i)
{
    switch (nd[i].t) {
    case N_CHAR: case N_ANY: case N_SET:
        return 0;
    case N_GROUP:
        return nullable(nd[i].l);
    case N_CAT:
        return nullable(nd[i].l) && nullable(nd[i].r);
    case N_ALT:
        return nullable(nd[i].l) || nullable(nd[i].r);
    case N_REP:
        return nd[i].x == 0 || nullable(nd[i].l);
    }
    return 1;
}

static int emit(int op, int x, int y)
{
    if (npg >= 20000) fail("Regular expression too big");
    pg = grow(pg, &pgcap, npg + 1, sizeof(struct inst));
    pg[npg].op = op; pg[npg].x = x; pg[npg].y = y;
    return npg++;
}

static void gen(int i)
{
    int t = nd[i].t, x = nd[i].x, y = nd[i].y, l = nd[i].l, k, j, s, nl, mk_;
    int fix[256];
    switch (t) {
    case N_CHAR:
        if (icase && isalpha(x)) {
            s = newset();
            setbit(s, x);
            emit(N_SET, s, 0);
        } else
            emit(N_CHAR, x, 0);
        break;
    case N_EMPTY:
        break;
    case N_GROUP:
        if (x) emit(O_SAVE, 2 * x, 0);
        gen(l);
        if (x) emit(O_SAVE, 2 * x + 1, 0);
        break;
    case N_CAT:
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

Re *recomp(const char *pat, int flags, char **err)
{
    Re *re;
    int root;
    P = pat;
    ext = flags & RE_EXT;
    icase = (flags & RE_ICASE) != 0;
    depth = nsub = closed = hasbref = nmark = 0;
    nnd = npg = nsets = 0;
    if (setjmp(jb)) {
        *err = errmsg;
        return 0;
    }
    root = alt();
    gen(root);
    emit(O_MATCH, 0, 0);
    re = malloc(sizeof(Re));
    if (!re) fail("out of memory");
    re->prog = malloc(npg * sizeof(struct inst));
    re->sets = malloc(nsets * 32 + 1);
    re->mark = malloc((nmark + 1) * sizeof(int));
    if (!re->prog || !re->sets || !re->mark) fail("out of memory");
    memcpy(re->prog, pg, npg * sizeof(struct inst));
    memcpy(re->sets, sets, nsets * 32);
    re->n = npg;
    re->nsub = nsub < RE_NSUB ? nsub : RE_NSUB - 1;
    re->nmark = nmark;
    re->bref = hasbref;
    re->icase = icase;
    re->first = pg[0].op == N_CHAR ? pg[0].x : -1;
    return re;
}

int rensub(Re *re)
{
    return re->nsub;
}

/* the matcher */
struct bt { int pc, pos; };     /* pc < 0: restore slot -1-pc to pos */
static struct bt *stk;
static int nstk, stkcap;
static unsigned char *vis;      /* states tried: bit pos * n + pc */
static long viscap;

static void push(int pc, int pos)
{
    if (nstk >= stkcap) {
        stkcap = stkcap * 2 + 64;
        stk = realloc(stk, stkcap * sizeof(struct bt));
        if (!stk) { stkcap = 0; nstk = 0; return; }
    }
    stk[nstk].pc = pc;
    stk[nstk].pos = pos;
    nstk++;
}

int reexec(Re *re, const char *s, int len, int start, int *sub)
{
    struct inst *ip;
    int cap[2 * RE_NSUB], best[2 * RE_NSUB];
    int st, pc, pos, slot, i, a, k, w1, w2, memo, n = re->n, hi = start, bestend = -1;
    long bit, nbytes = ((long)n * (len + 1) + 7) / 8;
    unsigned char *u = (unsigned char *)s;
    const char *f;

    memo = !re->bref && nbytes <= 1024L * 1024;
    if (memo && nbytes > viscap) {
        free(vis);
        vis = calloc(nbytes, 1);
        viscap = vis ? nbytes : 0;
        if (!vis) memo = 0;
    }
    for (st = start; st <= len; st++) {
        if (re->prog[0].op == N_BOL && st > 0) break;
        if (re->first >= 0) {
            f = st < len ? memchr(s + st, re->first, len - st) : 0;
            if (!f) break;
            st = f - s;
        }
        for (i = 0; i < 2 * RE_NSUB; i++) cap[i] = -1;
        nstk = 0;
        push(0, st);
        while (nstk > 0) {
            nstk--;
            pc = stk[nstk].pc;
            pos = stk[nstk].pos;
            if (pc < 0) {
                slot = -1 - pc;
                if (slot < 2 * RE_NSUB) cap[slot] = pos;
                else re->mark[slot - 2 * RE_NSUB] = pos;
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
                case N_CHAR:
                    if (pos < len && u[pos] == ip->x) { pos++; pc++; continue; }
                    goto fail;
                case N_ANY:
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
                case N_WB: case N_NWB: case N_WBEG: case N_WEND:
                    w1 = pos > 0 && isword(u[pos - 1]);
                    w2 = pos < len && isword(u[pos]);
                    if (ip->op == N_WB ? w1 == w2 : ip->op == N_NWB ? w1 != w2 :
                        ip->op == N_WBEG ? (w1 || !w2) : (!w1 || w2))
                        goto fail;
                    pc++;
                    continue;
                case N_BREF:
                    a = cap[2 * ip->x];
                    k = cap[2 * ip->x + 1] - a;
                    if (a < 0 || k < 0 || pos + k > len) goto fail;
                    for (i = 0; i < k; i++)
                        if (re->icase ? tolower(u[a + i]) != tolower(u[pos + i]) : u[a + i] != u[pos + i])
                            goto fail;
                    pos += k;
                    pc++;
                    continue;
                case O_SAVE:
                    push(-1 - ip->x, cap[ip->x]);
                    cap[ip->x] = pos;
                    pc++;
                    continue;
                case O_MARK:
                    push(-1 - 2 * RE_NSUB - ip->x, re->mark[ip->x]);
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
                if (pos > bestend) {
                    bestend = pos;
                    memcpy(best, cap, sizeof cap);
                }
                if (!memo || pos == len) goto done;
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
        memset(vis + b0, 0, b1 - b0);
    }
    if (bestend < 0) return 0;
    for (i = 2; i < 2 * RE_NSUB; i++) sub[i] = best[i];
    sub[0] = st;
    sub[1] = bestend;
    return 1;
}
