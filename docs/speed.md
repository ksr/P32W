# Speeding up the CPU: where it can be done

**Status:** ANALYSIS (2026-10-27; Ken: "look where there can be changes to
speed up cpu"). Nothing here is built. The figures come from the timing
analysis (docs/timing.md, `generators/p32timing.py`) with what-if changes,
and from `tools/isamix.sh` on two recorded P32W Unix sessions.

Speed is the clock times the work done per cycle. Today: **3 MHz
guaranteed** (the datasheets' worst case, D72), about **4.9 MHz at typical
delays**, and **3.2 microcycles an instruction**: about 0.94 MIPS at
3 MHz.

## 1. The clock

What the timing analysis says each step would give (worst case: every
chip at its datasheet maximum, 85 °C; typical: 25 °C):

| Change | Worst case | Typical | Cost |
|---|---|---|---|
| As built (D72) | 3.09 MHz | 4.93 MHz | — |
| **A faster oscillator, chosen by a jumper: done, D74** | — | 4 MHz, 19% in hand (4.5 MHz ruled out: the timer's 1 MHz needs a whole divider) | a 32 MHz oscillator in a socket, a 74ACT157 and a jumper on the CPU card; the `-FAST` bus line, so the I/O card's timer divides by 4 |
| Swap every remaining 74HCT part on the failing paths for 74ACT / 74F (about 93 more chips, the device cards' decoders and the MMU / data card's muxes included) | about 3.6 MHz | — | many chips, more supply current, faster edges |
| The control store and the decode tables in 15 ns SRAM, loaded from the flash at reset | 3.09 MHz (other paths limit) | 5.50 MHz | 7 + 2 × CY7C199 and a loader (a counter and a GAL) |
| Both of the above together | 4.09 MHz | 6.44 MHz | both |
| **5 MHz at worst case** | needs the structure changed, below | | |

**Why worst case stops near 4 MHz:** one path crosses the whole machine in
one microcycle, from IR through the decode table's flash (the access size)
to the byte enables, the addressed card's decode and `-ACK`, the bus-error
decision (FAULT), the next microaddress and the control store's flash.
Two flash reads and a bus round trip don't fit 200 ns. Structural fixes:

1. **Latch the decode table's outputs with IR** (look it up during FETCH,
   from R, as the opcode mux already does): the size and the class are
   ready when the cycle starts.
2. **Decide a bus error a cycle later:** a fault latch that the next
   microcycle traps on, so `-ACK` no longer feeds the microaddress in the
   same cycle. The microcode's abort discipline (D10) has to stay exact:
   the faulting cycle's writes must still be cancelled.
3. **Translation a cycle earlier** (microarch.md 8): the TLB looked up as
   MAR loads, so the bus cycle starts with its physical address.

With those and the SRAM control store, 5 MHz is in reach at worst case;
measured, the work per cycle is unchanged, so **5 MHz is 1.67× today**.

**The cheapest step by far is the jumper (D74, done):** most machines will
run at 4 MHz at room temperature. `make test` can't prove that; a bench run of
the test programs at the higher clock (the debug card's listen mode shows
any late signal) would.

## 2. The work per cycle

`tools/isamix.sh` on two sessions (cycles without `WAIT`):

| Session | Instructions | Cycles an instruction |
|---|---|---|
| shell, files (session1) | 24.2 M | 3.20 |
| MicroPython (session15) | 17.5 M | 3.18 |

| Change | Cycles saved | Cost |
|---|---|---|
| **A DMA disk card** (the engine is on the card now, D75; the driver change is still to do) | the CPU's copy loop: `LHU` / `SH` are **12.7%** of the shell's cycles and **21.4%** of Python's (with the loop's `ADDI` / `BNE` more) | a DMA engine on the disk card, a driver change |
| **An overlapped fetch**: the next instruction fetched in an instruction's last cycle when that cycle doesn't use the bus | **11–12%** (every ALU op, `LUI`, `LI32`, a branch not taken: 3 cycles → 2) | a second path from the MMU / data card's data into IR (today the fetched word reaches IR over R, which the last cycle also uses for its result), the shadow PC ahead by one, the microcode's END words; exceptions must stay precise |
| Leaf calls with a link register (`JAL` / `JR lr`) | 1.3% | an ISA and compiler change |
| Post-increment used more by the compiler | 1.2% | compiler |
| Compare with a constant and branch | 0.7% | ISA, compiler |
| `LDM` / `STM` | 0.6% | a register-list sequencer |

The ISA additions left over from D59 together are under 4%; the DMA disk
and the overlapped fetch are the two that matter.

## 3. In order of value for the effort

1. **A clock jumper** (4 MHz besides 3): +33%, a part or two. **Done: D74.**
2. **DMA on the disk card:** 13–21% fewer cycles where the disk is busy.
   **The hardware is built in (D75)**; what remains is the driver.
3. **The control store in SRAM:** typical 5.5 MHz; a step towards 5 MHz
   worst case.
4. **The structural changes for 5 MHz at worst case** (§1, 1–3): 1.67×.
5. **The overlapped fetch:** 11–12%, the largest datapath change of these.

Together: 5 MHz with the fetch overlapped and DMA is about **2× today's
speed** (1.67 × 1.13 × about 1.1), about 2 MIPS.

## 4. The benchmark: a clean make of the commands

Software speed is scored on one job (Ken, 2026-11-05): `make clean; make` in
`/usr/src/cmd`, every command rebuilt on P32W itself, all 101 byte for byte
the host's. Long and repeatable, and it uses the kernel, the disk and the
compiler together. To run it, on a copy of the disk (`emulators.md` §4.7):

```
cd /usr/src/cmd
cycles sh -c "make clean; make"
```

The times are the processor's alone, at 3 MHz, with no drive access time.

| Date | Change | Cycles | Time | User | Sys | Disk commands |
|---|---|---|---|---|---|---|
| 2026-11-05 | the baseline (D85) | 26,214,809,902 | 2:25:38 | 7,832 s | 890 s | 76,518 |
| 2026-11-05 | `dirlookup` a block at a time, `ls` keeps the last names (D87) | 25,805,258,346 | 2:23:22 | 7,838 s | 751 s | 76,513 |
| 2026-11-05 | `ctype` macros (D89) | 23,480,869,849 | 2:10:27 | 7,062 s | 752 s | 76,515 |
| 2026-11-05 | `putc` into the buffer (D89) | 22,687,214,493 | 2:06:02 | 6,789 s | 762 s | 76,770 |
| 2026-11-05 | `as`: the first character before `strcmp` (D89) | 21,746,096,713 | 2:00:49 | 6,478 s | 759 s | 76,778 |
| 2026-11-05 | `cc`'s lexer: the first character before `strncmp` (D91) | 19,330,720,936 | 1:47:24 | 5,672 s | 761 s | 76,745 |
| 2026-11-05 | `cc`'s `is`: the same (D91) | 18,514,042,450 | 1:42:51 | 5,399 s | 761 s | 76,590 |
| 2026-11-05 | `strlen`, `strcmp`, `strncmp`, `strchr` in assembly (D91) | 17,873,170,257 | 1:39:18 | 5,192 s | 756 s | 76,580 |
| 2026-11-05 | `cc`: integer constants in 32 bits (D93) | 16,963,919,772 | 1:34:15 | 4,887 s | 758 s | 76,474 |
| 2026-11-05 | the clock and scheduler walk only the slots in use (D93) | 16,396,397,709 | 1:31:06 | 4,719 s | 738 s | 76,524 |
| 2026-11-05 | `ialloc` starts at the lowest free inode (D93) | 16,186,841,412 | 1:29:56 | 4,717 s | 671 s | 64,425 |
| 2026-11-05 | `ld` reads only the archive members it takes (D93) | 15,846,258,991 | 1:28:02 | 4,675 s | 598 s | 47,367 |
| 2026-11-05 | `as`: one output buffer, operands from blocks (D93) | 15,011,735,330 | 1:23:24 | 4,396 s | 599 s | 47,328 |
| 2026-11-05 | runs of text in `printf`, `fputs` and `gen.c` (D93) | 14,391,050,348 | 1:19:57 | 4,194 s | 596 s | 47,576 |
| 2026-11-05 | `make` without `sh`, spare page tables (D93) | 14,157,678,312 | 1:18:39 | 4,193 s | 519 s | 46,838 |
| 2026-11-06 | `cc`: token tests inline, one call for a binary expression (D94) | 13,000,164,799 | 1:12:13 | 3,805 s | 522 s | 46,730 |
| 2026-11-06 | `cc`: punctuators by first character, `VPUSH` (D94) | 12,352,459,311 | 1:08:38 | 3,587 s | 524 s | 46,716 |
| 2026-11-06 | `printf`: plain `%s` and `%d` first (D94) | 11,912,110,108 | 1:06:11 | 3,434 s | 530 s | 46,868 |
| 2026-11-06 | `as`: the opcode once, registers, comments, case (D94) | 10,925,984,911 | 1:00:42 | 3,106 s | 530 s | 46,969 |
| 2026-11-06 | `as`: plain numbers skip the evaluator (D94) | 10,396,524,636 | 0:57:46 | 2,931 s | 529 s | 46,930 |

The user programs were 90% of the baseline, and those are mostly `cc` and `as`.
For `sh.c` (1,654 lines, 12,256 of assembly) `cc -c` took 346 s, about 8 lines
of C a second: 203 s to compile and 142 s to assemble. With D89 it takes 277 s,
with D91 233 s, with D93 183 s and with D94 129 s.
The link takes 7.5 s. (`cc -S` is slower, 638 s at the baseline, because it
copies each statement's source line into the assembly, rescanning the file
each time. A build does not use it.)

The profile (`p32uemu -G -q`, `tools/uprof.py`, D88) was flat, with no one
function to blame. In the compile, writing the assembly out as text (`fputc`,
`format`, `strlen`, `put`, `udivmod`, `fputs`, `towrite`) was about a third. In
`as`, the `ctype` functions were 21% and `strcmp` 15%. Those are gone now, and
the largest part left is structural: `cc` turning everything into text (a
quarter of its time) and `as` parsing it back. Together they are about half of
all the compile's time, and `cc` writing objects itself would be a redesign.

### What a program costs before its own code (D92)

A program that does nothing is the floor every command in the benchmark stands
on, and it is far above zero. At 3 MHz, with the code of D91:

| `cc -o x x.c` of | Compile | Assemble | Link | In all |
|---|---|---|---|---|
| one line, no headers | 1.8 s | 1.1 s | 4.6 s | 7.1 s |
| `hello.c` | 4.3 s | 1.2 s | 4.6 s | 9.6 s |
| an empty `main`, seven common headers (185 lines after `cpp`) | 9.5 s | 1.1 s | 4.6 s | 14.8 s |

The last row, times 101, is 25 minutes, a quarter of the benchmark. `cc`'s own
start is 0.13 s of user time: the cost is loading programs, reading headers and
linking. For the smallest command the kernel is 72% of the time. D93 brings the
three rows down to 4.1, 6.6 and 11.6 s, and D94 to 4.2, 5.9 and 9.6 s. Headers,
about 5.5 s a program, are still the largest part, a quarter of that in `malloc`.

Over the whole benchmark the kernel takes **21.4% of the cycles** (3.8 billion
of 17.9), where `time` says 12.7%. `time` counts 50 Hz ticks, and a tick cannot
land in a short trip with interrupts off, such as a TLB refill (`trapvec`, 3.6%).
By function, `uprof.py -k`:

| Function | Share of all cycles | What it is |
|---|---|---|
| `trapvec` | 3.6% | mostly the TLB refill, in software |
| `cf_in` | 3.1% | the disk's sector copy, 2,440 cycles a sector |
| `clockintr` | 2.5% | walks all 64 process slots, 50 times a second |
| `scheduler` | 1.8% | the same walk, at every switch |
| `memset` | 1.7% | zeroing pages |
| `memcpy` | 1.0% | |
| `bget` | 0.9% | the buffer cache's list |

About 90% of the 230,662 sectors read are three things, by the files' sizes and
the count of files made: `sh`, `cc`, `as` and `ld` loaded again for every program
(about half), `libc.a` read whole for every link (a quarter), and `ialloc`, which
scanned the inode table from the start for every file created (a sixth). D93
dealt with the last two: 154,166 sectors now, and 46,838 disk commands, not
76,580. The first is still there: a cache of programs would take most of it.
