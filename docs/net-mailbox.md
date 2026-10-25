# The network coprocessor's mailbox (D45)

The interface between P32W and the network coprocessor (a Pico 2 W with a
W5500, on the I/O card, D50). Three programs implement it and must
agree on it:
- the emulators' model (`emulator/p32io.h`, `-N`), which carries it out
  with the Mac's own sockets;
- the kernel's driver;
- the coprocessor's firmware.

This document is their common reference. Version 1, 2026-10-17.

## 1. Registers

Two byte registers in the I/O card's window (D52), after the first
CH376S's. Byte accesses only, as for the card's other byte devices. The
I/O card's interrupt status (`FFFF_FFF4`) has bit 8 for the coprocessor.

| Address | Read | Write |
|---|---|---|
| `0xFFFF_FF52` `NDATA` | the next reply byte (reading takes it) | the next request byte |
| `0xFFFF_FF53` `NSTAT` / `NCTRL` | status | control |

**Status** (`NSTAT`, read):

| Bit | Name | Meaning |
|---|---|---|
| 0 | `RXF` | a reply byte is waiting in `NDATA` |
| 1 | `TXE` | the request latch is empty: P32W may write `NDATA` |
| 2 | `ATTN` | the coprocessor has events (section 4); with `IEN`, `IRQ7` is asserted |
| 3 | `IEN` | interrupts enabled (as written) |
| 6 | — | always 0 |
| 7 | `READY` | the coprocessor is running (its firmware has started) |

**Control** (`NCTRL`, write):

| Bit | Meaning |
|---|---|
| 3 | `IEN`: assert `IRQ7` while `ATTN` is set |
| 7 | `RESET`: restart the coprocessor (the Pico's RUN pin) and empty both latches; `READY` drops until it has started again |

**The latches.** On the card, each direction is one byte latch with a
full/empty flag, kept in a GAL: writing `NDATA` fills the request latch,
and the Pico taking the byte empties it again; the Pico filling the reply
latch sets `RXF`, and P32W reading `NDATA` clears it. The Pico answers
within a microsecond, which is less than one P32W instruction, so a driver
that checks `TXE` and `RXF` before each byte almost never waits. The
model answers at once, as all of the emulators' devices do: software
must depend on the bits only, never on timing.

**The I/O card's side** (D50): the request latch reaches the Pico through
a 3.3 V buffer it enables with `-PRD`; the Pico clocks the
reply latch with `PWR`. A GAL clocked by P32W's `CLK` keeps `TXE` and `RXF`
and acts on each strobe's leading edge, once. `RESET` is a one-clock pulse
on the Pico's RUN, stretched by an RC to about 15 µs.

**Without the I/O card,** the device window does not answer at these addresses
(`BUSERROR`). **With the card but no running coprocessor,** `READY` is 0.
The emulators give the second case without `-N`.

## 2. Messages

P32W sends a **request** and reads the **reply**, then sends the next
request; there is exactly one reply to each request, and nothing else
ever arrives in `NDATA`. The two have the same 4-byte header:

| Byte | Request | Reply |
|---|---|---|
| 0 | command | status (section 5) |
| 1 | socket number (0 if none) | socket number (the new one, for `SOCKET` and `ACCEPT`) |
| 2, 3 | payload length, low byte first | payload length, low byte first |
| 4 … | payload | payload |

Payloads are at most **1024 bytes** each way. Addresses and ports in
payloads are in **network order** (big-endian), as in `sockaddr_in`:
an address is 4 bytes, a port 2.

A driver that loses its place (a P32W reset in the middle of a message,
say) sets `RESET` and starts again with `HELLO`.

## 3. Commands

| Code | Name | Request payload | Reply payload |
|---|---|---|---|
| `01` | `HELLO` | — | the text `P32NET 1` (the protocol's version). Closes every socket |
| `02` | `IFINFO` | — | 24 bytes: flags (bit 0 link up, bit 1 address configured, bit 2 by DHCP), 0, MAC (6), address, netmask, gateway, DNS server (4 each) |
| `03` | `IFCONFIG` | mode (1 byte: 0 DHCP, 1 static), then address, netmask, gateway, DNS server | — |
| `04` | `EVENTS` | — | 2 bytes: a bit for each socket whose state has changed since the last `EVENTS` (bit n = socket n), low byte first. Clears `ATTN` |
| `10` | `SOCKET` | type (1 byte): 1 TCP, 2 UDP, 3 TLS, 4 DNS lookup, 5 ICMP (the W5500's IP-raw mode, protocol 1: `SENDTO` and `RECVFROM` carry whole ICMP messages, P32W builds them, the port is 0) | — (the reply's socket byte is the new socket) |
| `11` | `CONNECT` | TCP, TLS: address, port; TLS also the host name, for SNI and the certificate check. DNS: the name to look up | — (started: section 4) |
| `12` | `BIND` | port (0: any) | — |
| `13` | `LISTEN` | backlog (1 byte) | — |
| `14` | `ACCEPT` | — | the peer's address and port; the reply's socket byte is the new, connected socket. `AGAIN` if none is waiting |
| `15` | `SEND` | the data | 2 bytes: how many were taken (`AGAIN` if none could be) |
| `16` | `RECV` | 2 bytes: the most to return | the data. `AGAIN` if none is there yet; `OK` with none at the end of the stream. DNS: the 4-byte address |
| `17` | `SENDTO` | UDP: address, port, the data | 2 bytes: how many were sent |
| `18` | `RECVFROM` | UDP: 2 bytes, the most to return | the sender's address and port, then the data. `AGAIN` if none |
| `19` | `SHUTDOWN` | — | — (TCP, TLS: no more sending; the peer sees the end of the stream) |
| `1A` | `CLOSE` | — | — (the socket is free again) |
| `1B` | `STATE` | — | 14 bytes: state, error, bytes ready to read (2; a listening socket: connections waiting), room to send (2), local port (2), peer address (4), peer port (2). The four 2-byte counts and ports here are numbers, low byte first; the address is in network order |

The coprocessor has **8 sockets**, numbered 0 to 7 (one for each W5500
socket). A TLS socket takes one, and a DNS lookup takes one while it
runs.

**Socket states** (`STATE`'s first byte):

| Value | State |
|---|---|
| 0 | free |
| 1 | new (made, not yet used) |
| 2 | connecting (TCP and TLS: the handshakes; DNS: the lookup) |
| 3 | connected (DNS: the answer is ready) |
| 4 | listening |
| 5 | closed by the peer (data may still be waiting to be read) |
| 6 | failed (`STATE`'s second byte is the error, a status code) |
| 7 | bound (UDP: ready to send and receive) |

## 4. Events and waiting

`CONNECT` only starts a connection, because a handshake can take seconds
and the mailbox is shared by every socket. The coprocessor sets `ATTN`
when anything a process might be waiting for changes on any socket:
- a connection made or failed;
- a DNS answer;
- data arriving;
- room to send again;
- a connection waiting to be accepted;
- the peer closing.

The driver then sends `EVENTS` and wakes the processes waiting on those
sockets, which look again with `STATE`, `RECV` or `ACCEPT`. Without
interrupts, the driver can poll `ATTN`.

## 5. Status codes

The reply's first byte. The driver turns them into P32W's errno values.

| Code | Name | errno | Meaning |
|---|---|---|---|
| 0 | `OK` | | done |
| 1 | `AGAIN` | `EAGAIN` | nothing yet: no data, no room, no connection waiting |
| 2 | `INVAL` | `EINVAL` | a bad request: no such socket, or not in that state |
| 3 | `NOSOCK` | `ENOBUFS` | all 8 sockets are in use |
| 4 | `REFUSED` | `ECONNREFUSED` | connection refused |
| 5 | `TIMEDOUT` | `ETIMEDOUT` | no answer |
| 6 | `UNREACH` | `EHOSTUNREACH` | no route to the host |
| 7 | `RESET` | `ECONNRESET` | the connection was reset by the peer |
| 8 | `NOTCONN` | `ENOTCONN` | not connected |
| 9 | `INUSE` | `EADDRINUSE` | the port is in use |
| 10 | `NONAME` | (the resolver's "host not found") | DNS: no such name |
| 11 | `TLS` | `ECONNABORTED` | the TLS handshake failed, or the certificate is not trusted |
| 12 | `NOLINK` | `ENETDOWN` | no network: the link is down, or no address yet |
| 13 | `TOOBIG` | `EMSGSIZE` | a payload over 1024 bytes |
| 14 | `BADCMD` | `EINVAL` | an unknown command |

## 6. The emulators' model (`-N`)

The model carries the commands out with the Mac's sockets, so P32W in the
emulator really reaches the network:
- **The interface it reports:** address 10.0.2.15/24, gateway 10.0.2.2,
  DNS server 10.0.2.3, MAC 02:50:33:32:00:01, link up, by DHCP. These are
  QEMU's user-networking numbers.
- **10.0.2.2 is the Mac** (connections to it go to 127.0.0.1), so tests
  can run their own servers. Other addresses are reached as they are.
- **Listening:** P32W port `p` becomes the Mac's `p + 8000` when `p` is
  below 1024 (80 is 8080, 23 is 8023), else `p`; `-Nport OFFSET` changes
  the 8000. On the Mac's loopback only.
- **DNS** uses the Mac's resolver (IPv4 answers).
- **TLS** goes through `tools/p32tls.py` (Python's `ssl`: the Mac's
  certificate store, SNI, and the host-name check). `-Nca FILE` gives
  another set of trusted certificates, so tests can use a certificate of
  their own.
- **No loopback:** P32W cannot connect to its own address, as on the W5500.
- **Waiting:** while a socket is busy, an idle P32W (`WAIT`) waits in real
  time for the Mac's sockets as well as the timer, so emulated time does
  not run ahead of the network.
