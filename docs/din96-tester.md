# The DIN 96 shorts tester

**Status:** DESIGNED (D129, 2026-11-16; Ken: "a din 96 short connector that could be connected to any din 96
connector for P32W or YACC1 and detect shorts"): the netlist `generators/card_din96.py`, the board
`generators/p32din96.py` (4 layers, routed), the firmware `hardware/din96-tester/firmware/`. Its logic is tested
on the Mac and the sketch builds for the Nano; **nothing has run on a real one.** Not ordered.

A bench tool, not a card of the machine: it is here because P32W's generators make it, and YACC1 and any other
system on 96-pin DIN 41612 connectors can use it.

## 1. What it is for

When a board has had its connectors soldered and nothing else (no chips, no resistors, no capacitors), the
tester is pushed onto **one** 96-pin connector and finds every pin joined to another: a solder bridge between
neighbours, two bus lines shorted, a line shorted to a supply. **It is a shorts tester and no more**, and it is
never used on a board with power on it.

It is the check the debug card cannot make (D128): it reaches all 96 pins of a connector, the spares and the
supply pins among them, and it works on a card as well as on a backplane.

## 2. Using it

1. Turn on the **one lever of TYPE** that is the connector's (each lever's connector is printed beside it):

   | Lever on | Connector | Its supply pins |
   |---|---|---|
   | 1 | P32W J1 | +5 V: rows 1–2; ground: rows 31–32 and row b's odd pins |
   | 2 | P32W J2 | as J1, and row b's even pins are +5 V too |
   | 3 | P32W J3 | as J1; b28 and b30 may be tied to ground (the CPU and MMU / data cards' "I am in" pins) |
   | 4 | YACC1 X1 | ground: pins 1 and 32 of each row; +5 V: pins 2 and 31 |
   | 5 to 8 | spare | for connectors to come (Ken: "8 position switch for future expansion") |
   | none | nothing plugged in | none: no pin may be joined to another (the tester's check of itself) |

   A lever is a connector, not a number to add up (Ken: "The switch is labeled 1-2-4-8 but types is
   0,1,2,3,4"). With two levers on, or a spare one, there is no test: FAIL lights and the serial port says to
   set one.

2. Plug it on. **The plug** (X1, the part a card has) goes into a backplane's socket; **the socket** (X2, the
   part a backplane has) takes a card's plug. Only one of the two at a time.
3. Give it power: the Nano's USB socket, from a computer, a charger or a battery pack.
4. Press **TEST**. About a quarter of a second later **PASS** (green) or **FAIL** (red) is lit, and stays lit
   until the next test.

With a computer on the USB cable, the serial port (115200) says what was found, each pin by its place and its
name:

```
TYPE: P32W J2
joined to each other: a16 (-BREQ), a17 (-BGNT)
FAIL: 1 fault
```

`t` and Return there tests as the button does; `?` says which type the switch names.

**On a backplane** a bused line is the same pin in every slot, so one test of a connector in any slot covers
those lines for the whole board. The lines that reach only some slots (P32W's request and grant lines, and J3
beyond slot 2) are seen only from a socket they reach.

**What PASS means:** the +5 V pins are joined to each other, the ground pins to each other, the two groups are
apart, and no other pin is joined to anything. **What it cannot see:** a net that does not come to this
connector; a break in a signal line; anything on a board with parts fitted (a resistor or a chip joins pins as
far as it can tell).

**If both LEDs blink in turn**, an expander does not answer (the serial port says which): the tester is at
fault, not the board.

## 3. How it works

**Six MCP23S17 port expanders** (U1–U6), sixteen pins each, have one pin on each of the connector's 96. Every
pin is an input with the expander's own pull-up (about 100 kΩ) and reads high. The Nano makes one pin an
output, low, waits a millisecond, reads all 96 and lets go: a pin that reads low is joined to the one pulled.
After 96 such steps it has the groups of joined pins. The connector's type says which pins are +5 V and which
ground; the largest group of each is taken as that supply, so a single supply pin that has come apart is the
one reported, and everything else must stand alone.

Before the 96 steps it reads once with nothing pulled: a pin that is low then is held low from outside (power
on the board, or a part fitted). During them, a pin that will not go low when pulled is fighting something.
Both are faults, with their own words.

**The plug and the socket are wired pin for pin**, and each pin's expander lies beside it on the board, so a
pin's track runs level from the plug to its expander pin and on to the socket.

**The Nano** selects the expanders one by one over SPI (D5–D10; no expander has an address), reads TEST (D2)
and the TYPE switch (A0–A7, a lever each: the one that is on is the connector's type; RN1 pulls each line up,
since the Nano's A6 and A7 have no pull-ups of their own and are read as voltages), and lights PASS (D3) and
FAIL (D4). Its 5 V pin
powers the board: about 50 mA in all. Its reset line resets the expanders too.

**The types are tables, not code.** `hardware/din96-tester/pins/` has a file a system, in the form every
system's bus definition already prints (`CONNECTOR PIN SIGNAL`): `p32w.txt` is written from
`generators/p32bus.py`, with the pins a card ties to its ground marked from the cards' netlists; `yacc1.txt` is
a copy of what YACC1-D's `tools/bus_pins.py` prints. `card_din96.py --firmware` turns them, and the place of
each connector pin on the expanders, into `profiles.h`; the sketch has no pin number of its own. Another
system: its table in that folder, a line in `TYPES`, and it has the next spare lever; build again. The lever's
name on the board is then out of date until the board is made again: write it on.

## 4. Parts

| Ref | Part | For |
|---|---|---|
| X1 | DIN 41612 C 96, plug, right angle (the cards' part) | into a backplane's socket |
| X2 | DIN 41612 C 96, socket, straight (the backplane's part) | onto a card's plug |
| A1 | Arduino Nano (5 V, ATmega328P), on two 15-way socket strips | the test, USB power and the report |
| U1–U6 | MCP23S17-E/SP (DIP-28, narrow), in sockets | a pin of each on every connector pin |
| C1–C6, C7 | 100 nF (0.1 inch leads), 10 µF | bypass |
| SW1 | push button, 6 mm | TEST |
| SW2 | DIP switch, 8 levers | TYPE: a lever a connector; four spare |
| RN1 | 10k × 8, bussed (SIP-9) | the TYPE levers' pull-ups |
| D1, D2, D3 | LED 3 mm: yellow, green, red (the colour is on the board beside each) | POWER, PASS, FAIL |
| R1; R2, R3 | 1k; 470 Ω | the LEDs' resistors |

All through-hole. The board: 106 × 112 mm, 4 layers (signals outside, ground and +5 V inside). How it looks:
[the top](../hardware/din96-tester/kicad/din96-tester-render-top.png), in `hardware/din96-tester/kicad/` with
the schematic, the placement drawing and the order files.

## 5. The firmware

`hardware/din96-tester/firmware/din96_tester/`: `din96_tester.ino` (the Nano's side: SPI, the button, the
LEDs, the serial port), `core.h` (the logic, with nothing of the Nano in it) and `profiles.h` (written by the
generator). In the Arduino IDE: open the sketch, board **Arduino Nano**, processor **ATmega328P** (a clone may
want "Old Bootloader"), upload. It needs no library but the IDE's own SPI. About 8 KB of the Nano's 30, and
400 bytes of its 2 KB of memory.

## 6. Verified

- **The netlist** (`card_din96.py`, in `make test`): the electrical rules every card is held to; every
  connector pin on exactly one expander pin, the plug and the socket pin for pin; `profiles.h` and `p32w.txt`
  as the generator writes them today.
- **The logic** (`emulator/test/din96_test.c`, in `make test`): `core.h` against made-up boards: a good J1, J2
  and J3; signals joined in twos and threes; a signal on a supply; the supplies joined; a supply pin apart, and
  one apart and on a signal; J3's "I am in" pins tied to ground (no fault) and joined to each other (a fault);
  a pin held low; a pin that will not pull; and **every one of the 4560 pairs of pins** joined in turn on a
  good J2, each found.
- **The sketch builds** for the Nano with the Arduino IDE's own tools.
- **The board**: KiCad's ERC and DRC, the schematic against the netlist, and `pcb-check`.

**Not verified, and to do on the first one:** that it works at all on a real Nano and real expanders; the
millisecond's wait (a long backplane line with nothing fitted is a few hundred picofarads on 100 kΩ, tens of
microseconds: it should be ample); which way round the TYPE switch's "on" is on the part bought (the board
prints each lever's connector, the part its own ON); and, as for the backplane, that pin a1 of a real plug and a
real socket is where KiCad's footprints have it. A swap would not hide a short: it would name the wrong pins
and upset PASS and FAIL.
