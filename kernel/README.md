# P32W Unix kernel

**Status:** M3 and M4 of D22 ([os-design.md](../docs/os-design.md); using
it: [docs/unix.md](../docs/unix.md)). Boots from the CF disk in the
emulator, mounts the root filesystem and runs `init`, which keeps a `getty`
/ `login` on each terminal. `make test` runs two recorded sessions: one user
working with files, directories, links, redirection, pipelines, background
jobs, `ps`, errors and `halt`; and two users logged in at once on the two
terminals (a wrong password refused, permissions between users enforced,
`su` with root's password, `passwd` refused for another user). After each,
the host's `fsck` finds the disk clean.

```
make -C kernel            unix.bin: the kernel, linked at 0x0010_0000
make -C software disk     software/build/disk.img: the kernel in the boot area + the root filesystem
emulator/p32emu -w -d software/build/disk.img monitor.bin       then type b at the > prompt
```
(`monitor.bin`: `python3 assembler/p32asm.py software/monitor/monitor.asm -o monitor.bin`.
`-w` waits for a prompt before each input line — for scripts; leave it out
when typing.)

## Files

| File | What |
|---|---|
| `start.s` | entry; the trap vector with the TLB-refill fast path; `trapret`; `swtch`; wrappers for the privileged instructions |
| `main.c` | `kmain`: memory, processes, buffers, files, disk, filesystem, terminals, the clock, process 1 |
| `trap.c` | system calls, faults (stack growth, or a signal), interrupts (clock, UARTs) |
| `proc.c` | the process table, the scheduler, `sleep` / `wakeup`, `fork`, `exit`, `wait`, signals |
| `vm.c` | page frames; page tables; `copyin` / `copyout`; stack growth; `sbrk` |
| `exec.c` | loading an `a.out`; the argument stack; set-user-id |
| `syscall.c` | the system calls (V7 numbers, `sys/syscall.h`) |
| `fs.c` | the inode filesystem (tools/p32fs.py's format): inodes, block maps, directories, path names, permissions |
| `bio.c` | the buffer cache (48 × 1 KiB, LRU, delayed writes) |
| `cf.c` | the CompactFlash driver (16-bit PIO, D26; a block is one two-sector command): block device 0 (the filesystem area) and the card's layout |
| `file.c` | open files, pipes, the block- and character-device switches, `/dev/null` `/dev/port` `/dev/rdisk` |
| `rtc.c` | the real-time clock (D23): read at boot, set by `stime` |
| `usb.c` | a USB drive through the I/O card's CH376S (D43), polled: block device 2 (`/dev/usb`) and the raw sectors, character device 5 (`/dev/rusb`) |
| `tty.c` | the terminals (UARTs 0–3, and the video console `/dev/tty4`), interrupt-driven, with the V7 line discipline |
| `video.c` | the video card's screen and PS/2 keyboard, the console behind `/dev/tty4` (D53) |
| `kprintf.c`, `klib.c` | console messages, panics, string routines |

## How it works

- **Memory.** The kernel runs untranslated (supervisor addresses are
  physical). Free RAM after the kernel is a list of 4 KiB frames. Each
  process has **one page table**: 2,048 entries in `TLBLO` format, in
  two physically contiguous frames, covering user addresses 0–8 MiB
  (D34), so the TLB-miss handler in `start.s` refills with one load. Page 0 is never mapped; text is
  read + execute; data, bss, heap and stack are read + write; the stack
  grows on demand down to 1 MiB. A process slot's ASID is its index + 1,
  so a context switch is one `MTCR ASID`; the TLB is flushed only when a
  mapping is removed or a slot is reused.
- **Processes.** `fork` shares the address space **copy-on-write**: every
  frame has a reference count; writable pages become read-only + `PTE_COW`
  (a software bit the TLB ignores) in both processes, and the first write —
  a `PROTECT` fault, or the kernel's `copyout` — copies the page (or, for
  the last sharer, just makes it writable); text is shared outright.
  `exec` builds a fresh one and frees the old only when the new is
  complete. Each process has a 4 KiB kernel stack holding its trap frame.
  The kernel is **not pre-empted** (V6): a process in the kernel runs until
  it sleeps; the clock (50 Hz) only asks for a reschedule, which happens on
  the way back to user mode. The scheduler is round-robin; it idles with
  `WAIT`.
- **System calls.** `SYSCALL`, number in r1, arguments r2–r5, result r1
  (−errno on failure). Every user pointer is checked through the page
  table.
- **Signals.** V7 numbers and semantics (a caught signal reverts to the
  default). A handler runs in user mode through the library's trampoline
  (`__sigtramp`): the kernel saves the trap frame on the user stack and
  `sigreturn` restores it. ^C / ^\ on a terminal signal every process on it.
- **Files.** V6/V7: an open-file table shared across `fork`, pipes (1 KiB),
  inodes cached and locked, a write-back buffer cache flushed by `sync` and
  every 30 s, `fsck` after a crash (no journal; os-design.md 5.4).
- **Devices** (Unix V6/V7): **block** devices (`bdevsw`: open, close,
  *strategy*) move 1 KiB blocks through the buffer cache — the CF card is
  block device 0, the root filesystem lives on its minor 0, and
  `/dev/disk` reads it through the cache (what `fsck` uses, so it sees what
  the kernel sees); **character** devices (`cdevsw`: open, close, read,
  write, ioctl) move bytes straight to the process — `/dev/tty0` `tty1`,
  `/dev/null`, `/dev/mem`, `/dev/port`, and `/dev/rdisk`, the raw card byte
  by byte (as V7's `/dev/rrk0` beside `/dev/rk0`). A new disk, a RAM disk
  or a partition is one more `bdevsw` entry; `mount` (M6) will use them.
- **Terminals.** Input by interrupt with echo, erase, line kill, ^C ^\ ^D,
  CBREAK and RAW modes (`stty`); output by interrupt from a queue.

## Still to come (M5–M6)

Signals in the session tests; the shell's `if` / `while`; `fsck` repairs
(it checks now, at every boot, from `/etc/rc`); `mount`; booting the real
machine.
