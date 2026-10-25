#!/usr/bin/env python3
"""The P32W backplane bus (9U x 340 mm cards, D49, D50) -- the canonical
definition: every netlist, PDF and document is generated from this file.

It is a system bus, not the CPU's insides: the backplane carries only
physical memory cycles, interrupts, arbitration, clock and power. The CPU's
microcode fields stay on the CPU card; the one exception is J3 in slots 1
and 2, the local link between the CPU card and the MMU / data card.

Three 96-pin DIN 41612 connectors per card, rows A/B/C, pins 1-32:
  J1  the system bus cycle: data, physical address, byte enables, strobes,
      -ACK and -WAIT
  J2  system control: clock, reset, interrupts, bus arbitration
  J3  slots 1-2: the CPU <-> MMU/data local link; slots 3-10: power only
      (signal pins not connected: reserved)

Conventions (as P8X's):
  * pins 1-2 of every row = VCC, pins 31-32 of every row = GND
  * row B odd pins 3-29 = GND: a ground guard between rows A and C
  * J2 row B even pins 4-30 = VCC as well (D57): with the odd pins' GND,
    alternate supply pins down the middle of J2, for a 9U card's current
  * active-low signals have a leading dash (-RES, -MRD)
  * open-drain lines (-ACK, -WAIT, -LOCK, -IRQn) are wired-OR with
    pull-ups on the backplane
  * a radial line runs from one slot to one pin of the arbiter's slot;
    it is not bused

    python3 p32bus.py --check            # validate
    python3 p32bus.py --doc FILE         # write the bus document (Markdown)
"""
import os, sys, argparse

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "microcode"))
import p32ucode  # noqa: E402  (the local link's field widths are checked against the microcode)

STATUS = "PROPOSED (2026-10-25, D50, D52, D57; Ken accepted the recommendations it is built on)"
CONNECTORS = ("J1", "J2", "J3")
ROWS = "ABC"
NSLOTS = 10
CPU_SLOT, MMU_SLOT = 1, 2                     # the arbiter is on the MMU / data card
DEV_SLOTS = range(3, NSLOTS + 1)              # slots that may request the bus

def group(prefix, n, start=0):
    return ["%s%d" % (prefix, i) for i in range(start, start + n)]

# ---------------------------------------------------------------- signals
# name -> (scope, driven by, read by, meaning). scope: "bus" (every slot),
# "local" (J3, slots 1-2 only), "own" (every device slot, a radial line's
# far end), "arb" (the radial ends, in slot 2 only).
SIGNALS = {}
def sig(names, scope, drv, rcv, desc):
    for n in names:
        SIGNALS[n] = (scope, drv, rcv, desc)

# J1: the cycle
sig(group("D", 32), "bus", "the master (write) or the addressed card (read)", "all",
    "data bus")
sig(group("A", 30, 2), "bus", "the master", "all", "physical address bits 31:2")
sig(group("BE", 4), "bus", "the master", "all", "byte enables (lanes 0-3; lane n = D[8n+7:8n])")
sig(["-MRD"], "bus", "the master", "all", "read cycle: the addressed card drives D")
sig(["-MWR"], "bus", "the master", "all", "write cycle: the addressed card takes D (enabled lanes)")
sig(["-ACK"], "bus", "the addressed card", "the master",
    "open-drain: a card decodes A, in the cycle's first clock; none -> BUSERROR")
sig(["-WAIT"], "bus", "the addressed card", "the master",
    "open-drain: stretch this cycle by one clock while low (a wait state)")
# J2: control
sig(["CLK", "-CLK"], "bus", "CPU", "all", "the system clock (the microcycle) and its inverse")
sig(["-RES"], "bus", "CPU", "all", "reset")
sig(["PWROK"], "bus", "power supply", "CPU", "power good (ATX PWR_OK); low = reset, falling = power failing")
sig(["-LOCK"], "bus", "the master", "arbiter",
    "open-drain: keep the bus for the next cycle too (an atomic read-modify-write)")
sig(["-IRQ%d" % i for i in range(8)], "bus", "devices", "CPU",
    "open-drain interrupt request levels (IRQ0 = timer; 7 highest)")
sig(["-IACK"], "bus", "the master (the MMU / data card for the CPU)", "devices",
    "interrupt acknowledge cycle (D57): instead of -MRD, with A4:A2 = the level; the card "
    "driving that level answers -ACK and puts its vector on D7:0")
sig(["-BREQ"], "own", "the card", "arbiter", "this slot asks for the bus (radial)")
sig(["-BGNT"], "own", "arbiter", "the card", "this slot is master from the next clock (radial)")
sig(["-BREQ%d" % s for s in DEV_SLOTS], "arb", "slot n's card", "arbiter", "slot n's -BREQ")
sig(["-BGNT%d" % s for s in DEV_SLOTS], "arb", "arbiter", "slot n's card", "slot n's -BGNT")
# J3: the local link (slots 1-2)
sig(group("LR", 32), "local", "CPU or MMU/data (RSRC)", "CPU, MMU/data",
    "the CPU's result bus R, extended to the MMU / data card: addresses to MAR, store data out, "
    "load data (steered), BADADDR and the MMU's control registers in")
sig(group("RSRC", 4), "local", "CPU", "MMU/data", "R's source, encoded (microword RSRC)")
sig(group("RDST", 3), "local", "CPU", "MMU/data", "R's destination, encoded (microword RDST)")
sig(group("MEM", 3), "local", "CPU", "MMU/data", "memory cycle type (microword MEM)")
sig(["SIZE0", "SIZE1", "SIGNED"], "local", "CPU", "MMU/data", "access size and sign-extension (from IR)")
sig(["SUPER"], "local", "CPU", "MMU/data", "STATUS.S: 1 = supervisor (untranslated)")
sig(["TLBW"], "local", "CPU", "MMU/data", "write TLBHI/TLBLO into the TLB")
sig(group("CRSEL", 3), "local", "CPU", "MMU/data", "hardware control register for RSRC / RDST = CRHW")
sig(["FAULT"], "local", "MMU/data", "CPU", "this memory cycle faulted: abort it, enter trap")
sig(group("FC", 4), "local", "MMU/data", "CPU", "fault cause")
sig(["-WSTB"], "local", "CPU", "MMU/data", "write strobe: gates every SRAM write")
sig(["STALL"], "local", "MMU/data", "CPU",
    "hold the microcycle: the bus is not yet ours (arbitration) or the cycle is waiting (-WAIT)")
sig(["LOCKREQ"], "local", "CPU", "MMU/data", "this access and the next are one atomic pair (-LOCK)")
sig(["PCINC", "MARPC"], "local", "CPU", "MMU/data",
    "the PC's +4 and MAR <- PC: the MMU / data card keeps a shadow PC (it loads from R with the PC "
    "and counts with it), so the next fetch's address needs no R cycle")
sig(["CCLK"], "local", "CPU", "MMU/data",
    "the CPU's clock: CLK's cycles in which the CPU commits (none while STALL, halted or between steps)")

# ---------------------------------------------------------------- pin assignment
ASSIGN = {
    ("J1", "A"): group("D", 28),                                          # A3-A30
    ("J1", "C"): group("D", 4, 28) + group("A", 24, 2),                   # C3-C30
    ("J1", "B"): group("A", 6, 26) + group("BE", 4) +
                 ["-MRD", "-MWR", "-ACK", "-WAIT"],                       # even B4-B30
    ("J2", "A"): ["CLK", "-CLK", "-RES", "PWROK", "-LOCK"] +
                 ["-IRQ%d" % i for i in range(8)] +
                 ["-BREQ", "-BGNT", "-IACK"],                            # A3-A18
    ("J2", "C"): ["-BREQ%d" % s for s in DEV_SLOTS] +
                 ["-BGNT%d" % s for s in DEV_SLOTS],                      # C3-C18
    ("J2", "B"): [],
    ("J3", "A"): group("LR", 28),
    ("J3", "C"): group("LR", 4, 28) + group("RSRC", 4) + group("RDST", 3) + group("MEM", 3) +
                 ["SIZE0", "SIZE1", "SIGNED"] + group("CRSEL", 3) + ["SUPER", "TLBW"],
    ("J3", "B"): ["FAULT"] + group("FC", 4) + ["-WSTB", "STALL", "LOCKREQ", "CCLK", "PCINC", "MARPC"],
}

def _pins(row):
    if row == "B":
        return ["B%d" % n for n in range(4, 31, 2)]                       # even 4-30
    return ["%s%d" % (row, n) for n in range(3, 31)]                      # 3-30

_MAP = {}
for (conn, row), names in ASSIGN.items():
    pins = _pins(row)
    assert len(names) <= len(pins), (conn, row, len(names))
    for name, pin in zip(names, pins):
        _MAP[(conn, pin)] = name

def pinnet(conn, pin):
    """The signal assigned to a connector pin (the same on every card)."""
    row, n = pin[0], int(pin[1:])
    assert conn in CONNECTORS and row in ROWS and 1 <= n <= 32, (conn, pin)
    if n in (1, 2):
        return "VCC"
    if n in (31, 32):
        return "GND"
    if row == "B" and n % 2 == 1:
        return "GND"
    if conn == "J2" and row == "B":
        return "VCC"                                                       # D57
    return _MAP.get((conn, pin), "SPARE_%s%s" % (conn, pin))

def busnet(conn, pin, slot):
    """The backplane net on this pin of this slot, or None (not connected).
    Bused signals keep their name; a radial line is named after the device
    slot (-BREQ in slot 5 and -BREQ5 in slot 2 are one net, -BREQ5)."""
    name = pinnet(conn, pin)
    if name not in SIGNALS:
        return name if name in ("VCC", "GND") else None                    # spares: not connected
    scope = SIGNALS[name][0]
    if scope == "bus":
        return name
    if scope == "own":
        return "%s%d" % (name, slot) if slot in DEV_SLOTS else None
    if scope == "arb":
        return name if slot == MMU_SLOT else None
    if scope == "local":
        return "L_" + name if slot in (CPU_SLOT, MMU_SLOT) else None
    raise AssertionError(scope)

ALLPINS = [(c, "%s%d" % (r, n)) for c in CONNECTORS for r in ROWS for n in range(1, 33)]

# ---------------------------------------------------------------- checks
def check():
    errors, seen = [], {}
    for conn, pin in ALLPINS:
        net = pinnet(conn, pin)
        if net in SIGNALS:
            if net in seen:
                errors.append("%s on %s and %s %s" % (net, seen[net], conn, pin))
            seen[net] = "%s %s" % (conn, pin)
    for name in SIGNALS:
        if name not in seen:
            errors.append("signal %s has no pin" % name)
    widths = dict(p32ucode.FIELDS)
    for prefix, fieldname in (("RSRC", "RSRC"), ("RDST", "RDST"), ("MEM", "MEM")):
        n = len([s for s in SIGNALS if s[:len(prefix)] == prefix and s[len(prefix):].isdigit()])
        if n != widths[fieldname]:
            errors.append("%s: %d link lines, microword field %s is %d bits" % (prefix, n, fieldname, widths[fieldname]))
    # every radial line has exactly two ends: its device slot and the arbiter's slot
    ends = {}
    for slot in range(1, NSLOTS + 1):
        for conn, pin in ALLPINS:
            net = busnet(conn, pin, slot)
            if net and net[:5] in ("-BREQ", "-BGNT"):
                ends.setdefault(net, []).append(slot)
    for net, slots in ends.items():
        if len(slots) != 2:
            errors.append("radial %s has %d ends (slots %s)" % (net, len(slots), slots))
    return errors, seen

def counts():
    c = {"signal": 0, "VCC": 0, "GND": 0, "spare": 0}
    for conn, pin in ALLPINS:
        net = pinnet(conn, pin)
        if net in ("VCC", "GND"):
            c[net] += 1
        elif net.startswith("SPARE_"):
            c["spare"] += 1
        else:
            c["signal"] += 1
    return c

# ---------------------------------------------------------------- document
TIMING = """\
## The bus cycle

The bus is **synchronous**: one clock (`CLK`, the CPU's microcycle, 5 MHz
planned, 3 MHz as the safe setting) for every card. A cycle is one clock,
stretched by `-WAIT`:

```
          ┌─────┐     ┌─────┐     ┌─────┐
CLK  ─────┘     └─────┘     └─────┘     └────
     │ clock 1   │ wait (opt) │ next cycle
A, BE,    <══ driven by the master, held through the waits ══>
-MRD/-MWR
-ACK      ‾‾‾‾\\____________________/‾‾   the addressed card, within clock 1
-WAIT     ‾‾‾‾‾\\_______/‾‾‾‾‾‾‾‾‾‾‾‾‾   a slow card: low before clock 1 ends
D (read)  ───────────────<═ valid ═>──   taken at the edge that ends the cycle
```

1. **The master** drives `A2–A31`, `BE0–BE3` and `-MRD` or `-MWR` from the
   start of the clock (on a write, `D` too).
2. **The addressed card** pulls `-ACK` low within the first clock: address
   decoding is fast and always fits. No `-ACK` by the end of the first clock
   is a **BUSERROR**, so a missing card can never hang the bus.
3. **A slow card** also pulls `-WAIT` low before the clock ends. The master
   holds everything for another clock, and checks `-WAIT` again; when it is
   high at the end of a clock, that edge ends the cycle (a read's data is
   taken then; a write is complete).
4. **The CPU** sees a waiting cycle as `STALL` on the local link: its
   clock `CCLK` has no edge until the cycle ends, so nothing on the CPU
   side changes. The microcode does not change for slower cards.

## Bus mastership (DMA)

The **arbiter is on the MMU / data card** (slot 2), which is the CPU's own
master interface. Any card in slots 3–10 may become master:

- It pulls its radial `-BREQ` low. The arbiter grants at a cycle boundary,
  when the CPU's next microcycle does not use the bus (most do not: about
  two in six are memory cycles), and holds the CPU (`STALL`) only when the
  CPU wants the bus while a device has it.
- `-BGNT` low means the card is master **from the next clock**. It runs
  as many cycles as it likes and gives the bus back by releasing `-BREQ`;
  the arbiter then drops `-BGNT`.
- **Priority:** fixed, the higher slot first, among the requests present at
  the boundary.
- **`-LOCK`**: a master that holds `-LOCK` low keeps the bus for the next
  cycle too; the CPU uses it for an atomic read-modify-write (`LOCKREQ` on
  the local link), and the arbiter grants nothing while it is low.
- A DMA master addresses **physical** memory; it has no MMU. The kernel
  gives it physical buffers (and pins their pages while the transfer runs).

## Interrupts

- **`-IRQ0`–`-IRQ7`**, open-drain levels into the CPU's `IMASK` / `IPEND`
  logic; the lowest-numbered pending, enabled level is taken first (`IRQ0`,
  the timer, has the highest priority).
- Each card's **jumpers** choose the level each of its devices drives
  (D52). Nothing about an interrupt depends on the slot a card is in.
- **Vectored (D57).** When the CPU takes level *n*, the MMU / data card
  runs an **interrupt acknowledge cycle**: a bus cycle like a read, with
  `-IACK` low instead of `-MRD` and `A4:A2` = *n* (the other address bits
  and `BE0`–`BE3` don't matter). The card driving level *n* pulls `-ACK` and
  puts its **vector** on `D7:D0`: its window number (D52) × 16 + the number
  of its device that is asking (its interrupt status register's bit; the
  lowest pending one on that level). A level is driven by **one card** —
  the jumpers of two cards must not share one — though one card may drive
  several. No `-ACK` (no card, or one without vectors) is not a bus error:
  the vector is `0xFF`, and the kernel reads the cards' interrupt status
  registers instead. The CPU puts the vector into `CAUSE` bits 15:8.

## Slots

| Slot | Card | J3 |
|---|---|---|
| 1 | CPU | the local link |
| 2 | MMU / data (and the arbiter) | the local link |
| 3–10 | any: memory, disk, I/O, video, debug | power only |

Every card is placed by its **jumpers**, not its slot (D50, D52): memory
cards choose a 16 MiB window, device cards a 4 KiB window among the device
registers, and their interrupt levels. Apart from slots 1 and 2 (the local link), any
card works in any slot; the slot only sets its bus-request priority.
"""

def doc(path):
    errors, seen = check()
    if errors:
        sys.exit("bus definition errors:\n  " + "\n  ".join(errors))
    c = counts()
    L = ["# The P32W Bus", "",
         "**Status:** %s." % STATUS, "",
         "*Generated by `generators/p32bus.py` — edit the generator, not this file.*", "",
         "Three 96-pin DIN 41612 connectors per card (D49), down the 366.7 mm edge",
         "of a 9U × 340 mm card: **J1** the bus cycle (data, physical address, byte",
         "enables, strobes, `-ACK`, `-WAIT`), **J2** system control (clock, reset,",
         "interrupts, arbitration), **J3** the CPU ↔ MMU/data local",
         "link in slots 1 and 2 (power only in the others). Pins 1–2 of every row",
         "are VCC, 31–32 GND; row B odd pins 3–29 are GND (a guard between rows A",
         "and C); J2's row B even pins 4–30 are VCC as well (D57), so J2 carries",
         "alternate supply pins down its middle. Active-low signals have a leading",
         "dash; `-ACK`, `-WAIT`, `-LOCK`",
         "and `-IRQn` are open-drain with backplane pull-ups.", "",
         "The backplane does **not** carry the CPU's microcode fields: only",
         "physical memory cycles, so any card can be a bus master (DMA), any",
         "number of memory cards can answer, and a slow card can stretch a cycle.", "",
         "**Pins (per card):** %d signal, %d VCC, %d GND, %d spare (of 288)."
         % (c["signal"], c["VCC"], c["GND"], c["spare"]), "", TIMING]
    for conn in CONNECTORS:
        L += ["## %s" % conn, "", "| Pin | Row A | Row B | Row C |", "|---|---|---|---|"]
        for n in range(1, 33):
            L.append("| %d | %s | %s | %s |" % (n, *(pinnet(conn, "%s%d" % (r, n)) for r in ROWS)))
        L.append("")
    where = {"bus": "every slot", "own": "slots 3–10 (radial)",
             "arb": "slot 2 only (radial)", "local": "slots 1–2 only"}
    L += ["## Signals", "", "| Signal | Pin | Wired to | Driven by | Read by | Meaning |",
          "|---|---|---|---|---|---|"]
    for name, (scope, drv, rcv, desc) in SIGNALS.items():
        L.append("| `%s` | %s | %s | %s | %s | %s |" % (name, seen[name], where[scope], drv, rcv, desc))
    L.append("")
    with open(path, "w") as fh:
        fh.write("\n".join(L))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--doc")
    args = ap.parse_args()
    errors, _ = check()
    if errors:
        sys.exit("bus definition errors:\n  " + "\n  ".join(errors))
    if args.doc:
        doc(args.doc)
    c = counts()
    print("bus OK: %d signals on %d pins, %d spare, %d slots" % (len(SIGNALS), c["signal"], c["spare"], NSLOTS))

if __name__ == "__main__":
    main()
