# USB thumb drive — design study (2026-10-16)

Status: **decided (D43); the emulator model and the driver are done**
(2026-10-16). The model is the CH376S's sector mode in `emulator/p32io.h`
(`-U`, `-Uout`; `emulator/test/usb_model.c`). The driver is `kernel/usb.c`:
`/dev/usb` and `/dev/rusb`, usb(4), sessions 17 and 18. The FAT tools
are done too (2026-10-17, D44): `software/cmd/dos.c`, dos(1), session 19,
`software/tests/fat_test.sh`. The hardware is two CH376S ports on the I/O
card (D50, [architecture.md](architecture.md) §3).

**Decided** (Ken, 2026-10-16; D43):
- USB goes on the **I/O card** (section 2): two ports, each a CH376S.
- **The CH376S is plan A**, with the Pico + TinyUSB as plan B.
- **FAT through user-space tools only** (`dosdir`, `doscp`, `dosrm`,
  `dosformat` on `/dev/rusb`). No FAT inside the kernel, so no VFS.

From Ken: "I also want to explore USB" — for **storage**: a thumb drive as
a removable disk, to move files to and from the Mac and for extra storage
or backups. Keyboards, hubs and P32W-as-a-USB-device are out of scope.

## 1. The controller

Three ways to give P32W a USB host port were compared. Sources: the WCH
CH376 datasheet and its `CH376INC.H`, the Fuzix CH375/CH376 driver, and
TinyUSB's `msc_host` and the Pico datasheet. Throughput figures are
estimates: nobody publishes sector rates for either.

| | **CH376S module** | Pico (RP2040) + TinyUSB | bare MAX3421E |
|---|---|---|---|
| USB protocol runs on | the CH376S (fixed firmware) | the Pico (our firmware) | **P32W's CPU** |
| Bus interface | 8-bit parallel, 5 V, connects straight to the HCT bus | 3.3 V: latches, flags, level shifting, PIO firmware | SPI |
| Glue chips | 3–4 | 6–8 | — |
| P32W driver | ~150 lines: a command/status state machine | a protocol we define | a whole USB stack |
| Throughput (est.) | 150–250 KB/s | 300–500 KB/s (P32W is the limit) | — |
| Drive compatibility | fixed; known mount quirks; no hubs | fixable in firmware; hubs work | — |
| Cost | module about €6 | Pico about $5 + chips | — |

**Recommendation: the CH376S in parallel mode.**
- It sits directly on the 5 V byte bus. Its timing needs are tiny next to
  the I/O card's strobes: write pulse ≥ 30 ns, read pulse ≥ 40 ns, ≥ 1.5
  µs after a command byte, ≥ 0.6 µs between data bytes. At about 1 µs an
  instruction, P32W meets those by itself.
- It needs little glue, and there are working drivers that use exactly
  this sector command set (Fuzix, RomWBW).
- It is simple to model in the emulators: two ports and a small state
  machine.

The Pico stays **plan B**, if too many drives turn out not to work with
the CH376S. The driver keeps the controller behind one small interface,
so changing to the Pico later changes the driver's bottom layer only.
Ruled out: VNC2 (end of life), FT311D (Android accessories, not storage),
MAX3421E (the USB stack would run on P32W).

## 2. Hardware

**On the I/O card** (D50): two USB-A ports, each a CH376S module, beside
the UARTs, the timer, the RTC and the network coprocessor. The card holds,
for USB:
- **each device's select** within the I/O card's window, byte accesses
  only, and `-ACK`, decoded with the card's other devices;
- **the byte lane** `D7–D0`, shared with the card's other byte devices;
- **the interrupt:** both ports onto `-IRQ6` (a jumper sets the level).
  The interrupt gate and the data buffer are **HCT**, because the module's
  outputs may reach only about 3.3 V;
- **two CH376S modules:** the common "CH376S U-disk module", with 2.54 mm
  headers and a USB-A socket, in socket strips (D21), the sockets at the
  card's front edge. The TXD-to-GND jumper selects parallel mode at reset.

**Registers** (in the I/O card's window, D52):

| Address | Read | Write |
|---|---|---|
| `0xFFFF_FF50` | USB 0: data byte | data byte |
| `0xFFFF_FF51` | USB 0: status (bit 7: interrupt pending, active low; bit 4: busy) | command byte |
| `0xFFFF_FF54` / `FF55` | USB 1: the same | the same |

The I/O card's interrupt status (`FFFF_FFF4`) has bit 6 for USB 0 and
bit 7 for USB 1.

**From the datasheet's application circuit** (CH376DS1, section 8.1, "USB
Flash Drive applications, 5V power supply"; Ken's copy):
- **Parallel mode:** TXD is tied to GND, the other mode pins are left
  unconnected. The signals are D0–D7 (pins 15–22), A0 (8), RD# (4), WR#
  (3), PCS# (27) and INT# (1).
- **RSTI (pin 2) is active high,** with a built-in pull-down: left open,
  the chip runs. (The research found the pin table contradicting this; the
  application circuit's text settles it.)
- **INT# drives only a weak high while the chip is in reset;** the
  datasheet recommends a 2–5 kΩ pull-up.
- **The parts around the chip** are the module's: a 12 MHz crystal, 0.01
  µF on V3 (at 5 V), 0.1 µF + 100 µF on VCC, and the USB socket with VBUS
  fed from VCC through a small resistor (R2). The drive takes its current
  from the backplane's 5 V (budget 200 mA).

**The module** (its schematic, from Ken; it is drawn with a CH375, which
has the same pinout):
- **A 16-pin header (8×2)** carries everything the parallel interface
  needs: D0–D7 on the odd pins 1–15, GND on 2 and 4, +5 V on 6, INT# 8,
  A0 10, CS# 12, RD# 14, WR# 16. Parallel mode: the 3-pin jumper P_S,
  pins 1–2, tying TXD to GND.
- **RSTI is not on the header.** The module has its own power-on reset
  (0.47 µF to its 3.3 V rail), so the card cannot reset the chip, and a
  P32W reboot without a power cycle does not reset it either. The driver
  sends `RESET_ALL` first, always.
- **The chip runs at 3.3 V,** from an LM1117-3.3 on the module (the USB
  socket gets +5 V directly). Its outputs reach about 3.3 V. That is
  enough for 74HCT inputs (2.0 V threshold), so the data buffer and the
  interrupt gate must be **HCT, not HC**. INT#'s pull-up must
  not go to 5 V.
- **Run at 5 V (Ken, 2026-10-16):** the module's LM1117 is bridged
  (input to output), or its 5 V/3.3 V jumper set if it has one, so the
  CH376S runs at 5 V as in the datasheet's own circuit (8.1). Its V3 pin
  already has that circuit's 10 nF to ground. Then the chip's outputs are
  full 5 V levels and its inputs take 5 V: no level shifting, and the
  question of 5 V tolerance at 3.3 V does not arise. (The HCT parts stay:
  they work either way.)

**To check on a real module before the I/O card's PCB is drawn:**
- the header pinout above, against the real board;
- whether it has a 5 V/3.3 V jumper, or the LM1117 must be bridged; and
  the datasheet's word on 5 V inputs at 3.3 V (its electrical section);
- that its P_S jumper is on pins 1–2 (parallel mode).

## 3. Software

1. **An emulator model** of the CH376S in `p32io.h` (so in both
   emulators), backed by an image file: `-U usb.img` plugs a drive in, and
   a key or option pulls it out. It runs the status codes and the
   64-byte-chunk protocol below, so the driver is written and tested
   before the card exists, as for the RTC (D23).
2. **A block driver,** `bdevsw` 2 (`/dev/usb`) and a character device
   (`/dev/rusb`). A 1 KiB block is two 512-byte sectors.
   - **Initialise:** `RESET_ALL`; `CHECK_EXIST` (0x55 → 0xAA);
     `SET_USB_MODE` 7, then 6 (host).
   - **When a drive is plugged in** (`USB_INT_CONNECT`): `DISK_INIT`,
     which Fuzix uses because `DISK_MOUNT` may reject a stick that isn't
     FAT; retry about 5 times. Then `DISK_CAPACITY`.
   - **Read:** `DISK_READ` with the LBA (4 bytes) and a count. Then, eight
     times per sector: wait for `USB_INT_DISK_READ`, `RD_USB_DATA0`, read
     64 bytes, `DISK_RD_GO`. Finish on `USB_INT_SUCCESS`.
   - **Write:** the same, with `DISK_WRITE`, `WR_HOST_DATA` and
     `DISK_WR_GO`.
   - **Timeouts:** every wait has one, so a drive pulled out mid-transfer
     gives `EIO`, never a hang.
   - **Removal:** a disconnect discards the drive's cached blocks, and
     later accesses fail until a drive is plugged in again.
   - Polled at first, as the CF driver is. `IRQ6` is used for
     plug and unplug, and later to sleep instead of poll.
3. **P32W filesystem on a stick:** `mkfs /dev/usb`, then `mount /dev/usb
   /mnt` (mount and umount exist already). `sync` or `umount` before
   pulling it out.
4. **FAT tools** for the Mac, mtools-style, in user space on `/dev/rusb`:
   `dosdir`, `doscp` (both ways), `dosrm`, `dosformat`. FAT32 first; FAT16
   for small sticks.
5. **Not planned:** FAT mounted directly (it would need a filesystem
   switch, a VFS, in the kernel; Ken chose the tools). Plan B, the Pico,
   only if drive compatibility is poor.

**Speed (estimate):** about 150–250 KB/s, against about 800 KB/s from the
CF card (D37). A 1.5 MB file would take under ten seconds.

## 4. Tests

- **The emulator model under `make test`:** `mkfs`, `mount`, copy files
  both ways, `fsck`, `umount`.
- **Pulling the drive out** mid-copy: the copy fails with an error, the
  system carries on, and plugging the drive back in works.
- **FAT tools:** a FAT32 image made on the Mac (`newfs_msdos`/`hdiutil`)
  is read on P32W, and one written on P32W is read back by the Mac's tools,
  as the `zip` tests do with Finder's archives.
- **On the bench:** a handful of real sticks of different makes and sizes,
  noting which ones mount.

## 5. Questions for Ken

1. ~~Where does USB go?~~ **Decided: on the I/O card**, two ports (D50).
2. ~~CH376S or Pico?~~ **Decided: the CH376S is plan A**, the Pico plan B.
3. ~~FAT tools or a FAT filesystem?~~ **Decided: user-space tools only.**
4. **Buy one module** to check its pinout before the PCB layout?
