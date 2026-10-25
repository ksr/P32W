/* lexdfa.c -- lex's DFA, made from the NFA of lexre.c:
 *  1. character classes: the 256 characters are split into the classes
 *     that no character set of the patterns tells apart (the same set of
 *     sets holds each character of a class), so the DFA moves on a class
 *     rather than a character (a C tokenizer needs some 50, not 256);
 *  2. subset construction: each DFA state is the set of NFA nodes it
 *     stands for (only the nodes that matter: N_SET, N_ACC, N_HEAD), found
 *     again through a hash table; two start states for each start
 *     condition, one for the start of a line (the ^ rules too);
 *  3. minimisation (Moore's): states are split, from a first partition by
 *     what they accept, until no class leads two states of a block to
 *     different blocks; then numbered again in the order a breadth-first
 *     walk from the start states meets them.
 * A state accepts the rules whose N_ACC it holds; the first (the earliest
 * rule) is the one taken, so only it counts unless REJECT is used, which
 * needs the whole list. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lex.h"

int nclass, ndfa, ndfa0;
int ec[NCHAR];
int *dtrans, *dacc, *dacclist, *dhead, *dstart;

static int **setcls, *nsetcls;  /* the classes in each character set */

/* the NFA node sets of the states being made */
static int *pool, npool, poolcap;
static int *soff, *slen, scap;
static int *htab, *hnext;
#define HSIZE 1024

/* scratch for the closure */
static int *mark, gen, *stk, *cbuf;

/* signatures: int strings numbered as they are first seen */
static int *sigpool, nsigpool, sigpoolcap;
static int *sigoff, *siglen, *signext, nsig, sigcap;
static int sighead[HSIZE];

static int intcmp(const void *a, const void *b)
{
    return *(int *) a - *(int *) b;
}

/* are a[0..n) and b[0..n) the same? (memcmp goes a byte at a time) */
static int sameints(int *a, int *b, int n)
{
    while (n-- > 0)
        if (*a++ != *b++) return 0;
    return 1;
}

static unsigned hashints(int *v, int n)
{
    unsigned h = n;
    while (n-- > 0) h = h * 33 + *v++;
    return h;
}

static void classes(void)
{
    int map[2 * NCHAR], c, s, k, n = 1, nn;
    char seen[NCHAR];
    for (c = 0; c < NCHAR; c++) ec[c] = 0;
    for (s = 0; s < ncset; s++) {
        for (k = 0; k < 2 * n; k++) map[k] = -1;
        nn = 0;
        for (c = 0; c < NCHAR; c++) {
            k = 2 * ec[c] + (CS_HAS(&csets[s], c) ? 1 : 0);
            if (map[k] < 0) map[k] = nn++;
            ec[c] = map[k];
        }
        n = nn;
    }
    nclass = n;
    setcls = xalloc((ncset + 1) * sizeof(int *));
    nsetcls = xalloc((ncset + 1) * sizeof(int));
    for (s = 0; s < ncset; s++) {
        memset(seen, 0, sizeof seen);
        setcls[s] = xalloc(nclass * sizeof(int));
        for (c = 0; c < NCHAR; c++)
            if (CS_HAS(&csets[s], c) && !seen[ec[c]]) {
                seen[ec[c]] = 1;
                setcls[s][nsetcls[s]++] = ec[c];
            }
    }
}

/* the nodes reachable from seeds[0..n) without a character, the ones
 * that matter, sorted, into cbuf; returns how many */
static int closure(int *seeds, int n)
{
    int sp = 0, cnt = 0, v, w;
    gen++;
    while (n-- > 0) {
        v = *seeds++;
        if (mark[v] != gen) {
            mark[v] = gen;
            stk[sp++] = v;
        }
    }
    while (sp > 0) {
        v = stk[--sp];
        if (nfa_op[v] != N_EPS) cbuf[cnt++] = v;
        if (nfa_op[v] == N_SET || nfa_op[v] == N_ACC) continue;
        w = nfa_out1[v];
        if (w >= 0 && mark[w] != gen) { mark[w] = gen; stk[sp++] = w; }
        w = nfa_out2[v];
        if (w >= 0 && mark[w] != gen) { mark[w] = gen; stk[sp++] = w; }
    }
    if (cnt > 1) qsort(cbuf, cnt, sizeof(int), intcmp);
    return cnt;
}

/* the state for the node set cbuf[0..n): found, or made */
static int addstate(int n)
{
    unsigned h = hashints(cbuf, n) % HSIZE;
    int s;
    for (s = htab[h]; s >= 0; s = hnext[s])
        if (slen[s] == n && sameints(pool + soff[s], cbuf, n)) return s;
    if (ndfa == scap) {
        scap = scap ? 2 * scap : 256;
        soff = xrealloc(soff, scap * sizeof(int));
        slen = xrealloc(slen, scap * sizeof(int));
        hnext = xrealloc(hnext, scap * sizeof(int));
        dtrans = xrealloc(dtrans, scap * nclass * sizeof(int));
    }
    if (npool + n > poolcap) {
        while (npool + n > poolcap) poolcap = poolcap ? 2 * poolcap : 4096;
        pool = xrealloc(pool, poolcap * sizeof(int));
    }
    memcpy(pool + npool, cbuf, n * sizeof(int));
    soff[ndfa] = npool;
    slen[ndfa] = n;
    npool += n;
    hnext[ndfa] = htab[h];
    htab[h] = ndfa;
    return ndfa++;
}

static void subsets(void)
{
    int *cnt, *off, *fill, *bucket = 0, nbucket = 0, *bhash, *distinct, nd;
    int sc, bol, i, k, j, d, v, n, t, total;
    int *seeds = xalloc((nrules + 1) * sizeof(int));
    mark = xalloc(nnfa * sizeof(int));
    stk = xalloc(nnfa * sizeof(int));
    cbuf = xalloc(nnfa * sizeof(int));
    htab = xalloc(HSIZE * sizeof(int));
    for (i = 0; i < HSIZE; i++) htab[i] = -1;
    dstart = xalloc(2 * nsc * sizeof(int));
    for (sc = 0; sc < nsc; sc++)
        for (bol = 0; bol < 2; bol++) {
            n = 0;
            for (i = 0; i < nrules; i++)
                if (rules[i].sc[sc] && (bol || !rules[i].bol)) seeds[n++] = rulestart[i];
            dstart[2 * sc + bol] = addstate(closure(seeds, n));
        }
    cnt = xalloc(nclass * sizeof(int));
    off = xalloc(nclass * sizeof(int));
    fill = xalloc(nclass * sizeof(int));
    bhash = xalloc(nclass * sizeof(int));
    distinct = xalloc(nclass * sizeof(int));
    for (d = 0; d < ndfa; d++) {
        /* the targets of each class's moves, in buckets */
        for (k = 0; k < nclass; k++) cnt[k] = fill[k] = 0;
        for (i = 0; i < slen[d]; i++) {
            v = pool[soff[d] + i];
            if (nfa_op[v] == N_SET)
                for (j = 0; j < nsetcls[nfa_arg[v]]; j++) cnt[setcls[nfa_arg[v]][j]]++;
        }
        total = 0;
        for (k = 0; k < nclass; k++) {
            off[k] = total;
            total += cnt[k];
        }
        if (total > nbucket) {
            nbucket = 2 * total;
            bucket = xrealloc(bucket, nbucket * sizeof(int));
        }
        for (i = 0; i < slen[d]; i++) {
            v = pool[soff[d] + i];
            if (nfa_op[v] == N_SET)
                for (j = 0; j < nsetcls[nfa_arg[v]]; j++) {
                    k = setcls[nfa_arg[v]][j];
                    bucket[off[k] + fill[k]++] = nfa_out1[v];
                }
        }
        /* each bucket's closure is a state (classes with the same bucket
         * share it) */
        nd = 0;
        for (k = 0; k < nclass; k++) {
            if (cnt[k] == 0) {
                dtrans[d * nclass + k] = -1;
                continue;
            }
            bhash[k] = hashints(bucket + off[k], cnt[k]);
            for (j = 0; j < nd; j++) {
                t = distinct[j];
                if (bhash[t] == bhash[k] && cnt[t] == cnt[k] &&
                    sameints(bucket + off[t], bucket + off[k], cnt[k])) break;
            }
            if (j < nd) {
                dtrans[d * nclass + k] = dtrans[d * nclass + distinct[j]];
                continue;
            }
            distinct[nd++] = k;
            n = closure(bucket + off[k], cnt[k]);
            t = addstate(n);    /* (may move dtrans) */
            dtrans[d * nclass + k] = t;
        }
    }
    free(cnt); free(off); free(fill); free(bhash); free(distinct); free(bucket);
    free(seeds); free(mark); free(stk); free(cbuf); free(htab); free(hnext);
}

static void sig_reset(void)
{
    int i;
    nsig = nsigpool = 0;
    for (i = 0; i < HSIZE; i++) sighead[i] = -1;
}

/* the number of the signature v[0..n) */
static int sig_find(int *v, int n)
{
    unsigned h = hashints(v, n) % HSIZE;
    int s;
    for (s = sighead[h]; s >= 0; s = signext[s])
        if (siglen[s] == n && sameints(sigpool + sigoff[s], v, n)) return s;
    if (nsig == sigcap) {
        sigcap = sigcap ? 2 * sigcap : 256;
        sigoff = xrealloc(sigoff, sigcap * sizeof(int));
        siglen = xrealloc(siglen, sigcap * sizeof(int));
        signext = xrealloc(signext, sigcap * sizeof(int));
    }
    if (nsigpool + n > sigpoolcap) {
        while (nsigpool + n > sigpoolcap) sigpoolcap = sigpoolcap ? 2 * sigpoolcap : 4096;
        sigpool = xrealloc(sigpool, sigpoolcap * sizeof(int));
    }
    memcpy(sigpool + nsigpool, v, n * sizeof(int));
    sigoff[nsig] = nsigpool;
    siglen[nsig] = n;
    nsigpool += n;
    signext[nsig] = sighead[h];
    sighead[h] = nsig;
    return nsig++;
}

/* what state s accepts, into v (head bits, count, rules); returns the length */
static int accepts(int s, int *v, int all)
{
    int i, n = 2, x;
    v[0] = v[1] = 0;
    for (i = 0; i < slen[s]; i++) {
        x = pool[soff[s] + i];
        if (nfa_op[x] == N_HEAD) v[0] |= 1 << nfa_arg[x];
        else if (nfa_op[x] == N_ACC && (all || v[1] == 0)) {
            v[n++] = nfa_arg[x];
            v[1]++;
        }
    }
    return n;
}

void dfa_build(void)
{
    int *block, *nblock, *sig, *newid, *rep, *queue, *ot;
    int s, k, t, n, nb, i, b, qn, siglenmax;
    classes();
    subsets();
    ndfa0 = ndfa;
    siglenmax = nclass + 1 > nrules + 2 ? nclass + 1 : nrules + 2;
    sig = xalloc(siglenmax * sizeof(int));
    block = xalloc(ndfa * sizeof(int));
    nblock = xalloc(ndfa * sizeof(int));
    /* the first partition: by what is accepted */
    sig_reset();
    for (s = 0; s < ndfa; s++) {
        n = accepts(s, sig, reject);
        for (i = 0; i < sig[1]; i++) rules[sig[2 + i]].used = 1;
        block[s] = sig_find(sig, n);
    }
    nb = nsig;
    /* split until stable */
    for (;;) {
        sig_reset();
        for (s = 0; s < ndfa; s++) {
            sig[0] = block[s];
            for (k = 0; k < nclass; k++) {
                t = dtrans[s * nclass + k];
                sig[1 + k] = t < 0 ? -1 : block[t];
            }
            nblock[s] = sig_find(sig, nclass + 1);
        }
        n = nsig;
        memcpy(block, nblock, ndfa * sizeof(int));
        if (n == nb) break;
        nb = n;
    }
    /* number the blocks breadth first from the start states */
    newid = xalloc(nb * sizeof(int));
    rep = xalloc(nb * sizeof(int));
    queue = xalloc(nb * sizeof(int));
    for (b = 0; b < nb; b++) newid[b] = -1;
    for (s = 0; s < ndfa; s++) rep[block[s]] = s;
    qn = 0;
    for (i = 0; i < 2 * nsc; i++) {
        b = block[dstart[i]];
        if (newid[b] < 0) { newid[b] = qn; queue[qn++] = b; }
    }
    for (i = 0; i < qn; i++)
        for (k = 0; k < nclass; k++) {
            t = dtrans[rep[queue[i]] * nclass + k];
            if (t >= 0 && newid[block[t]] < 0) {
                newid[block[t]] = qn;
                queue[qn++] = block[t];
            }
        }
    /* the minimal DFA */
    ot = dtrans;
    dtrans = xalloc(qn * nclass * sizeof(int));
    dacc = xalloc((qn + 1) * sizeof(int));
    dhead = xalloc(qn * sizeof(int));
    dacclist = xalloc((qn * (nrules > 0 ? 1 : 0) + 1) * sizeof(int));
    n = 0;
    for (i = 0; i < qn; i++) {
        s = rep[queue[i]];
        for (k = 0; k < nclass; k++) {
            t = ot[s * nclass + k];
            dtrans[i * nclass + k] = t < 0 ? -1 : newid[block[t]];
        }
        k = accepts(s, sig, reject);
        dhead[i] = sig[0];
        dacc[i] = n;
        dacclist = xrealloc(dacclist, (n + sig[1] + 1) * sizeof(int));
        for (k = 0; k < sig[1]; k++) dacclist[n++] = sig[2 + k];
    }
    dacc[qn] = n;
    for (i = 0; i < 2 * nsc; i++) dstart[i] = newid[block[dstart[i]]];
    ndfa = qn;
    free(ot); free(block); free(nblock); free(sig); free(newid); free(rep); free(queue);
    free(pool); free(soff); free(slen);
    free(sigpool); free(sigoff); free(siglen); free(signext);
}
