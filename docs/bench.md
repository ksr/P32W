# Hand-written assembly against C (first measurement, 2026-10-14)

The backlog item "Measure hand-written assembly against C" asks how much
speed assembly buys over p32cc. This is its first round: the inner loops
that the kernel, libc and commands spend their time in. Each loop is
written twice, in C and in hand-written assembly with the same ABI, and
timed in P32W microcycles on the microcode-level emulator (`p32uemu`).

Rerun the measurement with `software/bench/run.sh`, which prints the table
below. The files:

- `software/bench/bench.c` holds the C routines and the driver; `-DBENCH=n`
  picks the routine.
- `software/bench/bench_asm.s` holds the assembly versions.

Each figure has the setup-only baseline (BENCH=0) subtracted. All three
builds compute the same results: the exit statuses match.

| routine | stands for | C cycles | C, no regvars | asm cycles | C / asm | no-regvars / asm | C / asm at first |
|---|---|---:|---:|---:|---:|---:|---:|
| strlen (999 B × 20) | libc string functions | 404 181 | 824 235 | 312 781 | 1.29 | 2.63 | 1.29 |
| wcopy (4 KiB in words × 20) | `memcpy`, `copyin`/`copyout` | 864 801 | 1 622 455 | 406 861 | 2.12 | 3.98 | 3.08 |
| bytesum (4 KiB × 10) | `wc`, `cksum` loops | 1 436 291 | 2 951 835 | 626 191 | 2.29 | 4.71 | 3.20 |
| crc32 (4 KiB × 4) | gzip/zip, table lookups | 1 099 001 | 1 934 555 | 934 633 | 1.17 | 2.06 | 1.42 |
| isort (200 ints) | `sort`, compare-and-move | 771 431 | 1 493 896 | 372 069 | 2.07 | 4.01 | 2.07 |
| count (4 KiB × 10) | `grep`'s byte matching | 1 560 031 | 2 790 975 | 1 035 981 | 1.50 | 2.69 | 2.05 |
| hcopy (512 B in halfwords × 40) | the CF sector transfer | 439 341 | 817 995 | 206 981 | 2.12 | 3.95 | 3.06 |

The last column is the C compiler before the three fixes below (loop
headers, constant offsets, `*p++`).

## Where the gap comes from

**Register variables (M7 step 1) gain the most.** Turning them off makes
these loops 1.5–2× slower again.

**A fix made during this measurement.** p32cc counted a loop's condition
and increment as being outside the loop when it chose which variables to
keep in registers. So the counter in `while (n--)` stayed in memory. The
fix is in both compilers, `cc/cparse.py` and `software/cc/parse.c`, which
must agree byte for byte. It made wcopy, bytesum, crc32, count and hcopy
10–18% faster. Any code with a `while (n--)` loop gains, including the
kernel and the commands.

**What is left is mostly two things:**

1. **Post-increment through a temporary.** `s += *p++` compiles to:

       mov r1, r12
       addi r12, r12, 1
       lbu r1, 0(r1)

   A person writes `lbu r1, 0(r12)` and then `addi r12, r12, 1`. That is
   one instruction more per `*p++` and per `n--` test. Counting
   instructions, bytesum's C loop is 7 instructions per byte, against 4
   for a plain assembly loop.

2. **Unrolling.** The assembly versions unroll by 4 where it comes
   naturally (copies, sums, strlen). That takes the loop's overhead from
   4 instructions per byte to 2.5. A compiler without an optimiser will
   not do this; libc can.

What a compiler can't do here:

- The loop of crc32 (1.26×) is almost all real work, so the compiler is
  close.
- strlen is close too (1.29×): p32cc already walks the pointer in a
  register.

## Done after the measurement (2026-10-14)

- **`memcpy` and `memset` in assembly** (`software/libc/mem.s`). libc and
  the kernel both use it; the kernel's Makefile assembles the same file.
  - Each one aligns, then moves 16 bytes per loop, then words, then the
    tail. memcpy falls back to bytes when source and destination are
    differently aligned.
  - 4 KiB timings: memcpy went from 62 800 to 22 500 cycles (2.8×);
    memset from 45 400 to 14 000 (3.2×).
  - `cc/tests/mem.c` checks every alignment and lengths 0–40 against a
    byte loop.
  - Every `copyin`/`copyout`, `read`/`write` through the buffer cache and
    struct copy uses them.
- **Constant offsets folded into loads and stores** (both compilers):
  - `p[2]` and `*(p + 3)` become `lw r1, 8(rP)` instead of an `addi` then
    a load.
  - A constant address within ±32 KiB of 0 (the I/O page at the top of
    the address space) becomes an offset from r0: `sh r1, -4352(r0)` for
    the CF data register (`0xFFFF_EF00`), instead of `li` plus `sh`.
  - So the kernel's CF sector loop now takes 2 instructions per halfword
    instead of 4, and every device register access is one instruction.
  - (This doesn't change the table: its loops walk pointers with `*p++`.)
- **`*p++` on a register pointer** (both compilers): the load or store
  goes through p, then `addi` moves it, with no temporary in between.
  - This takes one instruction out of every pointer-walking loop: 7–20%
    on the loops above.
  - The suite's check that the C compiler matches p32cc caught a
    missing case (the store side) before this went in.
  - The remaining compiler gap is the `n--` test, which goes through a
    temporary, plus unrolling.

## What this suggests

- **libc and the kernel:** a few routines are worth writing in assembly,
  at 2.6–3× faster each (memcpy and memset are done; see above):
  - `memcpy`, `memset` and `copyin`/`copyout`, as unrolled word copies;
  - the CF transfer loop (`cf.c`), unrolled halfword moves;
  - `strlen`.

  These are small, stable routines, called everywhere.
- **The compiler:** `*p++` is done. `while (n--)` still copies n to a
  temporary to test it; a branch on n, then the decrement, would save one
  more instruction per loop. Any change has to go into both compilers
  (cc_test compares them).
- **Commands as a whole** (wc, grep, sort and a boot-plus-login figure)
  and **the kernel's trap path** are the next round of this measurement.
  Their time is spread across code that assembly would not change, so the
  ratio for a whole program will be well under these inner-loop figures.
