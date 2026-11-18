# The boot sequence

**Status:** DESCRIBES THE SYSTEM AS BUILT (2026-10-26): the CPU card's
reset logic (`generators/card_cpu.py`), the microcode
(`microcode/p32ucode.py`), the boot ROM monitor
(`software/monitor/monitor.asm`), the disk layout (`tools/p32fs.py`) and
the kernel (`kernel/`). Each step names the file that does it.

From power-on to `login:` the machine passes through six stages. Each one
hands a small, well-defined state to the next:

| Stage | Runs | Hands on |
|---|---|---|
| 1. Power and reset | the CPU card's GAL PANEL | `-RES` released: every card in its reset state |
| 2. The reset microcode | the CPU card's sequencer | r0 = 0, PC = 0, supervisor mode, interrupts off |
| 3. The monitor | the boot ROM at `0x0000_0000` | the console at 9600 baud; the `b` command |
| 4. The kernel loader | the monitor's `b` | the kernel in RAM at `0x0010_0000`, called |
| 5. The kernel's start-up | `kernel/start.s`, `kernel/main.c` | devices, memory, the root filesystem, process 1 |
| 6. User space | `/etc/init`, `/etc/rc`, `getty`, `login` | a shell for each user |

## 1. Power and reset

**What starts it.** Two things reset the machine:
- **`PWROK`** (J2 A6), *power good*, from the backplane's supply
  supervisor (a TL7705A, D78). It stays low until +5 V has been above
  4.55 V for about 130 ms, and falls when +5 V dips below that. The debug
  card can pull it low as well (`MRES`, D136), to hold the machine in
  reset from the Mac.
- **The RESET button** on the CPU card's front panel (debounced by a
  74HCT14).

**GAL PANEL** on the CPU card is clocked by the 24 MHz oscillator. It
registers `RESET = RESBTN or not PWROK` and drives the inverse onto the
backplane as **`-RES`** (J2 A5). Every card sees the same reset, and while
it is low:
- The CPU card's registered GALs (STATUS, IMASK, COND and the others) are
  held in their reset state through their asynchronous reset inputs. STATUS
  resets to supervisor mode with interrupts off, IMASK to 0 (every level
  masked), VBR to 0.
- GAL PANEL forces the sequencer's next microaddress to the **RESET
  internal sequence**. While RESET is high, every commit edge loads that same
  first microword again.
- The ALU module (a Tang Nano 20K) gets its own reset through a 74LVC244
  (`NANO_RES_N`).
- The MMU / data card clears its state: the TLB-write pending flag, the bus
  lock, the arbiter's grants.
- The device cards reset their devices. On the I/O card (its netlist is
  in progress) the timer stops (enable 0, pending 0), the UARTs reset
  (MR) and the network coprocessor's RUN pin is held low.

When the PANEL GAL sees `PWROK` high and the button released, it drops
RESET at the next oscillator edge, so `-RES` rises cleanly, in step with
the clock.

## 2. The reset microcode

The first two microwords after reset are the **RESET internal sequence**
(`p32ucode.py`, `ISEQ["RESET"]`):

| Step | Microword | Effect |
|---|---|---|
| 0 | R = 0 → register r0 | r0 is a real register in the register file's SRAM, and its contents after power-on are random. The microcode writes it to 0 here, and no instruction can write it afterwards (D4). |
| 1 | R = 0 → PC and MAR, END | The PC is 0, and so is the MMU / data card's MAR (its shadow PC loads with it). END starts the first fetch. |

The machine is now in the state `isa-system.md` §1 defines: **supervisor
mode** (`STATUS.S = 1`), **interrupts off** (`IE = 0`, `IMASK = 0`),
`VBR = 0`, and executing at **physical address 0**. Supervisor addresses
are not translated (isa-system.md C2), so the TLB's random contents after
power-on do not matter yet.

## 3. The monitor (the boot ROM)

Address 0 is the **boot ROM**: four SST39SF020A flash chips on the memory
card in window 0, the one with its ROM jumper open (D52, D63), one chip per
byte lane. The ROM holds the monitor, `software/monitor/monitor.asm`,
assembled and split into its four lanes. Its first instructions
(`reset:`):

1. `sp = 0x001F_FC00`: the stack, growing down from just below the
   monitor's 128-byte line buffer at the top of the first 2 MiB bank. Every
   machine has that bank (D60), whatever else is fitted.
2. `VBR = exception`: from now on any exception (a bad address, an illegal
   instruction in a program the monitor called) lands in the monitor's own
   handler. The handler prints `CAUSE`, `EPC` and `BADADDR` and returns to
   the prompt.
3. UART 0 to 9600 baud, 8 data bits, no parity, 1 stop bit (divisor 12 from
   the 1.8432 MHz clock), FIFOs on, interrupts off (`uart_init` in
   `software/lib/p32io.inc`).
4. The banner, `P32W monitor -- ? for help`, and the prompt `> `.

The monitor polls the UART; it never enables interrupts. Its commands are
described in [the monitor](monitor.md). To boot, the user types **`b`**.

## 4. The kernel loader: the monitor's `b`

**The disk.** The boot device is the CompactFlash card in the disk card's
port 0 (window 6, `0xFFFF_EF00`; D50, D52). `tools/p32fs.py` lays it out:

| Sectors (512 bytes) | Contents |
|---|---|
| 0 | **the boot header**: five little-endian words |
| 1 … 2047 | **the boot area** (1 MiB): the kernel image, `kernel/unix.bin` |
| 2048 … | **the filesystem** (its first sector is the header's word 4) |

| Header word | Meaning | Today's value |
|---|---|---|
| 0 | magic `"P32B"` (`0x4232_3350`) | |
| 1 | the kernel's size in sectors | `0xD1` (209 sectors, about 105 KiB) |
| 2 | the load address | `0x0010_0000` |
| 3 | the entry point | `0x0010_0000` (`_start`) |
| 4 | the filesystem's first sector | `0x800` (2048) |

**What `b` does** (`cmd_boot`):
1. `cf_init`. Is a card there? The status register's DRDY bit must be set.
   A socket with nothing in it reads 0, because the adapter pulls the bus
   down. If no card answers, the monitor prints `no disk`. If one does, it
   sends SET FEATURES 81h, which turns off any 8-bit transfer mode earlier
   software left behind. The data register is 16 bits wide (D26).
2. Sector 0 is read into RAM just below the line buffer, at `0x001F_F800`.
   If its first word is not `P32B`: `not a boot disk`.
3. `disk_load`: word 1's count of sectors, starting at sector 1, into
   memory from word 2's address, one READ SECTOR command per sector. Each
   sector is 256 halfword reads of the data register, polling BSY and DRQ
   in between. Then it prints `loaded 000000D1 sectors at 00100000` and
   `entry 00100000`.
4. **`callr`** to word 3's entry point. It is a *call*, so a program that
   returns lands back in the monitor, which prints `returned` and r1. The
   kernel never returns. Its `reboot` system call clears STATUS, IMASK and
   VBR and jumps to address 0 instead, which restarts the monitor as a
   reset would, except that the hardware is not reset.

At this point the CPU is still in supervisor mode with interrupts off, and
the stack pointer is the monitor's.

## 5. The kernel's start-up

### 5.1 `_start` (`kernel/start.s`)

1. `sp` = the kernel's own 4 KiB boot stack (`bootstack_top`, in its
   .bss).
2. The .bss, `_bss` to `_end`, is cleared. The monitor loaded only the text
   and data, and RAM after power-on holds anything.
3. `STATUS = 1` (supervisor, interrupts off), `IMASK = 0`,
   `VBR = trapvec`. From here every trap and interrupt enters the kernel's
   handler, not the monitor's.
4. `call kmain`.

The kernel is linked at `0x0010_0000` (`kernel/Makefile`:
`--text 0x00100000`) and runs **untranslated**: in supervisor mode an
address is a physical address, so the kernel needs no page table of its own.

### 5.2 `kmain` (`kernel/main.c`), in order

| Call | What it does |
|---|---|
| `video_init()` | If the video card answers (its ID read with a probe, D53), the screen becomes a second console. |
| `kprintf("P32W Unix (M7)")` | The first line on the console (UART 0, polled). |
| `kinit()` | **Memory** (`vm.c`): probes every 2 MiB bank from 2 MiB to 64 MiB. A bank is there if a test pattern written at its offset `0x100` reads back and no BUSERROR came; a missing bank's access is a BUSERROR, which `trap()` skips while `probing` is set. Every free 4 KiB frame above the kernel's `_end`, in the banks found, goes on the free list. Frames in missing banks are marked in use for good. Holes are allowed (D60). |
| `procinit()` | The 64 process slots. Slot k gets ASID k + 1. |
| `binit()` | The buffer cache: 48 one-KiB buffers on an LRU ring. |
| `fileinit()` | The open-file table (128 entries). |
| `cf_init()` | **The disk** (`cf.c`): the boot card must answer, or `panic("no disk")`. Reads the boot header's word 4 (the filesystem's first sector) and probes the second CF socket and the two IDE channels for more drives. |
| `fsinit(ROOTDEV)` | **The root filesystem** (`fs.c`): reads the superblock (block 0 of the filesystem area), checks its magic. If its *clean* flag is 0 it warns `the filesystem was not unmounted cleanly (run fsck)`. Then it writes the flag as 0 (in use) until a clean shutdown sets it again. |
| `kprintf("memory: … filesystem: …")` | RAM found, frames free, blocks free. |
| `rtc_init()` | **The real-time clock** (D23): if the DS12C887 answers, the time of day comes from it (`boottime`). Without it the system starts at 1 January 1970. |
| `usb_init()`, `net_init()` | The I/O card's two CH376S USB controllers and the network coprocessor, if they answer. |
| `ttyinit()` | The four UARTs at 9600 baud 8N1 with receive interrupts on, and IMASK levels 2 and 3 enabled (UARTs 0 and 2 share level 2, 1 and 3 share level 3). |
| timer | `TRELOAD = TCOUNT = 20000`, `TCTRL = 1`: the I/O card's 1 MHz timer interrupts 50 times a second (`HZ`, `param.h`); IMASK level 0 enabled. |
| `tlbflush()` | Both halves of the TLB invalidated. Its contents after power-on are random, and user mode is about to use it. |
| `userinit()` | **Process 1** (below). |
| `scheduler()` | Never returns. The kernel is now a scheduler that runs processes. |

Interrupts are still off (STATUS.IE = 0) through all of this. The
scheduler's loop is the first code to turn them on.

### 5.3 Process 1 (`userinit` in `kernel/proc.c`)

Process 1 gets an address space with two pages:
- **`0x1000`**: a nine-instruction program (`initcode`) that is
  `execve("/etc/init", { "/etc/init", 0 }, 0)`, then `exit(1)` if the
  exec fails;
- **`0x7F_F000`**, the page below `USERTOP` (8 MiB, D34): its stack.

Its trap frame is set so that the first "return from trap" (`RTE`) enters
**user mode at `0x1000` with interrupts on** (`status = S | PIE`: the
current mode supervisor, the mode `RTE` returns to user, interrupts
enabled after it). Its kernel context starts in `trapret`. Its current
directory is `/`, and its user and group are 0 (root).

The scheduler finds it RUNNABLE, loads its ASID and page table pointer
(`switchuvm`) and switches to it (`swtch`). `trapret` restores the trap
frame, and `RTE` runs `initcode`. Its `SYSCALL` for `execve` is the first
system call. The kernel reads `/etc/init` from the filesystem, builds a new
address space (text at `0x1000`, data, bss, a stack holding `argc` and
`argv`), and the same `RTE` path starts `init`'s `main`.

## 6. User space

### 6.1 `/etc/init` (`software/cmd/init.c`)

1. Opens `/dev/tty0` (the console, UART 0) as its standard input, output
   and error. If `/dev/tty4` opens, the video card is fitted and its screen
   and keyboard get a login too.
2. Runs **`/bin/sh /etc/rc`** and waits for it:

   ```
   /etc/fsck -q -c          a full check only if the filesystem was not clean (D40)
   : > /etc/mtab            nothing but / is mounted
   : > /etc/utmp            nobody is logged in
   /etc/cron
   /etc/ntpdate             only if /etc/ntp.conf exists: the clock from the network (D46)
   /etc/telnetd             remote logins, if there is a network
   echo P32W Unix: multi-user
   ```

3. If there is no `/etc/getty`, it runs single-user: a shell on the
   console, started again whenever it exits.
4. Otherwise it starts **one `getty` per terminal**: `tty0`, `tty1` and
   `tty4` when the video card is fitted. Each getty runs in a new session
   (`setsid`), so the terminal it opens becomes its controlling terminal.
5. It then waits forever. When a getty's session ends, init clears that
   terminal's `/etc/utmp` record and starts a new getty. A getty that ends
   within five seconds five times in a row waits a minute before the next
   (a terminal that keeps failing does not spin). Orphaned processes are
   given to init, which reaps them. `SIGTERM` (from `shutdown`) stops the
   respawning.

### 6.2 `getty` and `login`

- **`/etc/getty ttyN`** opens `/dev/ttyN`, sets it to echo with CR/LF
  mapping, erase ^H and kill ^U, prints `P32W Unix (ttyN)` and `login:`,
  reads a name and execs **`/bin/login name ttyN`**.
- **`/bin/login`** asks for the password with echo off (three tries, then
  back to getty), checks it against `/etc/passwd`, and records the login in
  `/etc/utmp`. It gives the user the terminal (owner, group `tty`, mode
  0620), prints `/etc/motd` and `You have mail.` when the mailbox is not
  empty, sets the user and group IDs, `HOME`, `USER`, `LOGNAME`, `SHELL`
  and `PATH=/bin:/usr/bin`, changes to the home directory and execs the
  user's shell. The shell reads `/etc/profile`.

The machine is up.

## 7. Shutting down and rebooting

- **`sync`** (and the kernel itself, every 30 seconds) writes the buffer
  cache's delayed blocks to the disk.
- **`shutdown`** warns the logged-in users (`wall`), sends `SIGTERM` to
  init (no new logins), then to every other process, waits two seconds
  and sends `SIGKILL`. **`halt`** and **`reboot`** call the `reboot` system
  call (root only). It writes every delayed block and the superblocks with
  the *clean* flag set (`fs_clean`). Then it either executes `HALT`, or
  stops the clock, masks every interrupt and jumps to the monitor at
  address 0 with STATUS and VBR cleared, as after a reset button press
  (`reset` in `start.s`). A
  machine that loses power without this leaves the flag 0, and the next
  boot's `fsck -c` checks the whole filesystem.

## 8. In the emulators

The emulators start exactly as the hardware does: `p32emu` and `p32uemu`
take the ROM image (`monitor.bin`) and begin at address 0 in supervisor
mode. `-d IMAGE` is the CompactFlash card:

```
make -C emulator
make -C software disk
python3 assembler/p32asm.py software/monitor/monitor.asm -o monitor.bin
emulator/p32emu -d software/build/disk.img monitor.bin
```

At the monitor's `>` type `b`. On the real machine, write
`software/build/disk.img` to a CF card (`dd … bs=1m`), power on, `b`.
