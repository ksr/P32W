# P32W Unix — design proposal

**Status:** ACCEPTED (2026-10-13, D22): all eight review points as proposed.
**Progress:** every milestone done, M1 to M7, the last on 2026-10-14
(kernel/README.md). P32W Unix runs in the emulators with about 120
commands (`software/cmd/`) — see [unix.md](unix.md).

**Ken's direction (2026-10-13):** "a multitasking OS where each user is in a
protected area with their own memory space. We will need a filesystem,
system calls into OS and classic unix like commands." Choices made with Ken
the same day:

| Question | Choice |
|---|---|
| Language | **C, with our own compiler** (and linker) |
| OS basis | **Our own, in the style of Unix V6** |
| Users | **Real multi-user:** logins, user IDs, rwx permissions, root; one terminal per serial port |
| Filesystem | **A Unix inode filesystem** on the CompactFlash card, plus a host tool for disk images |

Working name: **P32W Unix** (the kernel `/unix`, as in V6).

---

## 1. What the machine already gives us

The hardware was designed for this (D12–D16, D50, D20):

| Need | Already there |
|---|---|
| Protected processes | user / supervisor modes; user mode cannot run privileged instructions or reach the I/O page or the upper 2 GiB |
| Own memory space per process | a software-managed TLB (64 I + 64 D entries, 4 KiB pages, V/W/X bits), **8-bit address-space IDs**: a context switch is one `MTCR ASID`, no TLB flush |
| System calls | `SYSCALL` traps to the kernel; `RTE` returns; the faulting/trapping instruction has no effect, so page faults can restart |
| Pre-emption | the I/O card's timer (IRQ0); interrupt priorities and masks |
| Terminals | four TL16C550 UARTs with interrupts (IRQ2, IRQ3) on the I/O card |
| Disk | CompactFlash on the disk card, True-IDE, 512-byte sectors (IRQ1) |
| Memory | RAM from `0x0010_0000`, 16 MiB per memory card, one to four cards (up to 64 MiB, D52); 1 MiB boot ROM (the monitor) |
| Boot | the monitor's `b` command loads an image from the CF card and jumps to it |

**Speed:** about 450,000 instructions a second (3 MHz microcycle, ~6.5 per
instruction). That is the class of the PDP-11/40 that ran Unix V6 — with
many times its memory. A context switch is a few hundred instructions, a TLB
refill ~15, so time-sharing two users is comfortable.

The I/O card's battery-backed real-time clock (D23) keeps the date over
power-off.

## 2. The plan in one paragraph

First a **toolchain**: the assembler learns to emit relocatable object files,
a new **linker** joins them into executables, and a new **C compiler**
(`p32cc`, Python like the assembler) turns C into P32W assembly. With it, a
small **C library**. Then the **kernel**, in C with a little assembly:
processes with private address spaces, a scheduler, ~35 V6-style system
calls, a terminal driver for both UARTs, a disk driver and buffer cache, and
an inode **filesystem**. Last, **user programs**: `init`, `getty`/`login`, a
shell, `ed`, and the classic commands. Everything runs first in the
emulators, under `make test`, with scripted terminal sessions; the same disk
image then boots the real machine.

---

## 3. Toolchain

### 3.1 Objects and the linker (`p32asm -c`, `p32ld`)

- The assembler gains **sections** (`.text`, `.data`, `.bss`), `.global` /
  `.extern`, local labels, and **relocatable output** (`-c`, a simple `.o`
  format: sections, symbols, relocations). Three relocation kinds cover the
  ISA: 32-bit absolute (`.word sym`), the `LUI`/`ORI` pair (`LA`, `hi`/`lo`),
  and the 24-bit word offset of `JMP`/`CALL` across files.
- **`p32ld`** links objects and archives (`.a`) into an executable or a
  flat binary (for the kernel), with a link map. Without `-c` the assembler
  still makes flat images, so everything that exists keeps working.

### 3.2 The executable format (`a.out`, V7-like)

```
header (32 bytes): magic 0x50333277 ("P32w"), text size, data size, bss size,
                   entry, flags, symbol table size, 1 reserved
then the text, then the data, then the symbol table (debuggers; exec skips it)
```

The symbol table (2026-10-14; `assembler/p32ld.py` gives the format): every
symbol of every object linked, global and local, sorted by value; `ld -s`
leaves it out.

**User address space** (virtual, 8 MiB per process, D34):

```
0x0000_0000  page 0: never mapped (a NULL pointer faults)
0x0000_1000  text (read + execute, shared between processes running the same program)
             data, bss (read + write)
             heap: grows up with brk/sbrk
     ...     (unmapped gap)
0x0080_0000  top of the stack, growing down (pages added on demand)
```

The page table for 8 MiB is 2,048 entries in two physically contiguous
frames (§4.2). The window can grow with the memory without changing
programs.

### 3.3 The C compiler (`p32cc`)

A one-pass-per-stage compiler in Python: preprocessor → lexer → parser →
type checker → code generator → assembly → `p32asm -c`. No optimiser at
first beyond constant folding and sensible register use.

**The language: C89 without floating point** (P32W has no FPU; V6 Unix needed
none), growing in stages. (Since D30 it has `double` and `long long` too,
in software: [cc/README.md](../cc/README.md).)
1. `int`, `char`, pointers, arrays, functions, `if/while/for/do/break/continue/return`, expressions, globals, string literals.
2. `struct`, `union`, `enum`, `typedef`, `switch`, `goto`, `sizeof`, casts, `static`/`extern`, initialisers, function pointers, `short`, `unsigned`.
3. The preprocessor in full: `#include`, `#define` with arguments, `#if`/`#ifdef`/`#elif`, `#undef`.
4. Variadic functions (`stdarg.h` for `printf`) — done. `long long` (64-bit, via library helpers) — done, D30.

Types: `char` 8 (signed, as on the PDP-11), `short` 16, `int` and `long`
32, pointers 32, `long long` 64. Little-endian.

**The calling convention (ABI)** — a proposal, to fix early:

| Registers | Use |
|---|---|
| r0 | zero |
| r1–r4 | arguments 1–4 (more on the stack); r1 the result (r1:r2 for 64-bit) |
| r1–r7 | caller-saved (scratch) |
| r8–r13 | callee-saved |
| r14 | frame pointer (callee-saved) |
| r15 | stack pointer; `CALL` pushes the return address (D6) |

**Tests:** a C test suite (`cc/tests/*.c`), each with its expected output,
compiled by `p32cc` and run in `p32emu`; programs that are plain C are also
compiled by the host `cc` and must print the same. In `make test`.

### 3.4 The C library

Small and Unix-shaped, in C plus assembly stubs: system-call wrappers
(`read`, `write`, `open`, `fork`, `exec`, …), `string.h`, `ctype.h`,
`stdlib.h` (`malloc`/`free` on `sbrk`, `atoi`, `exit`), a small `stdio`
(`printf` family, `getchar`/`putchar`, `fopen`/`fgets`/`fputs` buffered),
`errno`, `signal`, `setjmp`/`longjmp`, `dirent` for reading directories, and
the 64-bit arithmetic helpers.

---

## 4. The kernel

Written in C (~8,000 lines expected) with assembly for trap entry, the TLB
refill and the context switch (~400 lines). It runs in supervisor mode,
untranslated, at physical addresses.

### 4.1 Physical memory

```
0x0000_0000  ROM: the monitor (loads the kernel)
0x0010_0000  kernel text and data (budget 256 KiB)
             kernel stacks (4 KiB per process), process table, buffer cache (64 KiB)
             page frames: the rest of the RAM, 4 KiB each (the boot
             message says how much: "memory: 16 MiB of RAM, ... KiB free")
0x00FF_FFFF  top of the first memory card (16 MiB; up to 0x03FF_FFFF
             with four cards, D52)
```

### 4.2 Processes and memory

- **Process table**, up to 64 processes: state, PID, parent, user IDs
  (real/effective), ASID, page-table frame, kernel stack, saved registers,
  open files, current directory, signal state, `sleep` channel.
- **Each process has its own address space:** its own page table, 2,048
  entries in two contiguous frames, already in `TLBLO` format (§C7 of isa-system.md), so
  the **TLB-miss handler is one indexed load**, `TLBW`, `RTE`. A missing or
  forbidden page is a real fault: the stack grows on demand, everything else
  kills the process (`SIGSEGV`) — the other users carry on.
- **ASIDs:** each running process gets one of 255; when they run out the TLB
  is flushed and they are handed out again (a rare, cheap event).
- **`fork`** is **copy-on-write** (M6): parent and child share every page,
  writable ones marked read-only, and the first write to one (a `PROTECT`
  fault) copies it. **Text pages are shared** outright between processes
  running the same program. (V6 copied the whole image.)
- **`exec`** reads the `a.out` from the filesystem into fresh pages, sets up
  the arguments on the new stack, and enters user mode with `RTE`.
- **The kernel checks every user pointer** (`copyin`/`copyout` through the
  caller's page table): a system call cannot be used to read or write
  another process's memory or the kernel.
- **Swapping:** not at first. A memory card's 15 MiB of frames holds dozens of
  small processes; a full memory makes `fork` fail with `EAGAIN`. Swapping to the
  CF card can come later if it is ever needed.

### 4.3 Scheduling and sleeping

- **Pre-emptive round-robin** on the timer interrupt (proposal: 50 Hz,
  a 20 ms slice), with V6 `sleep(channel)` / `wakeup(channel)` for waiting
  on the disk, a terminal, a pipe or a child. `WAIT` idles the CPU when
  nothing can run.
- The kernel itself is **not pre-empted** (V6 style): it runs a system call
  to completion or to a `sleep`; interrupt handlers only queue work and wake
  sleepers. Simple and safe on one CPU.

### 4.4 System calls

`SYSCALL` with the number in r1 and arguments in r2–r5 (isa-system.md §8);
the result comes back in r1, and failure as **−errno** (the library turns it
into `−1` and `errno`). The V6 set, with V7 names where clearer:

| Group | Calls |
|---|---|
| Processes | `fork` `exec` (`execve`) `exit` `wait` `getpid` `getppid` `kill` `signal` `pause` `alarm` `nice` `sbrk` |
| Files | `open` `creat` `close` `read` `write` `lseek` `dup` `dup2` `pipe` `stat` `fstat` `ioctl` (terminal modes) |
| Names | `link` `unlink` `mkdir` `rmdir` `chdir` `mknod` `chmod` `chown` `access` `rename` |
| Users | `getuid` `geteuid` `getgid` `setuid` `setgid` `umask` |
| Time, system | `time` `stime` `times` `sync` `uname` |

**Signals,** V7's fifteen, with V7's numbers: `SIGHUP`, `SIGINT` (^C),
`SIGQUIT` (^\), `SIGILL`, `SIGTRAP`, `SIGABRT`, `SIGEMT`, `SIGFPE`,
`SIGKILL`, `SIGBUS`, `SIGSEGV`, `SIGSYS`, `SIGPIPE`, `SIGALRM`, `SIGTERM`
(no `SIGCHLD`: a parent learns of its children through `wait`). A handler
runs in user mode through a small trampoline on the user stack; a caught
signal resets to the default action, as in V7.

### 4.5 Devices

| Device | Driver |
|---|---|
| `/dev/tty0`, `/dev/tty1` | UART 0 (the console) and UART 1, interrupt-driven, with a line discipline: echo, erase (backspace / DEL), line kill (^U), ^C → `SIGINT`, ^\ → `SIGQUIT`, ^D end of file, a raw mode for programs that want characters |
| `/dev/disk` | the CompactFlash card's filesystem area: a block driver (PIO, 512-byte sectors) under a **buffer cache** (V6 `bread`/`bwrite`, write-back, flushed by `sync` and every 30 s) |
| `/dev/rdisk` | the card raw, by its own sector numbers: reads and writes go through one sector buffer, bypassing the cache |
| `/dev/ram` | a 256 KiB RAM disk (block), for a scratch filesystem (`mkfs`, `mount`) |
| `/dev/null` | discards writes; reads give end of file |
| `/dev/mem` (root only) | physical memory: ROM and RAM by physical address |
| `/dev/port` | the ports on the I/O card: the 8-bit input and output ports |
| `/dev/tty` (character 4) | the process's controlling terminal, whichever it is |
| `/dev/video` (character 7) | the video card's HD63484 for a program's own drawing: the chip's command words written, what a command read back read; one program at a time, the console's text kept and drawn again when it closes (D110, `video(4)`) |

### 4.6 Boot

1. The monitor's `b` loads the kernel from the boot area at the start of the
   card (the existing "P32B" header) and jumps to it.
2. The kernel sets up memory, the TLB and the devices, mounts the root
   filesystem, and starts **`/etc/init`** as process 1.
3. `init` runs `/etc/rc` (checks the filesystem, cleans `/tmp`), then a
   **`getty`** on each terminal; `getty` runs **`login`**, `login` checks
   `/etc/passwd`, sets the user's IDs and directory, and runs their shell.
4. `shutdown` / `sync` before power-off (no journal, as in V6: §5.4).

---

## 5. The filesystem

A V7-style inode filesystem, sized for a CF card, with names longer than
V7's 14 characters.

### 5.1 Layout

**Blocks of 1 KiB** (two sectors). The card holds:

```
sectors 0..N   boot area: "P32B" header + the kernel image (the monitor boots this)
then block 0   superblock: magic, size, inode count, the free-block bitmap's place, clean flag
     block 1.. the inode table (64-byte inodes, 16 per block)
     then      the free-block bitmap (one bit per block)
     then      data blocks
```

**Inode (64 bytes):** type and permissions (16 bits: file / directory /
character / block device / pipe, set-user-ID, set-group-ID, sticky, rwx ×
3), link count, owner uid and gid, size, modification and change times,
**11 block numbers: 8 direct, 1 single-, 1 double-, 1 triple-indirect**
(files to 2 GiB: the triple block — the former padding word, 2026-10-14 —
reaches 16 GiB, the signed 32-bit file offset stops at 2 GiB).

**Capacity** (Ken: "we will want a bigger cf capacity"): the CF driver
addresses 2²⁸ sectors (LBA28, 128 GiB); block numbers are 32-bit. The
kernel's block allocator starts each search in the bitmap block where the
last one ended and steps over full bytes, so a large disk is not scanned
from its start every time. `mkfs` gives one inode per 8 blocks, at most
512 Ki inodes, so the on-machine `fsck` (one bit per block, three bytes per
inode) checks a filesystem of several GiB within a process's address space.
Still to do for very large cards: LBA48 (past 128 GiB), mount and
partitions (several filesystems on one card).

**Directory entries (32 bytes):** inode number (32 bits) + a name of up to
28 characters. `.` and `..` in every directory.

### 5.2 What it supports

Hard links, `mkdir`/`rmdir`, `rename`, device files (`mknod`), pipes,
permissions with root override, set-user-ID programs (`passwd`, `su`),
`umask`, and **mounting** a second filesystem (`mount`, `umount`; for
example the RAM disk on `/mnt`).

### 5.3 The host tool `p32fs`

A Python tool, so disk images are built from the repository by `make`:
`p32fs mkfs IMAGE SIZE`, `p32fs put IMAGE host-file /path`, `p32fs get`,
`ls`, `mkdir`, `chmod`, `chown`, **`fsck`** (checks every invariant: free map
vs blocks in use, link counts vs directory entries, sizes vs blocks), and
`p32fs build IMAGE tree/` to make a complete root filesystem (kernel in the
boot area, `/bin`, `/etc`, `/dev`, `/usr`, `/tmp`, home directories) in one
step. The same image boots in the emulator (`-d`) and is written to a real
card with `dd`.

### 5.4 Crash safety

V6 style: writes are cached and flushed by `sync`, every 30 seconds and at
`shutdown`; at every boot `/etc/rc` runs `fsck -q` (on the machine — a C
port of the host checker) before going multi-user. `fsck -y` repairs; after
repairing the mounted root it restarts the machine without writing the
cache back (D28), as V7's fsck did. A journal (as xv6's log) is
possible later, but adds complexity the first version does not need.

---

## 6. Users and security

- **`/etc/passwd`**: `name:password-hash:uid:gid:full name:home:shell`;
  **`/etc/group`**. **root** is uid 0 and bypasses permission checks.
- Every process carries a **real and effective uid/gid**; files carry an
  owner, a group and rwx bits for owner / group / others. The kernel checks
  them in `open`, `exec`, `chdir`, directory searches, `unlink`, `chmod`,
  `chown` (root only), `kill` (own processes, or root), `setuid` (root, or
  back to the real uid).
- **Memory protection** comes from the address spaces (§4.2): one user's
  processes cannot see another's memory, the kernel's, or the devices.
- **Passwords** are stored as a salted hash. P32W has no fast crypto, so the
  proposal is a deliberately simple keyed hash (several thousand rounds of a
  32-bit mixing function, a 16-bit salt): fine for a hobby machine on two
  serial lines, not a defence against a stolen disk. `/etc/passwd` can be
  readable (as in V7) or the hashes moved to a root-only `/etc/shadow`.

---

## 7. The commands

**Phase 1 — a usable system:**
`init` `getty` `login` `sh` `ls` `cat` `echo` `cp` `mv` `rm` `ln` `mkdir`
`rmdir` `pwd` `chmod` `chown` `chgrp` `ps` `kill` `wc` `grep` `head` `tail`
`sync` `date` `who` `passwd` `su` `ed` `mknod` `stty` `true` `false`
`sleep` `shutdown`

**Phase 2:** `more` `sort` `uniq` `od` `diff` `cmp` `find` `du` `df` `tee`
`tr` `cut` `test`/`[` `expr` `write` (to another user's terminal) `mail`
`wall` `fsck` `mount`/`umount` `cc`? (a native compiler is far off; programs
are compiled on the host).

*Status (2026-10-14):* both phases are built, and much more (about 85
commands, with `vi`, `make`, `basic`, `sql`, `sed`, `bc` and a native `cc`
since M7), except `chgrp`, `mknod` (the system call is there, the command
is not), `write`, `mail` and `wall`.

**The shell** (`sh`): V6/Thompson-shell features first — commands with
arguments, `<` `>` `>>` redirection, pipelines `|`, `;`, background `&`,
`cd`, `exit`, `$PATH` search (`/bin`, `/usr/bin`), wildcards `* ? [...]`;
later variables, `if`/`while`, and scripts (Bourne-style subset). *Status:*
all of that and more is in: functions, `case`, here-documents, subshells,
`#!` scripts, history and Tab completion (`sh(1)`).

**`ed`** is the editor: it needs no screen control and works on a serial
terminal, as on V6.

---

## 8. Emulator support

- `p32emu` (instruction level, fast) is the development platform; the
  microcode-level `p32uemu` checks the same images more slowly.
- **Two terminals in the emulator:** UART 0 on the emulator's own terminal,
  UART 1 on a pseudo-terminal or a TCP port (`-2 tcp:2323`), so two people
  can log in at once on the Mac.
- **Scripted sessions for `make test`:** feed input to each UART from a file
  (with waits for prompts), record the output, compare it with a recorded
  transcript — the way `monitor.expect` works today.
- A per-process trace (system calls, faults, context switches) for
  debugging.

## 9. How it is tested

| Layer | Test (in `make test`) |
|---|---|
| Assembler objects, linker | link tests: cross-file calls and data, relocations at the limits, archives |
| C compiler | the C test suite against expected output and the host compiler |
| C library | unit programs in `p32emu` (strings, printf, malloc) |
| Kernel | scripted sessions on a fresh image: boot, login on both terminals, the commands, pipes, background jobs, permission refusals, a program that faults is killed while the others run, `fork` until memory is full and recover, `fsck` clean afterwards |
| Filesystem | `p32fs fsck` after every session; the kernel and the host tool must agree on every image |
| The real machine | the same image on a CF card; booting to `login:` is the bring-up goal |

## 10. Effort and phases

| Phase | Delivers | Size (rough) |
|---|---|---|
| **M1** | relocatable assembler, linker, `a.out` | ~1,000 lines Python |
| **M2** | `p32cc` (stages 1–3) and its test suite; the C library | ~5,000 lines Python, ~2,500 C |
| **M3** | kernel I: boot, console, processes, `fork`/`exec`/`wait`, the filesystem read path, `p32fs`, a shell on one terminal | ~5,000 lines C |
| **M4** | kernel II: writing files, pipes, signals, both terminals, users and permissions, `init`/`getty`/`login` | ~3,000 lines C |
| **M5** | the phase-1 commands, `ed`, the on-machine `fsck`; manual pages | ~6,000 lines C |
| **M6** | copy-on-write `fork`, shared text, phase-2 commands, mount | done |
| **M7** | **the native toolchain** (D24): `as`, `ld`, `ar` and `cc` in C, running on P32W Unix; `cc` compiles itself there | ~8,000 lines C |

### M7 — the native toolchain (D24)

Programs are built on the host today (`cc/p32cc.py`, `assembler/`). M7 puts
the toolchain on the machine, written in C and compiled by `p32cc`:

1. **`as`** — the assembler, reading the same syntax as `p32asm.py` and
   writing relocatable objects; **`ld`** and **`ar`** — the linker and
   archiver, with the same layout rules as `p32ld.py` (a.out, data on the
   next page). Objects and archives get a compact **binary** format (the
   host's JSON is too bulky to parse on a 0.45 MIPS machine); the host tools
   learn to read and write it too, so either side can link the other's
   output.
2. **`cc`** — a C rewrite of `p32cc`, with the same design (token
   preprocessor, recursive-descent parser, the same code generator and
   ABI) so one test suite checks both; a driver runs `cpp` → `cc1` → `as`
   → `ld` with `/usr/include` and `/usr/lib/libc.a` on the disk.
3. **The test:** the host and native tools produce byte-identical objects
   and executables for every program in `cc/tests/` and the library; then
   **self-hosting** — `cc` compiles its own source on P32W Unix and the
   result is identical to the host-built `cc`.

Before it: keeping scalar locals in callee-saved registers in the code
generator (both compilers), since compile time on the machine depends on
the quality of the compiler's own code. **Done in `p32cc` (2026-10-14):**
up to four scalar locals or parameters whose address is never taken (the
most used, uses inside loops weighted ×8, at least 3) live in r13–r10;
reads use the register in place, an assignment's result is not copied when
unused, and a function that then runs short of temporaries is generated
again with fewer. Session 1 (boot, login, 31 commands, halt): 50.3 M →
35.9 M instructions (−29 %); the kernel 62,344 → 57,172 bytes.
`P32CC_NOREGVARS=1` turns it off, for comparisons. The C `cc` (step 3)
does the same.

**Step 1 (as / ld / ar) done (2026-10-14):** objects and archives are binary
(the format is specified in `assembler/p32obj.py`; the host tools write it
and still read M1's JSON; `p32obj.py dump` shows one as JSON).
`software/cmd/as.c`, `ld.c` and `ar.c` (with `objfmt.h`) are the host tools
in C; the ISA table they use (`software/include/p32isa.h`) is generated
from `p32isa.py` like `p32opc.h`. Checks: built on the Mac, the C tools give
byte-identical output to the Python ones for all 104 assembly files of the
system (libc, commands, kernel, tests), all 62 command links and libc.a;
on P32W Unix (`software/tests/toolchain_test.sh`, in `make test`) they
assemble, link and archive compiler output identically to the host, and
the program runs. Speed on P32W (emulated time): `as` of `sort.s` (≈ 900
lines) 4.2 s, `ld` of `echo` against libc 5.2 s. (p32cc does not yet
return structs from functions; `as.c` passes results by pointer.)

**Steps 2 and 3 done (2026-10-14): M7 is complete — P32W is
self-hosting.** `software/cc` (`lex.c cpp.c type.c parse.c gen.c main.c`,
≈ 3,300 lines with `cc.h`) is `p32cc` in C, stage for stage; built on the
Mac it writes the same assembly as `p32cc` for all 106 C files of the
system and the same `-E` text. On P32W Unix `cc` (with `/usr/include`,
`/usr/lib/crt0.o`, `/usr/lib/libc.a`) compiles, assembles (`as`) and links
(`ld`) programs that run, and **compiles its own source into a `cc`
byte for byte identical to the host-built one** (`software/tests/cc_test.sh`,
in `make test`). Each `.c` file is compiled in a process of its own (as
`cc1`), so each starts with a whole address space: the largest, `parse.c`, needed
3.1 MB of heap (1.6 MB since D29: each function is generated as soon as it
is parsed, and its tree and tokens freed). Self-hosting costs about 1.0 × 10⁹ instructions: 16 minutes
at the emulators' 1 MIPS, about half an hour on the hardware.

Each phase ends with `make test` green and a push.

## 11. For review (D22)

1. **The ABI** (§3.3): arguments in r1–r4, result in r1, r8–r13 + r14
   callee-saved, `char` signed.
2. **The user address space:** 8 MiB per process (D34) with a two-frame
   page table and a one-load TLB refill; page 0 unmapped.
3. **The C subset:** C89 without floating point, staged as in §3.3.
4. **Kernel model:** V6 — non-pre-emptive kernel, `sleep`/`wakeup`, copy on
   `fork` first (copy-on-write later), no swapping.
5. **Filesystem:** 1 KiB blocks, 64-byte inodes with 8 direct + single +
   double indirect, a free-block bitmap, 28-character names, no journal
   (`sync` + `fsck`).
6. **Users:** V7 passwd/group files, real/effective IDs, setuid programs, a
   simple salted hash for passwords.
7. **Timer at 50 Hz** for the scheduler and the clock; no real-time clock
   for now (`date` is set by hand, or by a later RTC).
8. **Start with M1 + M2** (the toolchain): nothing in the OS can start until
   C works.

(This list is the proposal as reviewed. Since then: copy-on-write `fork`
and shared text are in (M6); a real-time clock is on the I/O card (D23), so
the time survives power-off; the signal list is V7's, without `SIGCHLD`.)
