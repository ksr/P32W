# The DIN 96 shorts tester

A bench tool for any system on 96-pin DIN 41612 connectors (P32W, YACC1): plugged onto one connector of a board
that has nothing on it yet but its connectors, it finds every pin joined to another. An Arduino Nano, six
MCP23S17 port expanders, a TEST button, a green and a red LED. What it is, how to use it and how it works:
[docs/din96-tester.md](../../docs/din96-tester.md) (decisions D129, and D130: its check before the order).

- `kicad/`: the schematic, the board and its order files, made by `generators/card_din96.py` (the netlist) and
  `generators/p32din96.py` (the board): `sh generators/build_schematics.sh din96-tester`, then
  `sh generators/build_board.sh din96-tester`. Generators are canon: do not edit the board by hand.
- `firmware/din96_tester/`: the Arduino sketch. `profiles.h` is written by `python3 generators/card_din96.py
  --firmware`; the other two files are edited by hand. `make test` (in `emulator/`) runs its logic on made-up
  boards (`emulator/test/din96_test.c`) and the sketch itself against a model of this board
  (`emulator/test/din96_sim.cc`).
- `pins/`: a pin table a system (`CONNECTOR PIN SIGNAL`), which the connector types are made from. `p32w.txt` is
  written from P32W's bus definition; `yacc1.txt` is a copy of what YACC1-D's `tools/bus_pins.py` prints.

Another system: put its pin table in `pins/`, add its connector to `TYPES` in `generators/card_din96.py`, run
`--firmware`, and load the sketch again.
