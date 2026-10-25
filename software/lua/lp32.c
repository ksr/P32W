/*
** lp32.c -- Lua on P32W Unix: IEEE single precision for string.pack's "f"
** (P32W's float is the 8-byte double), plain C that also builds elsewhere
** (LUA_P32HOST, to test it on another machine). (The C library functions
** Lua needed that P32W's libc lacked used to be here too; they are in
** libc now.)
*/

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lp32.h"


/*
** {======================================================
** IEEE single precision ("f" in string.pack) when the C compiler's float
** is a double (P32W): the binary32 bits of a double, rounded to nearest
** (ties to even), and back (exactly).
** =======================================================
*/
unsigned lp32_tof32 (double x) {
  unsigned long long u, m, rem, half;
  unsigned s, q;
  int ex, e, sh;
  memcpy(&u, &x, sizeof u);
  s = (unsigned)(u >> 32) & 0x80000000u;
  ex = (int)((u >> 52) & 0x7FF);
  m = u & 0xFFFFFFFFFFFFFull;
  if (ex == 0x7FF)			/* inf, NaN (quiet, the payload's top) */
    return m ? s | 0x7FC00000u | (unsigned)(m >> 29) : s | 0x7F800000u;
  if (ex == 0) return s;		/* zero, or far below the smallest single */
  m |= 0x10000000000000ull;
  e = ex - 1023;
  if (e > 127) return s | 0x7F800000u;	/* overflow: inf */
  sh = e >= -126 ? 29 : 29 + (-126 - e);	/* bits to drop */
  if (sh > 60) return s;
  q = (unsigned)(m >> sh);
  rem = m & ((1ull << sh) - 1);
  half = 1ull << (sh - 1);
  if (rem > half || (rem == half && (q & 1))) q++;
  if (e < -126) return s | q;		/* subnormal (a carry makes it normal) */
  if (q == 0x1000000u) { q >>= 1; e++; }	/* 1.111... rounded up to 2 */
  if (e > 127) return s | 0x7F800000u;
  return s | ((unsigned)(e + 127) << 23) | (q & 0x7FFFFF);
}

double lp32_fromf32 (unsigned f) {
  unsigned long long u, s = (unsigned long long)(f & 0x80000000u) << 32;
  int e = (int)((f >> 23) & 0xFF);
  unsigned m = f & 0x7FFFFF;
  double x;
  if (e == 0xFF) u = s | 0x7FF0000000000000ull | ((unsigned long long)m << 29);
  else if (e == 0) {			/* zero, subnormal: m * 2^-149 */
    x = (double)m;
    x = x * (1.0 / 4194304.0) * (1.0 / 4194304.0) * (1.0 / 4194304.0)	/* 2^-22 (exact) */
          * (1.0 / 4194304.0) * (1.0 / 4194304.0) * (1.0 / 4194304.0) * (1.0 / 131072.0);
    return (f & 0x80000000u) ? -x : x;
  }
  else u = s | ((unsigned long long)(e - 127 + 1023) << 52) | ((unsigned long long)m << 29);
  memcpy(&x, &u, sizeof x);
  return x;
}

/* }====================================================== */
