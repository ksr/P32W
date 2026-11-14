# P32W Unix internals

**Status:** DESCRIBES THE KERNEL AS BUILT (2026-10-26), `kernel/`, about
6,100 lines of C and 220 of assembly. The design it grew from is
`os-design.md` (D22, written before the code); this chapter is what the
code does. Its companions: [the boot sequence](boot.md),
[the filesystem](filesystem.md), [using P32W Unix](unix.md).

P32W Unix is a **Unix V6/V7-style kernel**: one address space per process,
processes made by `fork` and changed by `exec`, everything a file, a
non-pre-emptive kernel that sleeps on events. The hardware's MMU (a
software-refilled TLB with address-space IDs, D13) shapes its memory
code, and its I/O cards shape its drivers.

## 1. Where things are

| File | What |
|---|---|
| `start.s` | the entry point; the trap entry and exit; the TLB-miss fast path; `swtch`; the privileged instructions as C functions |
| `main.c` | `kmain`: start-up, in order ([boot](boot.md) §5) |
| `vm.c` | physical frames, page tables, copy-on-write, the lazy heap, the growing stack, user-memory access |
| `proc.c` | processes, the scheduler, sleep and wakeup, fork / exit / wait, signals |
| `trap.c` | traps and interrupts that `start.s` does not finish; the clock |
| `syscall.c` | the system calls |
| `exec.c` | loading a program |
| `fs.c`, `bio.c` | the filesystem and the buffer cache ([filesystem](filesystem.md)) |
| `file.c` | open files, pipes, the device switches, `/dev/mem`, the raw disk |
| `cf.c`, `ramdisk.c`, `usb.c` | the block devices: the disk card's drives, a RAM disk, USB drives |
| `tty.c`, `video.c` | terminals: the UARTs, the video console, pseudo-terminals |
| `net.c` | sockets, through the network coprocessor |
| `rtc.c` | the real-time clock |
| `kprintf.c`, `klib.c` | console messages, `panic`, string routines |

**Limits** (`param.h`):

| | |
|---|---|
| processes | 64 (`NPROC`); slot *k* has ASID *k* + 1 |
| open files | 20 per process, 128 in the system |
| in-memory inodes | 96 |
| buffer cache | 48 blocks of 1 KiB |
| clock | 50 Hz (`HZ`); a time slice is 2 ticks (40 ms) |
| user address space | 8 MiB (`USERTOP`, D34); the stack up to 1 MiB (D41) |
| exec arguments | 4 KiB of strings, 250 arguments and 250 environment strings |

## 2. Memory

### 2.1 Physical memory

The kernel is loaded at `0x0010_0000`. It runs in supervisor mode, where
addresses are **physical** (isa-system.md C2), so it reads and writes any
RAM or device register directly.

At start-up `kinit` probes every 2 MiB bank of the four memory windows
(D60), and puts every free 4 KiB **frame** above the kernel's end, in the
banks it found, on a free list. Each frame on the list holds the address of
the next. Frames in missing banks are marked in use for good, so nothing
ever takes them. A byte per frame (`fref`) counts how many address spaces
share it (§2.4).

- `frame_alloc` / `frame_free`: one zeroed frame / give one back (or drop
  one reference to a shared one).
- **Page tables** need two *contiguous* frames (below), so `pt_new`
  searches the reference counts for two free frames side by side and then
  takes them off the free list.

### 2.2 A process's address space

| User addresses | What | Permissions |
|---|---|---|
| `0x0000_0000`–`0x0000_0FFF` | never mapped: a null pointer faults | — |
| `0x0000_1000`– | **text** (the program's code) | read + execute |
| next page – | **data**, then **bss** (zeroed), then the **heap** up to the break | read + write |
| … | unmapped: the heap grows up, the stack down | |
| up to `0x0080_0000` | **the stack**, growing down on demand, at most 1 MiB | read + write |

Addresses `0x8000_0000` and above trap in user mode, and so do the device
registers. User programs reach devices only through the kernel.

### 2.3 Page tables and the TLB

The MMU card has a **split TLB**, 64 instruction and 64 data entries, each
tagged with an 8-bit **ASID**. Software fills it (D13): a miss is a trap.

Each process has **one page table**: 2,048 entries, one per 4 KiB page of
its 8 MiB, each **in `TLBLO` format** (frame number, X, W, V, plus the
software bit `PTE_COW`, which the TLB ignores). The table fills two
physically contiguous frames, so the entry for virtual page *n* is simply
`pt[n]`.

**The refill fast path** (`trapvec` in `start.s`): on a TLB miss (causes
8–10) the handler saves only r12 and r13, takes the page number from
`BADADDR`, loads `pt[n]` from the current table (`curpt`), and if it is
valid writes it with `TLBW` (TLBHI was already loaded by the miss) and
returns with `RTE`. That is about 20 instructions and one memory load. A
miss on an unmapped page goes the slow way, to `trap()` (§3), which may
grow the heap or the stack, or deliver `SIGSEGV`.

**Address-space IDs.** Process slot *k* always uses ASID *k* + 1, and the
TLB matches only entries of the current ASID. A context switch is just
`MTCR ASID` and a new `curpt`: no flush. The whole TLB is flushed
(`tlbflush`) only when a mapping is removed or changed (`munmap`-like
shrinking of the break, exec, fork's write-protection) or a slot is reused
by a new process.

### 2.4 fork: copy-on-write

`pt_copy` gives the child a page table pointing at **the same frames**:
- read-only pages (text) are simply shared;
- writable pages become read-only with `PTE_COW` set, **in both
  processes**, and the frame's reference count goes up.

The first write to such a page is a `PROTECT` fault. `cow_break` copies the
frame for the writer (or, if it is the last sharer, just makes it writable
again) and writes the new TLB entry directly. The kernel's own writes into
user memory (`copyout`) break copy-on-write the same way. A `fork`
followed by `exec`, the common case, therefore copies almost nothing.

### 2.5 The lazy heap and the growing stack

- **`sbrk`** only *promises* pages: it raises the break and **reserves** a
  frame for each new page (`heap_reserve`), so that no other allocation can
  take them. The page is mapped (zeroed) on its first touch: a TLB-miss
  fault in the heap, or the kernel reaching it with `copyin` / `copyout`. A
  program that asks for 4 MiB and uses 100 KiB costs 100 KiB, yet `sbrk`
  still fails at once, as it should, when memory is short.
- **The stack** grows on demand: a fault below the lowest stack page but
  within 1 MiB of the top (and above the heap) maps the pages down to it
  (`grow_stack`).
- **Statistics:** each process counts its TLB refills, copy-on-write
  copies and stack pages (and its children's when they are reaped), for
  `ps`, `top` and `time`.

### 2.6 Reaching user memory from the kernel

The kernel never trusts a user pointer. `copyin`, `copyout`, `copyinstr`
and `user_ok` translate each page through the process's page table
(`va2pa`), growing the heap or stack or breaking copy-on-write as a user
access would, and return `-EFAULT` for an address the process could not
reach itself.

## 3. Traps and interrupts

**Entry.** Every exception, `SYSCALL` and interrupt goes to `VBR =
trapvec`, in supervisor mode with interrupts off. r15 is banked (D6), so
`sp` is already the supervisor stack: the current process's **kernel
stack**, one 4 KiB frame per process. Apart from the TLB-miss fast path,
`trapvec` builds a **trap frame** on it (r1–r14, the user `sp`, `EPC`,
`STATUS`, `CAUSE`, `BADADDR`: 80 bytes) and calls `trap()`.

**`trap()`** (`trap.c`) sorts it out:

| What | Handling |
|---|---|
| an interrupt (`CAUSE` bit 31) | dispatched by the **vector** the card put in `CAUSE` bits 15:8 (D57): window 7, device 0 the clock; 1–4 the UARTs; 8 the network; window 5 the keyboard. No vector (`0xFF`): by the level, asking each device on it. |
| a bus error while `probing` | `kinit`'s memory probe: skip the access |
| anything else in the kernel | `panic("kernel trap")` |
| `SYSCALL` from user mode | `syscall()` (§5), with interrupts on |
| a TLB miss in the heap or below the stack | a heap page made, or the stack grown; `RTE` retries the access |
| `PROTECT` on a copy-on-write page | the page copied; `RTE` retries |
| any other user fault | a signal: `SIGILL` (illegal or privileged instruction), `SIGTRAP` (`BREAK`), `SIGBUS` (misaligned, bus error), `SIGSEGV` (the rest), with a console message unless the program catches it |

**On the way back to user mode** `trap()` writes delayed disk blocks if the
30-second flush is due, delivers a pending signal, and gives up the CPU if
the clock asked for a reschedule. Then `trapret` restores the frame, and
`RTE` returns to the user program.

**The clock** (`clockintr`, 50 Hz, the I/O card's timer reloading 20,000
microseconds): it acknowledges the timer and counts `ticks`. It charges
the tick to the interrupted process (user or system time), asks for a
reschedule after 2 ticks, fires `alarm` signals, and polls the network.
Every 5 s it updates the load average (classic Unix's 1-, 5- and
15-minute exponential averages, 11-bit fixed point), and every 30 s it
sets the flush flag.

## 4. Processes

### 4.1 The process table (`proc.h`)

A `struct proc` per slot holds:
- the state: `UNUSED`, `EMBRYO`, `SLEEPING`, `RUNNABLE`, `RUNNING`,
  `ZOMBIE`, `STOPPED`;
- the pid and parent, the ASID, the page table, the break, the stack
  bottom, the text end;
- the kernel stack, its trap frame and its saved **context** (the
  callee-saved registers r8–r14, `sp`, the return address);
- 20 open-file slots, the current directory, the user and group IDs (real
  and effective), the umask and the controlling terminal;
- signals (a pending bitmask, a handler per signal), the alarm, the CPU
  times, the ptrace state.

### 4.2 Scheduling: not pre-emptive in the kernel

The kernel follows V6: **a process running in the kernel keeps the CPU
until it sleeps** or returns to user mode. The clock never switches
processes in the middle of kernel code; it only sets `need_resched`, and
the switch happens in `trap()` on the way out. So kernel data needs no
locks between processes, only protection from interrupt handlers.
`splhi()` turns interrupts off and `splx()` restores them, around the few
structures an interrupt handler also touches (terminal queues, the buffer
list, the process table).

**The scheduler** runs in its own context on the boot stack. It loops over
the table **round-robin** from where it last stopped, picks the next
RUNNABLE process, loads its ASID and page table (`switchuvm`), and calls
`swtch(&schedctx, &p->ctx)`. When nothing is runnable it turns interrupts
on and executes **`WAIT`**: the CPU idles until an interrupt (D11).

**`swtch`** saves the callee-saved registers, `sp` and the return address
in one context and loads another. A process gives up the CPU with
`sched()`: `yield` (its slice is over: RUNNABLE), `sleep` (SLEEPING) or
`exit` (ZOMBIE). It resumes inside its own `sched()` call when the
scheduler picks it again.

### 4.3 Sleep and wakeup

`sleep(chan)` marks the process SLEEPING on an address (a buffer, an
inode, a tty queue, a child's parent) and switches away. `wakeup(chan)`
makes every process sleeping on that address RUNNABLE. The sleeper checks
its condition again when it runs, because another process may have got
there first. Interrupts are off from the caller's test of its condition
until the process is asleep, so a wakeup from an interrupt handler cannot
fall between the two. A signal ends a sleep (`psignal` makes the process
RUNNABLE), so slow calls return `EINTR`.

### 4.4 fork, exit, wait

- **`fork`**: a new slot and pid, a kernel stack with a copy of the
  parent's trap frame (r1 = 0 in the child), a copy-on-write page table
  (§2.4), the open files shared (reference counts), the same directory,
  IDs, umask, terminal and signal handlers. Its context starts in
  `trapret`, so the child's first run is a return to user mode.
- **`exit`**: closes the files, releases the directory, gives orphans to
  init, becomes a ZOMBIE holding its status, wakes its parent.
- **`wait`**: finds a ZOMBIE child, adds its CPU times and memory
  statistics to the parent's, frees its page table, frames and kernel
  stack, flushes the TLB (its ASID will be reused) and frees the slot. With
  no zombie it sleeps; with no children it returns `ECHILD`. A traced child
  that stopped is reported too.

**Process 1** is built by hand at start-up ([boot](boot.md) §5.3).

### 4.5 exec (`exec.c`)

1. Look up the file. It must be a regular file the process may execute, and
   start with the a.out header: magic `0x5033_3277`, text size, data size,
   bss size, entry point (`assembler/p32ld.py` writes it).
2. Copy the arguments and environment into kernel frames *before*
   touching the old program, because they live in the old address space.
3. Build a **new** page table: text pages read + execute from `0x1000`,
   data and bss read + write from the next page boundary. The contents
   come straight from the disk into the pages (`readi_pages`: runs of
   consecutive blocks in one ATA command, past the buffer cache). The
   stack's top page holds the strings, then `argc`, `argv[]`, NULL,
   `envp[]`, NULL.
4. Only now **commit**: free the old page table, switch to the new one,
   flush the TLB, set the break after the bss, apply a set-user-ID or
   set-group-ID bit, reset caught signals to their defaults, and point the
   trap frame at the entry with the new stack. An exec that fails at any
   earlier step leaves the old program untouched, and the call returns an
   error to it.

### 4.6 Signals

V7's numbers and semantics: a caught signal reverts to the default when it
is delivered (`signal` must be called again). Pending signals are a
bitmask. `kill` (by root, or by a process of the same user) and the
terminal (^C `SIGINT`, ^\ `SIGQUIT`, hang-up `SIGHUP`) post them with
`psignal`, which also ends an interruptible sleep.

**Delivery** (`issig`, on the way back to user mode): `SIGKILL` first,
then the lowest pending.
- Ignored: nothing happens.
- Default: the process exits with the signal number as its status.
- Caught: the kernel copies the whole trap frame onto the **user stack**,
  points `EPC` at the C library's trampoline (`__sigtramp`, registered as
  handler 0), with r1 = the signal and r2 = the handler. The trampoline
  calls the handler, then calls `sigreturn`, which copies the frame back:
  every register as it was when the signal arrived.

**ptrace** (for `db`, the debugger): a traced process stops at every signal
and at exec. Its parent sees the stop through `wait`, can read and write
its memory and registers (`pt_private` gives it a private copy of a shared
text page for a breakpoint), and lets it continue with or without the
signal.

## 5. System calls

A program executes **`SYSCALL`** with the call's number in r1 and up to
four arguments in r2–r5. The trap frame holds them, `syscall()` advances
`EPC` past the instruction, calls the handler from the table, and puts the
result in the frame's r1: **a value ≥ 0, or −errno**. Every other register
is preserved (the C library turns −errno into −1 and `errno`). A call that
sleeps simply sleeps inside the handler: the kernel stack keeps its state.

The numbers follow V7 (`software/include/sys/syscall.h`): 1 `exit`, 2
`fork`, 3 `read`, 4 `write`, 5 `open`, 6 `close`, 7 `wait`, 8 `creat`, 9
`link`, 10 `unlink`, 11 `execve`, 12 `chdir`, … 17 `sbrk`, 18 `stat`, 19
`lseek`, … 48 `signal`, 54 `ioctl`, … then P32W's additions: `mkdir`,
`rmdir`, `rename`, `dup2`, `uname`, `sigreturn`, `reboot`, `pinfo` (one
process-table slot, for `ps` and `top`: there is no `/dev/kmem`),
`setsid`, `sysinfo`, `ustat`, `fcntl`, `select`; the 4.2BSD socket calls
at their BSD numbers (`socket` 97, `connect` 98, … D45); and `netctl`
(160).

## 6. Files

- **The open-file table** (128 entries): an open file is a type (an
  inode, a pipe or a socket), a reference count, the open flags and the
  offset. `fork` and `dup` share an entry, so the offset is shared, as in
  Unix.
- **Pipes**: a 1 KiB ring in a frame, with reader and writer counts.
  Readers sleep while it is empty, writers while it is full. Writing with
  no reader left is `SIGPIPE`.
- **Device files** go through two switches, Unix V6's: **block devices**
  (`bdevsw`: open, close, *strategy*, which transfers one buffer) for
  anything a filesystem can live on, and **character devices** (`cdevsw`:
  open, close, read, write, ioctl) for the rest. The table of devices is in
  [the filesystem](filesystem.md) §8.
- **The filesystem** itself: [its own chapter](filesystem.md).

## 7. Terminals (`tty.c`)

Nine terminals share one line discipline: the four UARTs (`tty0`–`tty3`),
the video console (`tty4`, D53) and four pseudo-terminals (`ttyp0`–`ttyp3`,
D46).

- **Input** arrives by interrupt (a UART's received-data interrupt, the
  keyboard, or a pty master's write) and goes through `input()`: echo,
  erase (^H or DEL), line kill (^U), ^C → `SIGINT` and ^\ → `SIGQUIT` to
  every process on the terminal, ^D end of file, CR → LF. The *raw* queue
  holds the line being typed. Return moves it to the *canonical* queue,
  which `read` takes from. **CBREAK** delivers each character at once,
  with the signals still working. **RAW** passes everything through as
  typed. Modes are set with `ioctl(TIOCSETP)` (`stty`), as in V7.
- **Output** goes to a 4 KiB queue, and the UART's transmit-empty interrupt
  drains it. A writer sleeps while the queue is full. LF goes out as CR LF
  (CRMOD).
- **The controlling terminal** is the first terminal a process opens while
  it has none: `setsid` (getty's parent, init, calls it) clears it, so the
  terminal getty opens becomes its session's, and children inherit it.
  `/dev/tty` is always "this process's terminal".
- **Pseudo-terminals:** a slave (`/dev/ttypN`) is an ordinary terminal to
  the programs on it. Its master (`/dev/ptypN`), held by `telnetd`, writes
  the slave's typing and reads its output. Closing the master hangs up the
  slave.

## 8. Other drivers

- **The disk card** (`cf.c`): ATA PIO, polled, 16-bit data (D26). The
  boot CompactFlash card's filesystem area is minor 0. The second CF socket
  and the four IDE drives are minors 1–5, probed at start-up. A 1 KiB block
  is one two-sector command.
- **USB** (`usb.c`, D43): the I/O card's two CH376S controllers in sector
  mode, polled. A drive is found when first opened (connect, `DISK_INIT`,
  capacity). Pulled out, the next access fails with `EIO` and its cached
  blocks are dropped. Every wait has a time limit.
- **The network** (`net.c`, D45): sockets as file descriptors and the BSD
  calls, with no protocol code in P32W. Each operation is a request and a
  reply through the coprocessor's mailbox (docs/net-mailbox.md), byte by
  byte and polled. The kernel is not pre-empted, so two processes never
  interleave on the mailbox. A socket that must wait sleeps until the
  coprocessor's `ATTN` interrupt (IRQ7), or a 200 ms tick, and looks again.
  `select` covers terminals, pipes and sockets.
- **The clock** (`rtc.c`, D23): the DS12C887, read at boot and set by
  `stime` (binary mode, 24-hour, UTC).
- **The video console** (`video.c`, D53), when the video card is fitted:
  the screen as an ANSI-style terminal, drawn by the card's HD63484 from
  commands (D103, D104). A character is one graphic copy from a picture of
  the font; a scroll is one copy by the chip. The command words wait in a
  ring and go to the chip eight at a time from its "write FIFO empty"
  interrupt, so nothing spins while it draws; kernel messages wait for it.
  The keyboard's characters come as ASCII from the card's PS/2 section.
  That section is an ATmega328P with a will of its own (D109): two seconds
  starting after a reset, and a command written before it has taken the
  last is lost. So the three commands that start it (interrupts off, which
  may be dropped; the mouse off; interrupts on) go out from the clock's
  tick, one each time the card shows BUSY clear (`kbdtick`), and the
  kernel never waits for it. An interrupt brings one byte.
- **`/dev/video`** (D110, character device 7; `video(4)`) lends the chip to
  one program. What the program writes are the chip's command words, as
  they are: they go into the same ring behind the console's, the writer
  sleeping while it is full. What it reads are the words a command put in
  the chip's read FIFO; the reader sleeps on the chip's "read FIFO ready"
  and "command end" interrupts, which are on only while someone waits.
  **While the device is open the console draws nothing**: its text, and
  the kernel's messages, go into `vtext` only, a copy of the 25 rows that
  the driver keeps all the time (a word a cell: the character and the
  font's picture; a scroll turns its rows round and moves nothing).
  **Closing it, or the program's end, gives the screen back**: the words
  not yet in the chip are dropped, the chip's command in hand is aborted,
  its drawing registers, its pattern and the font's eight pictures are
  made again (a program may write anywhere in the chip's memory), and the
  text is drawn again from `vtext`, with what was written meanwhile.
  Nothing a program writes can outlast it, so the driver does not look at
  the words at all. The memory under the font's pictures (2,412 lines of
  the screen's width) is the program's own meanwhile, and is cleared with
  the rest when the console takes the chip back: `VIOCSPARE` says where
  it is (D119).
- **`/dev/mouse`** (D120; `mouse(4)`): the card's PS/2 mouse, for one
  program at a time. The console keeps it off; an open turns it on (the
  PS/2 section is told from the clock's tick, as at the start, so the
  kernel never waits for it) and a close off again. Its packets, three
  bytes each as the mouse sends them, are kept (64 of them) for `read`,
  which gives whole ones; `select` knows whether one is waiting.
- **Pseudo-terminals are 16** (D122; they were 4): 0 to 3 for logins from
  the network (`telnetd`), 4 to 15 for the windows of `wm`, whose nodes
  are root and group staff as the screen's and the mouse's are.
- **A write of 1024 bytes or less to a terminal goes in whole** (D123):
  it waits for room for all of it, so that nothing else's characters
  (another program's, the echo of what is typed) come in between. The
  window system's drawing requests rest on it. Lines, circles and text over it are a library's work,
  in user space: `libgfx` (D111; `gfx(3)`).
