# Using P32W Unix

P32W Unix is the P32W's own multi-user operating system (D22; the design is
[os-design.md](os-design.md), the kernel [kernel/README.md](../kernel/README.md)).
It is a small Unix in the style of V6/V7: several users at once, one on
each serial port and one at the video console, each process in its own protected address space, a Unix
filesystem on the CompactFlash card, and the classic commands.

## Running it

```
make -C emulator                      # the emulators
make -C software disk                 # the C library, the commands, the kernel, software/build/disk.img
python3 assembler/p32asm.py software/monitor/monitor.asm -o monitor.bin
emulator/p32emu -d software/build/disk.img monitor.bin
```

At the monitor's `>` prompt type `b`: the monitor loads the kernel from the
card and the kernel boots. Your terminal is UART 0 (the console); **^]**
stops the emulator. When idle, the emulated machine waits in real time.

**A second user** gets UART 1 over TCP:
```
emulator/p32emu -T 2323 -d software/build/disk.img monitor.bin
python3 tools/p32term.py              # in another window: login: ...
```
`tools/p32term.py` sends each key at once, as the console does (so `q` in
`top`, `vi` and ^C work) and leaves with ^]. Plain `nc localhost 2323` also
connects, but the Mac then holds each line until Return, and ^C ends `nc`.
(For scripts: `-A FILE` feeds UART 1 and `-a FILE` records it; `-w` feeds
input a line at a time, each after a prompt — `software/tests/run_session.sh`.)

On the real machine: write `software/build/disk.img` to a CF card
(`dd if=software/build/disk.img of=/dev/diskN bs=1m`), boot, `b`.

## Logging in

| User | Password | Home |
|---|---|---|
| `root` | `root` | `/` |
| `ken` | `p32` | `/usr/ken` |
| `guest` | (none) | `/usr/guest` |

Change a password with `passwd`. `su` becomes root (with root's password).
`who` lists who is logged in; `id` / `whoami` show who you are. Log out with
`exit` (or ^D); `init` starts a new `login:` on that terminal.

**Shut down** as root with `shutdown [-r] [now | +minutes] [message]`: it
warns both terminals (each minute, with the message), stops the logins,
ends every process, writes everything to the disk and marks the filesystem
clean, then halts — or with `-r` restarts the machine into the monitor
(type `b` to boot again). `halt` and `reboot` do the same at once, without
the warnings. (Switching off without it is like any Unix
without sync: the next boot warns that the filesystem was not unmounted
cleanly.)

## The shell

`sh` reads a command per line:

| | |
|---|---|
| `cmd arg ...` | run a command (found on `$PATH`: `/bin`, `/usr/bin`) |
| `cmd < in > out`, `>> out` | redirection |
| `a | b | c` | a pipeline |
| `a ; b`, `a &` | a list; run in the background (the shell prints its pid) |
| `'...'`, `"..."`, `\c` | quoting (`$` is expanded inside `"..."`) |
| `$NAME`, `${NAME}`, `$?`, `$$`, `$!` | variables, the last status, the shell's pid, the last background pid |
| `$0`–`$9`, `$#`, `$*` | a script's name and arguments (`sh script args`) |
| `NAME=value` | set a variable (all variables are passed to commands) |
| `` `command` `` | its output, in place (split into words unless in `"..."`) |
| `* ? [...]` | wildcards (in the last part of a path) |
| `if list; then list; elif list; then list; else list; fi` | conditions: a command's exit status (`test` / `[`) |
| `while list; do list; done`, `for NAME in words; do list; done` | loops; `break`, `continue` |
| `cd dir`, `exit [n]`, `unset NAME`, `:` | built-ins |

More, as in the Bourne shell (`man sh`): functions (`name() { list; }`,
`return`), `case word in pattern) list ;; esac`, here-documents (`<<END`),
subshells `( list )` and groups `{ list; }`, redirection of any descriptor
(`2> err`, `2>&1`, `>&2`), `set`, `shift`, `read`, `eval`, `.`, `exec`,
`wait`, `umask`, `NAME=value cmd` for one command, `#!` scripts, and
`/etc/profile` plus `$HOME/.profile` for login shells.

A construct may span lines: the shell prompts `> ` until it is complete.
`test` (also `[ ... ]`) checks files (`-f -d -e -r -w -x -s`), strings
(`-z -n = !=`) and numbers (`-eq -ne -lt -le -gt -ge`); `expr` does
arithmetic and comparisons (`` n=`expr $n + 1` ``).

^C interrupts the running command, ^\ quits it, ^U erases the line,
backspace / DEL erase a character, ^D ends input.

**Editing and history** (at a terminal):

| Key or command | What it does |
|---|---|
| ← → (or ^B ^F) | move along the line; typing inserts at the cursor |
| ^A, ^E (Home, End) | go to the start / end of the line |
| backspace, ^D, Delete | delete before / at the cursor (^D on an empty line ends input) |
| ^K, ^U, ^W | delete to the end, the whole line, the word before the cursor |
| ↑ ↓ (or ^P ^N) | step through earlier commands |
| ^L | redraw the line; ^C abandons it |
| Tab | complete a command name (first word) or a file name; Tab again lists the choices |
| `history` | list the last 100 commands, numbered (works in pipelines: `history \| grep cc`) |
| `!!`, `!n`, `!-n`, `!text` | run the last command again, number n, the n-th last, the last starting with `text` (the expanded line is shown first) |

History is kept per user in `$HOME/.sh_history`, so it survives logging
out. A `!` followed by a space or `=` (as in `test a != b`) is left alone,
as is anything inside `'...'`.

## Commands

`ls [-1ACFRSadhilnrst]` `cat` `cp` `mv` `rm [-rf]` `ln` `mkdir` `rmdir` `pwd` `touch`
`chmod` `chown` `find` `du` `ed` `grep [-vnci]` `wc [-lwc]` `sort [-rn]`
`uniq [-c]` `head [-n]` `tail [-n]` `tee` `more` `od [-c]` `cmp` `echo`
`basename` `dirname` `test` / `[` `expr` `fsck` `ps` `kill [-sig]` `sleep` `date` `stty` `uname [-a]`
`who` `id` `whoami` `su` `passwd` `login` `sync` `true` `false`;
`ps [-u user] [-t tty] [-f]`; `/etc/adduser [-p] [-u uid] [-g group] [-c
comment] name` (root: a passwd line, the next free uid from 100, group
staff, a home `/usr/name` of their own — `-p` makes it private, 700; then
`passwd name`); `time
command` (real, user and system time), `uptime` (with the load average);
`top [-d secs] [-n count] [-b]` (the busiest processes, redrawn; keys `q`,
`k` kill, `n` how many, `s` delay); `kill -l`, `kill -HUP pid` (names or
numbers); `shutdown`, `reboot`, `halt`. `ps` shows each process's CPU
time.

**Text tools:** `sed` (scripts, `-n -e -f -i`, `s///` with groups, ranges,
hold space), `tr`, `cut`, `paste`, `join`, `comm`, `xargs`, `diff` (`-u -c -e
-r`, and more; its output works with patch and ed), `bc` (an
arbitrary-precision calculator; `bc -l` for the math library), and `awk`
(POSIX awk: fields, arrays, functions, printf, getline, pipes). Each has a
man page; `man -k word` finds commands by subject.

**Compiler tools:** `yacc` (LALR(1) parsers, with `-ly`) and `lex`
(scanners, with `-ll`); their output compiles with P32W's own `cc`.

**Lua:** `lua` (Lua 5.4: 32-bit integers, double floats, the string,
table, math, io, os, coroutine and utf8 libraries, a REPL) and `luac`
(compile to bytecode). `require` looks in `/usr/lib/lua/5.4` and the
current directory. Both are built on the host, since the native `cc`
can't yet hold Lua's largest files in memory.

**Users:** `write user` and `wall` send to terminals, `mesg y|n` allows or
refuses them (terminals are group `tty`; write and wall are setgid tty);
`mail user` sends and `mail` reads (`/usr/spool/mail`, setgid `mail`;
login says "You have mail."). Root has `chgrp` and `/etc/mknod`.

**Administration** (root): `df` (any user), `mount` / `umount`, `/etc/mkfs`,
`/etc/fsck [-y]` (repairs; after repairing / the machine restarts), `su [-]
[user] [-c cmd]`, `at` and `cron` (`/etc/crontab`).

**Time zones:** the clock keeps UTC; programs show local time by `TZ` (a
POSIX rule, e.g. `TZ=PST8PDT,M3.2.0,M11.1.0`, `TZ=CET-1CEST,M3.5.0,M10.5.0/3`)
or, without it, the first line of `/etc/TZ` (`echo PST8PDT,M3.2.0,M11.1.0 >
/etc/TZ` sets it for everyone); neither: UTC. `date` names the zone; `date
-u` shows and sets UTC; `ls -l`, `who`, `uptime`, `top`, zip's DOS times and
Kermit's dates are local; tar keeps UTC, as its format says.

**Archives** (D25), exchangeable with the Mac: `tar c|t|x[vzf]` (POSIX
ustar; `z` through gzip), `gzip` / `gunzip` / `zcat` (`-cdfkqtv1..9`),
`zip [-rq0..9] archive files` (adds or replaces entries) and `unzip
[-lqtoan] [-d dir] archive [names]`. Modes, owners and modification times
travel with the files. On the Mac, make tar archives with `COPYFILE_DISABLE=1
tar ...` (or the `._name` resource-fork files come along); `unzip` leaves out
Finder's `__MACOSX/` entries unless `-a`.

## Moving files with Kermit

`kermit` (D25) moves files over a terminal line: `kermit -s file ...`
sends, `kermit -r` receives, and **`kermit -x` makes P32W a server** that the
other side drives entirely. Options: `-a name` (as), `-T` (a text file:
CRLF on the line, LF here), `-e length` (longest packet, default 1000),
`-w window` (default 3), `-l /dev/tty1` (use the other line), `-q`. Long
packets, sliding windows, CRC checks and attribute packets (type, size,
modification time) are negotiated with the other Kermit; a lost or damaged
packet is sent again.

From the Mac, with **C-Kermit** (`brew install c-kermit`):

- In the emulator, start `p32emu ... -T 5000` (the second terminal on TCP);
  in C-Kermit: `set host /raw-socket localhost 5000`, then `connect`.
- On the hardware: `set line /dev/cu.usbserial-…`, `set speed 115200`,
  `set flow none`, `set carrier-watch off`, then `connect`.

Log in, type `kermit -x`, and escape back to C-Kermit (Ctrl-\\ then `c`).
Then: `send file` (or `send *.c`), `get name` (wildcards `*` `?`), `remote
dir`, `remote cd /tmp`, `remote delete name`, `remote type name`, and
`finish` to end the server (you are back at the shell). C-Kermit decides
text or binary by the file name and says so in its attribute packet; P32W
stores text with LF line ends. Times: P32W keeps UTC and C-Kermit sends local
time, so received files are off by the time zone until P32W has one.

## Later, and regularly

`/etc/cron` (started by `/etc/rc`) runs `/etc/crontab`'s lines — `minute
hour day month weekday command`, fields `*`, `n`, `a-b`, `a,b`, steps `/n`,
local time — as root, each minute. `at HH:MM [MM/DD]` or `at now + N
minutes|hours|days` queues the commands on its input to run once, as you,
in the current directory, output appended to `$HOME/at.out`; `atq` lists
your jobs, `atrm job` removes one. `sh -c 'commands'` runs a command line.

## More filesystems

`/etc/mkfs special nblocks [ninodes]` makes an empty filesystem on a block
device; `/etc/mount special dir` mounts it, `/etc/umount special|dir`
unmounts it (not while a file or a current directory on it is in use);
`/etc/mount` lists them (`/etc/mtab`); `df` shows the space on each;
`/etc/fsck [special]` checks one. `/dev/ram` is a RAM disk of 256 blocks
(empty after a reboot): `/etc/mkfs /dev/ram 256; /etc/mount /dev/ram /mnt`.
Links cannot cross filesystems (`EXDEV`); `cp` the file instead. Halt,
reboot and shutdown write out and mark clean every mounted filesystem.

## Debugging

`db program [args]` runs a program under the debugger, stopped before its
first instruction: `b where` (a function, `sym+off` or `0xADDR`), `c`, `s
[n]` (step instructions), `n [n]` (over calls), `f` (finish the function),
`bt`, `r` (registers), `set REG VALUE`, `x where [n]`, `p sym`, `l [where]
[n]` (disassemble), `bl` / `d [n]` (list / delete breakpoints), `sym
[prefix]`, `q`; an empty line repeats `s`, `n` or `l`. It works at the
machine level, with the names from the a.out's symbol table (`ld` writes
one; `ld -s` leaves it out). Underneath: `ptrace` (system call 26) — a
traced program stops at every signal and after `exec`, and `wait` reports
the stop (`WIFSTOPPED`).

## Programming tools (M7)

`cc [-c | -S | -E] [-o OUT] [-I DIR] [-D NAME[=VAL]] [-w] files...` is the
C compiler (the language of `cc/README.md`: C89 without floating point,
`long long`, bit-fields or struct arguments and results): `cc -o prog
prog.c` compiles, assembles and links with `/usr/lib/libc.a`; headers are in
`/usr/include`. Underneath: `as [-o file.o] file.s` (the syntax of the
host's `p32asm.py`), `ld [-o prog] /usr/lib/crt0.o file.o ...
/usr/lib/libc.a`, `ar lib.a file.o ...` (or V7 keys: `ar r|q|t|x|d[v]
lib.a [files]`). All four write exactly what the host tools write, and `cc`
compiles itself. Each `.c` file is compiled in a process of its own, which
holds the file's tokens after preprocessing (28 bytes each) and its
largest function's tree. Lua's biggest file, `lvm.c` (53,000 tokens, one
function of 35,000), takes 3 MB of a process's 8 MiB (D33, D34); split
much bigger programs into several files.

`sh` lists: `a ; b`, `a & b`, `a && b` (b if a succeeded), `a || b` (b if
it failed); `NAME=value` and `export NAME=value` set variables.

Without C-Kermit, `tools/pykermit.py --tcp 5000 send|get|dir|finish ...`
does the same against a P32W server (the tests use it).

**`vi [file]`** is the screen editor, for a VT100-style terminal (the
Mac's Terminal or `nc`; `LINES` / `COLUMNS` set its size, 24 × 80): counts;
`h j k l` and the arrows, `w b e 0 ^ $ G gg H M L`, `^F ^B ^D ^U`, `f t F T
; ,`, `%`; `x X r J ~`; `d c y` with a motion or doubled, `D C Y p P`;
`i a I A o O R`; `u` (undo), `.` (repeat); `/re ?re n N`; `ZZ`; `:w :q :q!
:wq :x :e file :r file :N`, `:[range]s/re/text/[g]`, `:[range]d`, `:!cmd`
(ranges `N,M . $ %`; regular expressions `. * ^ $ [...]`). Long lines
scroll sideways instead of wrapping.

**`ed`** is the editor (as on V6: no screen needed): `a` appends lines
until a line with `.`, `p` / `n` print (`,p` the whole file), `d` deletes,
`s/old/new/g` substitutes, `w` writes, `q` quits; addresses are line
numbers, `.`, `$`, `/pattern/`. See the head of `software/cmd/ed.c`.

## Writing programs

On P32W Unix itself: `cc prog.c` (see `man cc`; `cc -h` lists the options),
then `./a.out`. The C library is Unix-shaped (`stdio`, `string`, `stdlib`,
`unistd`, `fcntl`, `signal`, `dirent`, `pwd`...; `man 3 intro`), the system
calls are in section 2 (`man 2 intro`), and `db` debugs the result.

On the host, the same language is compiled with `cc/p32cc.py` (C89
without floating point; [cc/README.md](../cc/README.md)). To make a program
part of the system, add it to `software/cmd/`, to `CMDS` in
`software/Makefile` and to `software/root.manifest`, and `make disk`.

## The system's sources and make

`/usr/src` holds the sources of P32W Unix, each directory with a makefile
for `make` (`man make`):

| Directory | `make` | `make install` (as root) |
|---|---|---|
| `/usr/src/cmd` | rebuilds the commands whose source changed (`make ls`: one) | copies each rebuilt one that differs into `/bin` or `/etc` |
| `/usr/src/cc` | rebuilds the C compiler (`make test`: it compiles itself, identically) | `/bin/cc` |
| `/usr/src/libc` | rebuilds `libc.a` and `crt0.o` | `/usr/lib` |

The disk comes with everything already built there, so a first `make`
says there is nothing to do and later ones rebuild only what was edited.
`/usr/src/kernel` holds the kernel's sources to read, with no makefile:
the kernel is built on the host (`make -C kernel`).
The group `staff` (ken) may build in `/usr/src`. For your own programs,
`make hello` builds `hello` from `hello.c` with no makefile at all.

## The manual

`man name` shows a command's page (a page at a time, through `more`); `man
2 name` and `man 3 name` the system call or library function of that name;
`man -k word` lists the pages about it; `man intro` explains the sections:
1 commands, 2 system calls, 3 the C library, 5 file formats, 8
administration. The pages are plain text in `/usr/man` (the source is
`software/man/`; `tools/mancheck.py` checks their layout). Every command also answers `--help` (alone) with its page: `ls --help` is
`man ls`.

## The date and time

The I/O card's real-time clock (D23) keeps the time with the power off;
P32W Unix reads it at boot and keeps UTC. `date` prints it; as root,
`date 2610141230` sets it to 2026-10-14 12:30 (`[cc]yymmddhhmm[.ss]`, as in
Unix V7) — the clock is set too. In the emulator the clock follows the
Mac's (`-R SECONDS` fixes a start time, as the tests do).

## Devices

Block devices (through the buffer cache): `/dev/disk`, the filesystem area
of the CF card (root only). Character devices: `/dev/tty0`–`/dev/tty3`
(the UARTs), `/dev/tty4` (the video card's screen and keyboard, D53), `/dev/null`, `/dev/mem` (root), `/dev/port` (reading gives the
input port, writing sets the output port), `/dev/rdisk` (the whole card,
raw; root only).
