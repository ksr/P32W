/* ylr.c -- yacc's automaton: the LR(0) states, their LALR(1) lookaheads
 * and each state's actions (yacc.h has the overview).
 *
 * The LR(0) states are made from item sets, each state named by its kernel
 * (the items after a transition); a closure adds the rules of every
 * nonterminal that can start what follows a dot (FDERIVES, by rule).
 * The lookaheads are DeRemer and Pennello's (1982), exactly LALR(1): for each
 * nonterminal transition (p, A) the terminals that can follow it,
 *   Read(p,A)   = DR(p,A) + Read(r,C) for (p,A) reads (r,C)
 *   Follow(p,A) = Read(p,A) + Follow(p',B) for (p,A) includes (p',B)
 * each computed by digraph() (Tarjan's strongly connected components, so
 * the sets of a cycle come out equal), and a reduction's lookahead set is
 * the union of Follow over its lookbacks. Conflicts are resolved as POSIX
 * yacc says: by the rule's and the token's precedence and associativity if
 * both have one; else shift over reduce, and the earlier rule. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "yacc.h"

struct state **states;
int nstates, finalstate;
int tsize;
unsigned *LA;
int nsr, nrr, nunused;
FILE *vout;
int ngotos;
int *gotomap, *fromst, *tost;

#define SET(s, i)   ((s)[(i) >> 5] |= 1u << ((i) & 31))
#define CLR(s, i)   ((s)[(i) >> 5] &= ~(1u << ((i) & 31)))
#define ISSET(s, i) ((s)[(i) >> 5] >> ((i) & 31) & 1)

static int *dstart, *drules;            /* the rules of each nonterminal */
static char *nullable;                  /* by symbol */
static unsigned *fderives;              /* per nonterminal, a set of rules */
static int rsize;                       /* words in a set of rules */
static int *itemset, nitemset;          /* a closure */
static int statecap;
#define NSHASH 1024
static struct state *shash[NSHASH];

/* ---- the grammar's properties ---- */

static void derives(void)
{
    int i, *n = xmalloc((nnterms + 1) * sizeof *n);
    dstart = xmalloc((nnterms + 1) * sizeof *dstart);
    drules = xmalloc(nrules * sizeof *drules);
    for (i = 0; i < nrules; i++) n[rules[i].lhs - ntokens + 1]++;
    for (i = 0; i < nnterms; i++) n[i + 1] += n[i];
    memcpy(dstart, n, (nnterms + 1) * sizeof *n);
    for (i = 0; i < nrules; i++) drules[n[rules[i].lhs - ntokens]++] = i;
    free(n);
}

static void nullables(void)
{
    int i, j, changed;
    nullable = xmalloc(nsyms);
    do {
        changed = 0;
        for (i = 0; i < nrules; i++) {
            if (nullable[rules[i].lhs]) continue;
            for (j = rules[i].rhs; ritem[j] >= 0 && nullable[ritem[j]]; j++) ;
            if (ritem[j] < 0) { nullable[rules[i].lhs] = 1; changed = 1; }
        }
    } while (changed);
}

/* FIRSTS(A): the nonterminals that can start A (left corners, A too);
 * FDERIVES(A): their rules */
static void firsts(void)
{
    int nw = (nnterms + 31) >> 5, i, j, k, s;
    unsigned *f = xmalloc(nnterms * nw * sizeof *f), *fi, *fj;
    for (i = 0; i < nnterms; i++) {
        fi = f + i * nw;
        SET(fi, i);
        for (j = dstart[i]; j < dstart[i + 1]; j++) {
            s = ritem[rules[drules[j]].rhs];
            if (s >= ntokens) SET(fi, s - ntokens);
        }
    }
    for (j = 0; j < nnterms; j++)               /* Warshall's transitive closure */
        for (i = 0; i < nnterms; i++) {
            fi = f + i * nw;
            if (!ISSET(fi, j)) continue;
            fj = f + j * nw;
            for (k = 0; k < nw; k++) fi[k] |= fj[k];
        }
    rsize = (nrules + 31) >> 5;
    fderives = xmalloc(nnterms * rsize * sizeof *fderives);
    for (i = 0; i < nnterms; i++)
        for (j = 0; j < nnterms; j++)
            if (ISSET(f + i * nw, j))
                for (k = dstart[j]; k < dstart[j + 1]; k++) SET(fderives + i * rsize, drules[k]);
    free(f);
}

/* ---- LR(0) ---- */

/* the closure of a kernel, into itemset (ascending) */
static void closure(int *kern, int nk)
{
    static unsigned *ruleset;
    unsigned *fd, w;
    int i, j, k, s, r, it;
    if (!ruleset) ruleset = xmalloc(rsize * sizeof *ruleset);
    memset(ruleset, 0, rsize * sizeof *ruleset);
    for (i = 0; i < nk; i++) {
        s = ritem[kern[i]];
        if (s >= ntokens) {
            fd = fderives + (s - ntokens) * rsize;
            for (j = 0; j < rsize; j++) ruleset[j] |= fd[j];
        }
    }
    nitemset = 0;
    k = 0;
    for (j = 0; j < rsize; j++) {
        if (!(w = ruleset[j])) continue;
        for (r = j << 5; w; r++, w >>= 1) {
            if (!(w & 1)) continue;
            it = rules[r].rhs;
            while (k < nk && kern[k] < it) itemset[nitemset++] = kern[k++];
            if (k < nk && kern[k] == it) k++;
            itemset[nitemset++] = it;
        }
    }
    while (k < nk) itemset[nitemset++] = kern[k++];
}

/* the state with this kernel, made if new */
static int getstate(int sym, int *kern, int nk)
{
    unsigned h = nk;
    int i;
    struct state *s;
    for (i = 0; i < nk; i++) h = h * 31 + kern[i];
    h %= NSHASH;
    for (s = shash[h]; s; s = s->hnext)
        if (s->nk == nk && !memcmp(s->kern, kern, nk * sizeof *kern)) return s->la;
    s = xmalloc(sizeof *s);
    s->nk = nk;
    s->kern = xmalloc(nk * sizeof *kern);
    memcpy(s->kern, kern, nk * sizeof *kern);
    s->sym = sym;
    s->la = nstates;                    /* (its number, until lalr()) */
    s->hnext = shash[h];
    shash[h] = s;
    if (nstates == statecap) {
        statecap = statecap ? statecap * 2 : 128;
        states = xrealloc(states, statecap * sizeof *states);
    }
    states[nstates++] = s;
    if (nstates > 32000) fatal("too many states", 0);
    return nstates - 1;
}

void lr0(void)
{
    int *count, *base, *kbuf, *syms, nsh, i, j, s, t, n, item0 = 0;
    struct state *st;
    derives();
    nullables();
    firsts();
    itemset = xmalloc(nitems * sizeof *itemset);
    count = xmalloc(nsyms * sizeof *count);
    base = xmalloc(nsyms * sizeof *base);
    kbuf = xmalloc(nitems * sizeof *kbuf);
    syms = xmalloc(nsyms * sizeof *syms);
    getstate(0, &item0, 1);
    for (n = 0; n < nstates; n++) {
        st = states[n];
        closure(st->kern, st->nk);
        /* the transitions, by symbol ascending; the reductions */
        nsh = 0;
        st->nr = 0;
        for (i = 0; i < nitemset; i++) {
            s = ritem[itemset[i]];
            if (s < 0) { st->nr++; continue; }
            if (!count[s]++) syms[nsh++] = s;
        }
        for (i = 1; i < nsh; i++)               /* (few: an insertion sort) */
            for (j = i; j > 0 && syms[j - 1] > syms[j]; j--) {
                t = syms[j]; syms[j] = syms[j - 1]; syms[j - 1] = t;
            }
        for (i = 0, t = 0; i < nsh; i++) { base[syms[i]] = t; t += count[syms[i]]; }
        st->red = xmalloc(st->nr * sizeof *st->red);
        st->nr = 0;
        for (i = 0; i < nitemset; i++) {
            s = ritem[itemset[i]];
            if (s < 0) st->red[st->nr++] = -1 - s;
            else kbuf[base[s]++] = itemset[i] + 1;
        }
        st->nt = nsh;
        st->trans = xmalloc(nsh * sizeof *st->trans);
        for (i = 0; i < nsh; i++) {
            s = syms[i];
            st->trans[i] = getstate(s, kbuf + base[s] - count[s], count[s]);
            st = states[n];             /* (states may have moved) */
            count[s] = 0;
        }
    }
    free(count); free(base); free(kbuf); free(syms);
    /* the accepting state: $accept : start $end . */
    for (n = 0; n < nstates; n++)
        if (states[n]->nk == 1 && states[n]->kern[0] == 2) finalstate = n;
}

/* the transition from state s on symbol sym (-1: none) */
static int trans(int s, int sym)
{
    struct state *st = states[s];
    int lo = 0, hi = st->nt - 1, m, x;
    while (lo <= hi) {
        m = (lo + hi) / 2;
        x = states[st->trans[m]]->sym;
        if (x == sym) return st->trans[m];
        if (x < sym) lo = m + 1; else hi = m - 1;
    }
    return -1;
}

/* ---- LALR(1) ---- */

static unsigned *F;                     /* per nonterminal transition, terminals */
static int *ehead, *enext, *eto, nedges, edgecap;      /* a relation */
static int *lbhead, *lbnext, *lbgoto, nlb, lbcap;      /* lookbacks, per reduction */
static int *dindex, *dstack, dtop;

static int mapgoto(int s, int sym)
{
    int lo = gotomap[sym - ntokens], hi = gotomap[sym - ntokens + 1] - 1, m;
    while (lo <= hi) {
        m = (lo + hi) / 2;
        if (fromst[m] == s) return m;
        if (fromst[m] < s) lo = m + 1; else hi = m - 1;
    }
    fatal("internal error: no goto", 0);
    return 0;
}

static void addedge(int from, int to)
{
    if (nedges == edgecap) {
        edgecap = edgecap ? edgecap * 2 : 256;
        enext = xrealloc(enext, edgecap * sizeof *enext);
        eto = xrealloc(eto, edgecap * sizeof *eto);
    }
    eto[nedges] = to;
    enext[nedges] = ehead[from];
    ehead[from] = nedges++;
}

static void traverse(int i)
{
    int e, j, k, height;
    unsigned *fi = F + i * tsize, *fj;
    dstack[++dtop] = i;
    dindex[i] = height = dtop;
    for (e = ehead[i]; e >= 0; e = enext[e]) {
        j = eto[e];
        if (!dindex[j]) traverse(j);
        if (dindex[i] > dindex[j]) dindex[i] = dindex[j];
        fj = F + j * tsize;
        for (k = 0; k < tsize; k++) fi[k] |= fj[k];
    }
    if (dindex[i] == height)
        for (;;) {
            j = dstack[dtop--];
            dindex[j] = ngotos + 2;
            if (j == i) break;
            memcpy(F + j * tsize, fi, tsize * sizeof *fi);
        }
}

/* F(x) = F(x) + F(y) for every x R y, through the relation's cycles */
static void digraph(void)
{
    int i;
    memset(dindex, 0, ngotos * sizeof *dindex);
    dtop = 0;
    for (i = 0; i < ngotos; i++)
        if (!dindex[i] && ehead[i] >= 0) traverse(i);
}

static int redindex(int s, int r)
{
    int k;
    for (k = 0; k < states[s]->nr; k++)
        if (states[s]->red[k] == r) return states[s]->la + k;
    fatal("internal error: no reduction", 0);
    return 0;
}

void lalr(void)
{
    int i, j, k, n, s, r, t, x, nla, *path, plen, A, it, e;
    unsigned *f, *la;
    tsize = (ntokens + 31) >> 5;
    /* the nonterminal transitions, by symbol then state */
    gotomap = xmalloc((nnterms + 1) * sizeof *gotomap);
    for (s = 0; s < nstates; s++)
        for (i = 0; i < states[s]->nt; i++)
            if ((x = states[states[s]->trans[i]]->sym) >= ntokens) gotomap[x - ntokens + 1]++;
    for (i = 0; i < nnterms; i++) gotomap[i + 1] += gotomap[i];
    ngotos = gotomap[nnterms];
    fromst = xmalloc((ngotos + 1) * sizeof *fromst);
    tost = xmalloc((ngotos + 1) * sizeof *tost);
    path = xmalloc((nnterms + 1) * sizeof *path);
    memcpy(path, gotomap, (nnterms + 1) * sizeof *path);
    for (s = 0; s < nstates; s++)
        for (i = 0; i < states[s]->nt; i++) {
            t = states[s]->trans[i];
            if ((x = states[t]->sym) >= ntokens) {
                k = path[x - ntokens]++;
                fromst[k] = s;
                tost[k] = t;
            }
        }
    free(path);
    /* each state's first lookahead set */
    for (s = 0, nla = 0; s < nstates; s++) {
        states[s]->la = nla;
        nla += states[s]->nr;
    }
    LA = xmalloc((nla + 1) * tsize * sizeof *LA);
    F = xmalloc((ngotos + 1) * tsize * sizeof *F);
    ehead = xmalloc((ngotos + 1) * sizeof *ehead);
    dindex = xmalloc((ngotos + 1) * sizeof *dindex);
    dstack = xmalloc((ngotos + 2) * sizeof *dstack);
    /* DR, and the reads relation */
    for (i = 0; i < ngotos; i++) ehead[i] = -1;
    nedges = 0;
    for (i = 0; i < ngotos; i++) {
        struct state *st = states[tost[i]];
        f = F + i * tsize;
        for (j = 0; j < st->nt; j++) {
            x = states[st->trans[j]]->sym;
            if (x < ntokens) SET(f, x);
            else if (nullable[x]) addedge(i, mapgoto(tost[i], x));
        }
    }
    digraph();
    /* includes and lookback */
    for (i = 0; i < ngotos; i++) ehead[i] = -1;
    nedges = 0;
    lbhead = xmalloc((nla + 1) * sizeof *lbhead);
    for (i = 0; i < nla; i++) lbhead[i] = -1;
    path = xmalloc((nitems + 1) * sizeof *path);
    for (i = 0; i < ngotos; i++) {
        s = fromst[i];
        A = states[tost[i]]->sym;
        for (j = dstart[A - ntokens]; j < dstart[A - ntokens + 1]; j++) {
            r = drules[j];
            path[0] = s;
            plen = 1;
            for (it = rules[r].rhs; ritem[it] >= 0; it++) {
                s = trans(s, ritem[it]);
                path[plen++] = s;
            }
            /* the reduction of r in s looks back to (fromst[i], A) */
            if (nlb == lbcap) {
                lbcap = lbcap ? lbcap * 2 : 256;
                lbnext = xrealloc(lbnext, lbcap * sizeof *lbnext);
                lbgoto = xrealloc(lbgoto, lbcap * sizeof *lbgoto);
            }
            k = redindex(s, r);
            lbgoto[nlb] = i;
            lbnext[nlb] = lbhead[k];
            lbhead[k] = nlb++;
            /* (path[n], B) includes (fromst[i], A) if B's rest is nullable */
            for (n = rules[r].len - 1; n >= 0; n--) {
                x = ritem[rules[r].rhs + n];
                if (x < ntokens) break;
                addedge(mapgoto(path[n], x), i);
                if (!nullable[x]) break;
            }
            s = fromst[i];
        }
    }
    free(path);
    digraph();
    /* LA: the union of Follow over the lookbacks */
    for (k = 0; k < nla; k++) {
        la = LA + k * tsize;
        for (e = lbhead[k]; e >= 0; e = lbnext[e]) {
            f = F + lbgoto[e] * tsize;
            for (j = 0; j < tsize; j++) la[j] |= f[j];
        }
    }
    free(F); free(ehead); free(enext); free(eto); free(dindex); free(dstack);
    free(lbhead); free(lbnext); free(lbgoto);
}

/* ---- actions and conflicts ---- */

/* y.output's text: by fwrite (fprintf is slow on P32W) */
static void vs(char *s)
{
    fwrite(s, 1, strlen(s), vout);
}

static void vn(int n)
{
    char d[12];
    int i = 11;
    d[11] = 0;
    do d[--i] = '0' + n % 10; while ((n /= 10) > 0);
    vs(d + i);
}

/* an item: \tlhs : a . b  (rule) */
static void pitem(int it)
{
    int r, i;
    for (i = it; ritem[i] >= 0; i++) ;
    r = -1 - ritem[i];
    vs("\t");
    vs(symtab[rules[r].lhs]->name);
    vs(" :");
    for (i = rules[r].rhs; ; i++) {
        if (i == it) vs(" .");
        if (ritem[i] < 0) break;
        vs(" ");
        vs(symtab[ritem[i]]->name);
    }
    vs("  (");
    vn(r);
    vs(")\n");
}

/* an action: \tsymbol  what n */
static void pact(int sym, char *what, int n)
{
    vs("\t");
    vs(symtab[sym]->name);
    vs("  ");
    vs(what);
    if (n >= 0) { vs(" "); vn(n); }
    vs("\n");
}

static void prules(void)
{
    int r, i, last = -1;
    for (r = 0; r < nrules; r++) {
        if (rules[r].lhs != last) {
            if (r) fprintf(vout, "\n");
            fprintf(vout, "%4d  %s :", r, symtab[rules[r].lhs]->name);
        } else fprintf(vout, "%4d  %*s |", r, (int)strlen(symtab[rules[r].lhs]->name), "");
        last = rules[r].lhs;
        for (i = rules[r].rhs; ritem[i] >= 0; i++) fprintf(vout, " %s", symtab[ritem[i]]->name);
        if (rules[r].len == 0) fprintf(vout, " /* empty */");
        fprintf(vout, "\n");
    }
    fprintf(vout, "\f\n");
}

#define ERRV 0x7fffffff                 /* an explicit error (%nonassoc) */

/* the first terminal >= t in a set (ntokens: none) */
static int nextbit(unsigned *set, int t)
{
    int w = t >> 5;
    unsigned x;
    if (w >= tsize) return ntokens;
    x = set[w] >> (t & 31);
    for (;;) {
        if (x) {
            while (!(x & 1)) { x >>= 1; t++; }
            return t;
        }
        if (++w >= tsize) return ntokens;
        x = set[w];
        t = w << 5;
    }
}

static char **log;                      /* y.output's resolutions in a state */
static int nlog, logcap;

static void logres(int r, int t, int how)
{
    if (nlog == logcap) {
        logcap = logcap ? logcap * 2 : 16;
        log = xrealloc(log, logcap * sizeof *log);
    }
    log[nlog] = xmalloc(strlen(symtab[t]->name) + 80);
    sprintf(log[nlog++], "\tConflict between rule %d and token %s resolved as %s.\n",
            r, symtab[t]->name, how == 0 ? "reduce" : how == 1 ? "shift" : "an error");
}

/* each state's actions on the terminals, its default reduction; y.output */
void actions(void)
{
    int s, i, k, t, r, n, rp, tp, max, nodef, consistent, haverr;
    int *row, *shift, *nred, *rk, *cnt, *tsym, *tval;
    char *errs;
    unsigned *lk;
    struct state *st;
    row = xmalloc(ntokens * sizeof *row);
    shift = xmalloc(ntokens * sizeof *shift);
    nred = xmalloc(ntokens * sizeof *nred);
    rk = xmalloc(ntokens * sizeof *rk);
    tsym = xmalloc(ntokens * sizeof *tsym);
    tval = xmalloc(ntokens * sizeof *tval);
    errs = xmalloc(ntokens);
    cnt = xmalloc((nrules + 1) * sizeof *cnt);
    for (t = 0; t < ntokens; t++) shift[t] = -1;
    if (vout) prules();
    for (s = 0; s < nstates; s++) {
        st = states[s];
        nlog = 0;
        haverr = 0;
        for (t = 0; t < ntokens; t++) { row[t] = 0; nred[t] = 0; errs[t] = 0; }
        consistent = st->nr <= 1;
        for (i = 0; i < st->nt && (t = states[st->trans[i]]->sym) < ntokens; i++) {
            shift[t] = st->trans[i];            /* (terminals come first) */
            consistent = st->nr == 0;
        }
        if (s == finalstate) {
            st->nr = 0;                 /* (rule 0: accepted when entered) */
            consistent = 1;
        }
        /* shift/reduce conflicts that precedence resolves */
        for (k = 0; k < st->nr; k++) {
            r = st->red[k];
            if (!(rp = rules[r].prec)) continue;
            lk = LA + (st->la + k) * tsize;
            for (t = nextbit(lk, 0); t < ntokens; t = nextbit(lk, t + 1)) {
                if (shift[t] < 0 || !(tp = symtab[t]->prec)) continue;
                if (tp < rp || (tp == rp && symtab[t]->assoc == A_LEFT)) {
                    shift[t] = -1;              /* reduce */
                    n = 0;
                } else if (tp > rp || symtab[t]->assoc == A_RIGHT) {
                    CLR(lk, t);                 /* shift */
                    n = 1;
                } else {
                    shift[t] = -1;              /* %nonassoc: an error */
                    CLR(lk, t);
                    errs[t] = haverr = 1;
                    n = 2;
                }
                if (vout) logres(r, t, n);
            }
        }
        /* the reductions, the earlier rule winning */
        for (k = st->nr - 1; k >= 0; k--) {
            lk = LA + (st->la + k) * tsize;
            for (t = nextbit(lk, 0); t < ntokens; t = nextbit(lk, t + 1)) {
                row[t] = -st->red[k];
                rk[t] = k;
                nred[t]++;
            }
        }
        /* the conflicts left: counted; the shift wins */
        for (t = 0; t < ntokens; t++) {
            if (!nred[t]) continue;
            if (shift[t] >= 0) {
                nsr++;
                if (vout)
                    fprintf(vout, "%d: shift/reduce conflict (shift %d, reduce %d) on %s\n",
                            s, shift[t], st->red[rk[t]], symtab[t]->name);
            }
            if (nred[t] >= 2) {
                nrr++;
                if (vout)
                    for (k = rk[t] + 1; k < st->nr; k++)
                        if (ISSET(LA + (st->la + k) * tsize, t))
                            fprintf(vout, "%d: reduce/reduce conflict (reduce %d, reduce %d) on %s\n",
                                    s, st->red[rk[t]], st->red[k], symtab[t]->name);
            }
        }
        nodef = 0;
        for (i = 0; i < st->nt && (t = states[st->trans[i]]->sym) < ntokens; i++)
            if (shift[t] >= 0) {
                row[t] = shift[t];
                if (t == 1) nodef = 1;          /* error is shifted: no default */
            }
        if (haverr)
            for (t = 0; t < ntokens; t++)
                if (errs[t]) row[t] = ERRV;
        /* the default reduction: the commonest */
        st->defrule = 0;
        if (st->nr >= 1 && !nodef) {
            if (consistent) st->defrule = st->red[0];
            else {
                for (k = 0; k < st->nr; k++) cnt[k] = 0;
                for (t = 0; t < ntokens; t++)
                    if (row[t] < 0) cnt[rk[t]]++;
                for (k = 0, max = 0; k < st->nr; k++)
                    if (cnt[k] > max) { max = cnt[k]; st->defrule = st->red[k]; }
            }
        }
        if (st->defrule) rules[st->defrule].used = 1;
        /* y.output */
        if (vout) {
            vs(s ? "\n\nstate " : "state ");
            vn(s);
            vs("\n");
            for (i = 0; i < st->nk; i++) pitem(st->kern[i]);
            fprintf(vout, "\n");
            for (t = 0; t < ntokens; t++)
                if (row[t] > 0 && row[t] != ERRV) {
                    if (row[t] == finalstate) pact(t, "accept", -1);
                    else pact(t, "shift", row[t]);
                }
            for (t = 0; t < ntokens; t++) {
                if (row[t] < 0 && row[t] != -st->defrule)
                    pact(t, "reduce", -row[t]);
                if (row[t] == ERRV) pact(t, "error (nonassociative)", -1);
            }
            if (st->defrule) fprintf(vout, "\t.  reduce %d\n", st->defrule);
            else if (s != finalstate) fprintf(vout, "\t.  error\n");
            n = 0;
            for (i = 0; i < st->nt; i++)
                if ((t = states[st->trans[i]]->sym) >= ntokens) {
                    if (!n++) fprintf(vout, "\n");
                    pact(t, "goto", st->trans[i]);
                }
            if (nlog) fprintf(vout, "\n");
            for (i = 0; i < nlog; i++) { fputs(log[i], vout); free(log[i]); }
        }
        /* keep the actions other than the default (an error without one) */
        for (t = 0, n = 0; t < ntokens; t++) {
            if (!row[t] || (st->defrule && row[t] == -st->defrule)) continue;
            if (row[t] == ERRV && !st->defrule) continue;
            if (row[t] < 0) rules[-row[t]].used = 1;
            tsym[n] = t;
            tval[n++] = row[t] == ERRV ? 0 : row[t];
        }
        st->nact = n;
        st->asym = xmalloc(n * sizeof *st->asym);
        st->aval = xmalloc(n * sizeof *st->aval);
        for (i = 0; i < n; i++) {
            st->asym[i] = tsym[i];
            st->aval[i] = tval[i];
        }
        for (i = 0; i < st->nt && (t = states[st->trans[i]]->sym) < ntokens; i++) shift[t] = -1;
    }
    /* rules never reduced */
    for (r = 1, n = 0; r < nrules; r++)
        if (!rules[r].used) n++;
    nunused = n;
    if (n) {
        if (vout) {
            fprintf(vout, "\n\nRules never reduced:\n");
            for (r = 1; r < nrules; r++)
                if (!rules[r].used) pitem(rules[r].rhs + rules[r].len);
        }
    }
    if (vout) {
        if (nsr || nrr) {
            fprintf(vout, "\n\n");
            if (nsr) fprintf(vout, "%d shift/reduce conflict%s", nsr, nsr == 1 ? "" : "s");
            if (nsr && nrr) fprintf(vout, ", ");
            if (nrr) fprintf(vout, "%d reduce/reduce conflict%s", nrr, nrr == 1 ? "" : "s");
            fprintf(vout, ".\n");
        }
        fprintf(vout, "\n\n%d terminals, %d nonterminals\n", ntokens, nnterms);
        fprintf(vout, "%d grammar rules, %d states\n", nrules, nstates);
    }
    free(row); free(shift); free(nred); free(rk); free(tsym); free(tval); free(errs); free(cnt);
}
