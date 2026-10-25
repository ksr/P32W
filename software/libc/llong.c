/* llong.c -- P32W C library: long long arithmetic (D30).
 *
 * A long long lives in memory, two words, low first, and the compilers turn
 * each operation on it into a call of one of these, passing and returning
 * long longs by value (the convention for structs: every argument on the
 * stack, the result through a hidden pointer). They are written on a
 * struct of the same layout, since they implement the type itself. */

typedef struct { unsigned lo, hi; } ll;

static ll mk(unsigned lo, unsigned hi) { ll r; r.lo = lo; r.hi = hi; return r; }

ll __lladd(ll a, ll b) { unsigned lo = a.lo + b.lo; return mk(lo, a.hi + b.hi + (lo < a.lo)); }
ll __llsub(ll a, ll b) { return mk(a.lo - b.lo, a.hi - b.hi - (a.lo < b.lo)); }
ll __lland(ll a, ll b) { return mk(a.lo & b.lo, a.hi & b.hi); }
ll __llor(ll a, ll b)  { return mk(a.lo | b.lo, a.hi | b.hi); }
ll __llxor(ll a, ll b) { return mk(a.lo ^ b.lo, a.hi ^ b.hi); }
ll __llnot(ll a)       { return mk(~a.lo, ~a.hi); }
ll __llneg(ll a)       { return mk(-a.lo, -a.hi - (a.lo != 0)); }
int __lltest(ll a)     { return (a.lo | a.hi) != 0; }

ll __llfromint(int v)       { return mk((unsigned)v, v < 0 ? 0xFFFFFFFFu : 0); }
ll __llfromuint(unsigned v) { return mk(v, 0); }

/* -1, 0 or 1: a below, equal to, above b */
int __llcmp(ll a, ll b)
{
    if (a.hi != b.hi) return (int)a.hi < (int)b.hi ? -1 : 1;
    return a.lo == b.lo ? 0 : a.lo < b.lo ? -1 : 1;
}
int __ullcmp(ll a, ll b)
{
    if (a.hi != b.hi) return a.hi < b.hi ? -1 : 1;
    return a.lo == b.lo ? 0 : a.lo < b.lo ? -1 : 1;
}

ll __llshl(ll a, int n)
{
    n &= 63;
    if (n >= 32) return mk(0, a.lo << (n - 32));
    if (!n) return a;
    return mk(a.lo << n, a.hi << n | a.lo >> (32 - n));
}
ll __ullshr(ll a, int n)                /* logical */
{
    n &= 63;
    if (n >= 32) return mk(a.hi >> (n - 32), 0);
    if (!n) return a;
    return mk(a.lo >> n | a.hi << (32 - n), a.hi >> n);
}
ll __llshr(ll a, int n)                 /* arithmetic */
{
    n &= 63;
    if (n >= 32) return mk((unsigned)((int)a.hi >> (n - 32)), (unsigned)((int)a.hi >> 31));
    if (!n) return a;
    return mk(a.lo >> n | a.hi << (32 - n), (unsigned)((int)a.hi >> n));
}

/* the 64-bit product of two 32-bit numbers, from 16-bit halves */
static ll mul32(unsigned a, unsigned b)
{
    unsigned al = a & 0xFFFF, ah = a >> 16, bl = b & 0xFFFF, bh = b >> 16;
    unsigned ll_ = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh;
    unsigned mid = (ll_ >> 16) + (lh & 0xFFFF) + (hl & 0xFFFF);
    return mk((ll_ & 0xFFFF) | mid << 16, hh + (lh >> 16) + (hl >> 16) + (mid >> 16));
}

ll __llmul(ll a, ll b)
{
    ll r = mul32(a.lo, b.lo);
    r.hi += a.lo * b.hi + a.hi * b.lo;
    return r;
}

/* unsigned a / b and a % b (b != 0): 32-bit when both fit, else shift and
   subtract a bit at a time */
static ll udivmod(ll a, ll b, ll *rem)
{
    ll q, r;
    int k;
    if (!b.hi && !a.hi) {
        if (rem) *rem = mk(b.lo ? a.lo % b.lo : 0, 0);
        return mk(b.lo ? a.lo / b.lo : 0, 0);
    }
    if (b.hi >> 31) {                   /* (b >= 2^63: the quotient is 0 or 1, and the
                                           running remainder below could pass 64 bits) */
        int ge = __ullcmp(a, b) >= 0;
        if (rem) *rem = ge ? __llsub(a, b) : a;
        return mk(ge, 0);
    }
    q = mk(0, 0);
    r = mk(0, 0);
    for (k = 63; k >= 0; k--) {
        r = mk(r.lo << 1 | ((k >= 32 ? a.hi >> (k - 32) : a.lo >> k) & 1), r.hi << 1 | r.lo >> 31);
        if (__ullcmp(r, b) >= 0) {
            r = __llsub(r, b);
            if (k >= 32) q.hi |= 1u << (k - 32); else q.lo |= 1u << k;
        }
    }
    if (rem) *rem = r;
    return q;
}

ll __ulldiv(ll a, ll b) { return udivmod(a, b, 0); }
ll __ullmod(ll a, ll b) { ll r; udivmod(a, b, &r); return r; }

/* signed: the quotient truncated toward 0, the remainder taking a's sign */
ll __lldiv(ll a, ll b)
{
    int neg = ((int)a.hi < 0) != ((int)b.hi < 0);
    ll q = udivmod((int)a.hi < 0 ? __llneg(a) : a, (int)b.hi < 0 ? __llneg(b) : b, 0);
    return neg ? __llneg(q) : q;
}
ll __llmod(ll a, ll b)
{
    ll r;
    udivmod((int)a.hi < 0 ? __llneg(a) : a, (int)b.hi < 0 ? __llneg(b) : b, &r);
    return (int)a.hi < 0 ? __llneg(r) : r;
}
