# Internet over Ethernet with the W5500 — design study (2026-10-17)

Status: **decided (D45, 2026-10-17): B, the Pico coprocessor, and
Ethernet before SLIP.** The hardware: a Pico 2 W and a W5500 on the I/O
card, for Ethernet and Wi-Fi (D50, [architecture.md](architecture.md) §3);
the mailbox is [net-mailbox.md](net-mailbox.md).

From Ken: "analyze adding internet connectivity, TCP/IP, to the system
using the W5500", then "it might need a microprocessor as a coprocessor".
This study weighs both. It extends the backlog's "Networking: SLIP first,
then Ethernet" (2026-10-14), whose last step already named the W5500.

## 1. What the W5500 is

WIZnet's W5500 is an Ethernet controller with the **TCP/IP stack built
into the silicon** (WIZnet calls it "hardwired TCP/IP"):

| | |
|---|---|
| Network | 10/100 Ethernet MAC and PHY on the chip; an RJ45 jack with magnetics outside |
| Protocols in hardware | TCP, UDP, IPv4, ICMP (it answers ping itself), ARP, IGMP, PPPoE |
| Sockets | **8**, each opened as TCP, UDP, IP-raw, or (socket 0 only) MAC-raw |
| Buffers | 32 KiB on the chip: 16 KiB send and 16 KiB receive, shared among the sockets (2 KiB each by default) |
| Interface to the host | **SPI only** (modes 0 and 3, up to 80 MHz), plus an interrupt pin (INTn) and reset (RSTn) |
| Supply | 3.3 V, about 130 mA at 100 Mb/s. Its inputs are **5 V tolerant**; its outputs reach 3.3 V |
| Package | LQFP-48, surface mount, so it comes on a **module** (D21) |
| Not in it | IPv6, DHCP, DNS, TLS. DHCP and DNS are small programs over its UDP sockets |

**The model of use.** The host writes a socket's mode and port into its
registers, then gives it a command: `OPEN`, `LISTEN`, `CONNECT`, `SEND`,
`RECV`, `DISCON` or `CLOSE`. Data goes through each socket's ring buffer
in the chip, with read and write pointers the host moves itself. The chip
does the handshakes, retransmissions, windows, ARP and checksums, and
raises INTn when something happens (connected, data arrived, sent,
disconnected, timeout).

**An SPI transfer** is 3 header bytes (a 16-bit address, then a control
byte naming the register block and read or write) and then any number of
data bytes, which go to consecutive addresses. So a 1460-byte TCP segment
costs about 1463 SPI bytes, and the overhead of the pointer and command
registers is a few dozen bytes per packet.

**Two ways to use it:**
1. **Socket offload** (TCP and UDP sockets): P32W never sees a packet,
   only streams and datagrams. Small and fast on P32W's side.
2. **MAC-raw** (socket 0): the W5500 is just an Ethernet card, sending and
   receiving whole frames, and P32W runs its own TCP/IP stack (ARP, IP,
   ICMP, UDP, TCP). That is the "KA9Q route" of the backlog's SLIP plan.

## 2. The numbers that decide it

**P32W's speed:** about **0.5 MIPS** (D14: a 3 MHz microcycle). Moving a
byte through an I/O register costs a few instructions (load, store,
count, branch). The CH376S driver's figure, an estimate since the
emulators answer at once, is **150–250 KB/s** for a polled byte port.
That is the ceiling for any design in which P32W moves every byte itself.
It is far below Ethernet's 12 MB/s, so the wire is never the limit.

**Compared with SLIP** at 115,200 baud: about 11 KB/s. So any W5500
design is roughly **10–20 times faster** than the SLIP plan.

**A software TCP/IP stack on P32W** (MAC-raw) adds per byte at least a
checksum (an add per 16 bits, done twice: in and out of the stack) and a
copy or two. A rough estimate is **30–60 KB/s**, in line with KA9Q on a
PC/XT. It also needs about 5,000–8,000 lines of kernel C, and its timers
(retransmission, ARP) run in the kernel.

**TLS (HTTPS)** is where P32W runs out. A TLS handshake does an
elliptic-curve key exchange and checks a certificate chain. On P32W that
is on the order of 10^8 instructions or more, which is **minutes per
connection** (an estimate), before the cipher itself costs time on every
byte. Today most of the internet is HTTPS only: web pages, package
servers, GitHub. **Without TLS, P32W can reach the local network, plain
HTTP, telnet, FTP, NTP, DNS, and HTTPS only through a proxy on another
machine.**

## 3. Three ways to build it

The kernel side above the hardware (sockets as file descriptors, the
classic system calls) is the same in all three: section 5.

### A. A TTL SPI master; P32W drives the W5500 itself

The W5500 module sits on the I/O card behind a small **SPI
master made of logic**:
- an 8-bit shift register, a 74HCT299 (or a '595 to send and a '165 to
  receive), loaded by a write and read back by a read;
- a 74HCT161 counting the 8 bits and a flip-flop for "busy", or all of it
  in a GAL (an ATF22V10 holds the 3-bit counter and the control);
- SCLK from the bus clock: at 3 MHz, a byte shifts in under 3 µs, about
  one P32W instruction, so P32W never has to wait;
- chip select from a register bit, so P32W holds SCSn low across a burst;
- no level shifting: the W5500 takes 5 V inputs, and its 3.3 V MISO and
  INTn are a valid high for HCT inputs (2.0 V);
- a 3.3 V regulator for the module (an LM1117-3.3 in TO-220).

About **5–7 chips** plus the module. **Registers:** SPI data (write sends
a byte; read returns the byte received), SPI control (chip select,
reset), and status (busy, INTn). **IRQ7**.

**Software on P32W:**
- the W5500 driver in the kernel, about 800 lines: socket commands, the
  ring buffers, the interrupt;
- DHCP and DNS as small user programs (`dhcp`, and a resolver in libc)
  over UDP sockets.

**Good:**
- the fewest parts, and no second program to maintain;
- all of it is P32W's own code: in keeping with a machine built to be
  understood;
- MAC-raw is there too, for a P32W stack of its own later (route 2 above).

**Limits:**
- 8 sockets at most;
- the W5500's TCP has no listen backlog: accepting a connection uses up
  the listening socket, and the server must open a new one;
- no TLS;
- P32W moves every byte (150–250 KB/s at best).

### B. A coprocessor: an RP2040 with the W5500 (Ken's suggestion)

A microcontroller sits between P32W and the W5500. The neatest part is
WIZnet's **W5500-EVB-Pico**: a Raspberry Pi Pico board (RP2040, 133 MHz,
264 KiB of RAM) with a W5500 and an RJ45 jack on the same board, on the
Pico's 2.54 mm pins. The debug card's host link is a Pico too (D50), so
the tools and the 3.3 V-to-5 V interfacing are shared.

**P32W talks to it through a mailbox,** like the CH376S's command and data
ports. Its commands are network operations, not W5500 registers:
- `socket`, `connect`, `listen`, `accept`, `send`, `recv`, `close`;
- `dns` (a name to an address) and `status` (link, address from DHCP);
- possibly `tls-connect` (below).

**The firmware** runs WIZnet's ioLibrary (DHCP, DNS, the W5500 driver)
or lwIP over MAC-raw, in C with the Pico SDK.

**The mailbox hardware** needs care, because the Pico is 3.3 V and **not
5 V tolerant**, and firmware cannot answer a 333 ns bus cycle in time.
So the mailbox is latches, not a live bus:
- P32W to Pico: a 74HCT574 latch that P32W writes. Its 5 V outputs reach
  the Pico through CD74HC4050 buffers (DIP-16, run at 3.3 V, 5 V-tolerant
  inputs).
- Pico to P32W: the Pico drives a second 74HCT574's inputs at 3.3 V (a
  valid HCT high), and P32W reads it.
- A GAL keeps the "full" and "empty" flags and raises IRQ7.

About **6–8 chips** plus the board.

A refinement: **FIFOs instead of single latches** (IDT7201-style, 512 × 9,
DIP-28, one per direction), so P32W can move a block without checking a
flag between bytes. Their availability needs checking: the 720x family is
old.

**Good:**
- **TLS becomes possible.** The RP2040 can run mbedTLS: slowly by PC
  standards (a handshake in a second or two), but feasible. P32W could
  then fetch real `https://` URLs, with the certificates checked on the
  coprocessor. **This is the one thing A cannot do.**
- DHCP leases, DNS, ARP and TCP keepalives are timed by the
  coprocessor, not P32W's kernel.
- The kernel driver is the simplest of the three (like `usb.c`).
- The coprocessor can be reprogrammed, so protocols can be added later
  without touching the card.

**Limits:**
- **A second program to write and keep in step,** in another language
  and toolchain. Its emulator model must speak the same mailbox protocol.
- **P32W still moves every byte through the mailbox,** so throughput is
  about the same as A (150–250 KB/s at best). A coprocessor saves P32W
  register bookkeeping, not byte moves.
- The coprocessor is far more powerful than P32W, so "the computer"
  does less of the networking. (The CH376S already has a microcontroller
  inside, and so does the W5500's state machine.)

### C. A 5 V microcontroller: an AVR with a W5500 module

The same idea as B with an **ATmega1284P** (DIP-40, 5 V, 20 MHz, 16 KiB
RAM, through-hole like the rest of P32W) and a WIZ850io module:
- **no level shifting on P32W's side:** it can sit on the 5 V bus behind
  plain HCT latches;
- MISO from the W5500 goes through one HCT buffer, because 3.3 V is only
  just a high for a 5 V AVR;
- programmed with avr-gcc.

**Good:** the most "in period" part: fully through-hole and 5 V.

**Limits:** too small and slow for TLS. Without TLS it offers little that
A does not, at the cost of the second program.

### Summary

| | A: TTL SPI master | B: RP2040 + W5500 | C: AVR + W5500 |
|---|---|---|---|
| Chips on the card (plus the module) | 5–7 | 6–8 | 5–7 |
| Second program to maintain | no | yes (C, Pico SDK) | yes (C, avr-gcc) |
| Kernel driver | about 800 lines | about 400 lines (mailbox) | about 400 lines |
| Throughput (P32W moves every byte) | 150–250 KB/s | the same | the same |
| HTTPS from P32W | no (a proxy) | **yes, on the coprocessor** | no |
| DHCP, DNS | user programs on P32W | on the coprocessor | on the coprocessor |
| A P32W TCP/IP stack later (MAC-raw) | yes | yes (frames through the mailbox) | possible, slow |
| Level shifting | none | the Pico's side | MISO only |
| Through-hole (D21) | yes, plus the W5500 module | yes, plus the Pico board | yes, plus the W5500 module |

**My reading:**
- If the goal is **the internet as it is today** (web pages, downloads,
  GitHub), the deciding fact is TLS, and that points to **B**. P32W keeps
  everything above the socket: the system calls, the programs, the shell
  scripts.
- If the goal is **a network P32W runs itself** (the local network, a web
  server for P32W's own pages, telnet, plain HTTP, with HTTPS through a
  proxy as the backlog already planned), **A** is smaller, has nothing
  extra to program, and does not need a microcontroller.
- **C** sits between them without TLS, so it has the least to recommend
  it.

So yes, a coprocessor is needed, but only for TLS. For TCP/IP itself the
W5500 already is the coprocessor.

## 4. The emulator first, as with USB

All three can be built and tested in the emulators before any hardware,
and with **real network access**. The model maps P32W's sockets onto the
Mac's own sockets: a W5500 `CONNECT` becomes a `connect()` on the Mac, a
`LISTEN` a listening socket on a host port.
- **For A:** a W5500 register model behind the SPI registers, with the
  socket commands done by the Mac. It is large-ish, but the W5500's
  socket model is simple.
- **For B:** a model of the mailbox protocol, even simpler, since its
  commands are already socket operations. TLS in the model can use the
  Mac's TLS library (Python's `ssl`, or `-lssl` in C).

Then P32W Unix can fetch a web page from the emulator on the first day.
The tests can run against a small server the test script starts on the
Mac, so `make test` needs no internet.

## 5. P32W Unix's side (the same for A, B and C)

- **Sockets as file descriptors:** a new `FD_SOCKET` type beside
  `FD_INODE` and `FD_PIPE`, so `read`, `write` and `close` work on them,
  and shell redirection and `cat` work with the network.
- **System calls:** `socket`, `connect`, `bind`, `listen`, `accept`,
  `send`/`recv` (or just read and write), `shutdown`, `getsockname`,
  `setsockopt` in a small form; BSD-compatible, so ported programs work.
  A way to wait on several descriptors at once (`select` or `poll`) is
  needed for telnet and a server that handles more than one client.
- **A network interface layer underneath,** so the SLIP plan (with P32W's
  own stack) could become a second interface later, as the backlog wants.
- **Programs, in the backlog's order:**
  - `ifconfig` / `netstat` (small);
  - `ping`;
  - a DNS resolver in libc (`gethostbyname`);
  - `telnet` and `telnetd` (on a pseudo-terminal);
  - `httpd` (static pages and CGI);
  - `ftp`/`tftp` and NTP (the clock set at boot);
  - with B, a `fetch URL` command, http and https.
- **Limits to accept:** 8 sockets on the W5500, each listening port using
  one. That is enough for telnetd plus httpd plus a few clients, not for
  a busy server.

## 6. The hardware, wherever it goes

- **On the I/O card** (D50): next to the CH376S ports, with **IRQ7** and
  registers `FF52` / `FF53` in the card's window ([net-mailbox.md](net-mailbox.md)).
- **Modules, in sockets (D21):** WIZ850io (W5500 with an RJ45 jack and
  magnetics, 2 × 6 pins, 3.3 V) for A or C; W5500-EVB-Pico (2 × 20 pins,
  powered from 5 V through its own regulator) for B. **Before the PCB,**
  check the module in hand: pinout, supply, and the RJ45 jack's height
  against the card's front edge, where the jack sits.
- **Power:** about 130 mA more for the W5500 (plus about 30 mA for an
  RP2040), at 3.3 V from a regulator on the card or on the board.
- **The jack at the card's edge,** as the USB socket is: both modules have
  it at one end, so the layout puts that end at the front edge.

## 7. Work, in order (once the questions are answered)

1. Ken's choice of A, B or C, and of the order relative to SLIP.
2. The emulator model (both emulators), mapped to the Mac's sockets.
3. Sockets in the kernel, and the driver for the chosen interface.
4. The programs: `ifconfig`, `ping`, the resolver, `telnet`, `fetch`,
   then `telnetd`, `httpd`, NTP.
5. With B: the coprocessor firmware, tested against the emulator's model
   on the Mac's side first (the firmware's logic can build on the Mac).
6. The card: part of the I/O card's netlist and GALs, through the
   generators, after a real module is checked.

## 8. Questions for Ken

1. **A, B or C?** In short: does P32W need HTTPS itself (B), or is "the
   network P32W runs, HTTPS through a proxy" enough (A)?
2. **SLIP first, as the backlog says, or Ethernet first?** With A or B,
   the W5500 does TCP, so SLIP would mean writing P32W's own stack as a
   separate project (still worthwhile for learning, and needed for
   MAC-raw).
3. ~~**B only:** the W5500-EVB-Pico (one board), or a separate Pico and
   WIZ850io?~~ **Decided (D50): a Pico 2 W and a W5500, for Ethernet and
   Wi-Fi.**
4. **Buy a module now** to check its pinout and size before the card's
   layout, as for the CH376S?
