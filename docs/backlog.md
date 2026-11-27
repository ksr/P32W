# P32W backlog

Work Ken has asked for that is not yet scheduled, and smaller follow-ons.
The software's milestones are done: the last, **M7, the native toolchain**
(D24, [os-design.md](os-design.md)), made P32W self-hosting on 2026-10-14.
The hardware's next step is **the first order**: the backplane, the debug
card, the memory card and the DIN 96 tester, with every card's netlist
designed and every board routed (`pcb-check`'s `orders/next-order.md` holds
the order). Items move into a design document (and a decision, D-number)
when they are taken up.

## Hardware still to design

### The board layouts (status 2026-10-31: every card routed)

All on 6 layers (D77), from `sh generators/build_board.sh <card>` (Freerouting
2.5 at `~/freerouting`: FRJAR, FRJAVA, else the system's Java 25+). Each
run places the card fresh; a try that leaves connections open is improved
only by placing again (ROUTE_TRIES) or another direction cost
(`KT_AGAINST`, D80), not by routing the same board again: Freerouting
cannot finish an already-routed board (D77), and it stops by itself once
ten passes bring nothing.

- **Backplane:** done, placed and routed by `p32backplane.py` (D79): 0 DRC
  violations, 0 unconnected. Verify the sockets' 180° turn with a real
  card and socket before ordering.
- **I/O card:** routed (D80: a row per function), **0 unconnected**; its
  14 DRC "violations" are cosmetic (DB9 outlines past the front edge, silk
  clipped by pads, the P32W footprint library not registered). Routed at
  `KT_AGAINST=1`.
- **Memory card:** routed (D80: its grid spread across the card), **0
  unconnected, 0 violations**. Routed at `KT_AGAINST=1.5`.
- **MMU / data card:** routed (D80: a row per register), **0
  unconnected, 0 violations**. Routed at `KT_AGAINST=1`, 2 h tries
  (`FRTIME=7200`).
- **Disk card:** done (D81, D82): **0 unconnected, 0 DRC violations**,
  matching its schematic (its seventh try at the D81 layout).
- **Video card:** revision 2 (the ACRTC card) done, D108: **0 unconnected**,
  its 1 DRC warning the LED footprint's (D90); matches its schematic. Its
  kept part is placed for the nets its thin margins hang on, and **a board
  routed again must be measured again**: `build_board.sh` rewrites
  `video-card-tracks.json` and `make test` runs the timing with it
  (docs/video-card-acrtc.md §4.9). (Revision 0's board, D81, D82, is in the
  history.)
- **Debug card:** done (D81, D82): **0 unconnected, 0 DRC violations**,
  matching its schematic. Its rows put the bus side left (sample latches,
  drive registers) and the capture side right; the first layout, two
  whole slices a row, stalled at 94.
- **CPU card:** done (D83): **0 unconnected, 0 DRC violations**, matching
  its schematic, on 6 layers with the general packer (its 195 chips at the
  tightest spacing), routed by Freerouting with its fixes for issues #1017
  and #1054 built in. Rows by function and 8 layers had routed worse (D82).
- **Freerouting:** back to a release once one has the fixes for its issues
  #1017 and #1054 (pull requests #1021, #1055; D83); until then
  `generators/build_freerouting.sh` builds the one P32W uses.
- Route with `PARITY=` while a card has no generated schematic;
  `FRTIME=7200` gives a try 2 hours; one router at a time.
- **The cards' labels upright with the card standing** (Ken's rule of
  2026-11-18, every board). The debug card and the memory card are done
  (D150, `generators/p32silk.py`), as the DIN 96 tester (D137). The CPU,
  MMU / data, I/O, disk and video cards are still drawn to be read lying
  (0 and 90): before each one's order, set `card.upright` and run
  `p32kicad_pcb.py --label`, silk only. First `p32silk.py` needs upright
  forms for what they have and these two do not: DIP switches (levers too
  close for a name at each: Ken's to say), cable connectors, turned chips'
  notes. **Until then pcb-check fails those five** (Ken, 2026-11-23: "make
  sure I don't order the other cards until issue is addressed"): each has
  `hold =` in its `pcb-check.ini`, taken out in the commit that turns its
  labels. The backplane lies flat and is not a card the rule turns.

### Found by the theory documents: to settle before fabrication (2026-10-30)
Writing the theory of operation of the I/O, disk, video, debug cards and
the backplane (`docs/*-theory.md`), against the netlists and the gate-level
simulations, turned these up; each document's last section has the
detail. Ken's decisions; nothing is changed yet.

1. **The arbiter grants in the middle of a stretched cycle** (GAL ARB on
   the MMU / data card grants at any `CLK` edge with `-LOCK` high). A CPU
   cycle held by `-WAIT` is taken off the bus and run again, so a disk
   port's register sees two strobes: at gate level a PIO read during a DMA
   transfer got 7 extra strobes; a write would be done twice. Both
   emulators pass the same program. bus.md says a grant comes at a cycle
   boundary. (disk-card-theory §10, backplane-theory §11)
2. **The video card's `+F4` word read: bits 16-31 float** (no driver and,
   unlike XD0-15, no pull-downs). Two more 10k networks would fix it.
   (video-card-theory §14) **Settled with revision 2's netlist, D106
   (2026-11-12):** that was revision 0; revision 2 drives zeros onto
   D16-D31 for that read (two 74HCT541s), and its gate-level check sees
   the whole word.
3. **The debug card's capture SRAMs: a write race**, and its GO / ARM / STEP
   flip-flops not cleared at power-on: **settled, D101 (2026-11-11)**: the
   write is in the sample clock's high half, and CRESN clears them.
   (debug-card-theory §8, §5)
4. **GAL IOSTB (I/O card): a possible glitch on `-RD` / `-WR`** between
   the two clocks of a byte access (a term `BCLK & !W1` in both). Nothing
   checks hazards. (io-card-theory §17)
5. **The disk card's internal bus CD0-15 has no pull resistors**, and
   IORDY reaches the sequencer GAL's registers unsynchronised. (disk-card-theory §10)
6. **The emulators against the hardware:** a TRELOAD write at the reload
   edge (the gate level reloads the new value, p32io.h the old one); the
   clock's interrupt (status bit 5, vector +5) is not modelled; a DMA bus
   error in DIR 0; BUSY in the PS/2 status. (io-, disk-, video-card-theory)
7. **The backplane:** with no card in slot 2 nothing drives the grants or
   holds the requests (their drivers and pull-ups are on the MMU / data
   card); `XCLK` is not terminated. bus.md's signal table says "7 highest"
   for `-IRQn`, against everything else (0 first). (backplane-theory §11)
8. **The I/O card's DB9s stand 2.84 mm further out than KiCad's footprint
   intends.** card_io.py puts pin 1 7.70 mm from the edge ("the shell
   through the panel"); the footprint's 7.7 mm "pin-PCB-offset" is the
   inner row (pins 6-9): its mounting holes, 9.12 mm from the edge, put
   pin 1's row at 10.54 mm, the flange at the board's edge. If the flange
   should sit at the edge, set 10.54 and route the I/O card again. (The
   video card's DE-15 had the same mix-up worse, its outer row off the
   board; fixed, D81.)
9. **The debug card's documents have drifted from its netlist**, and DRIVE
   cannot drive `-BGNT3-10`, `-FAST` or `-IRQ0-7`: **settled, D101**: the
   documents follow the netlist, the window-4 registers and the in-place
   ROM writing are dropped, and DRIVE reaches those lines. **D102**: the
   card alone no longer takes the host's word that the CPU and MMU / data
   cards are out; each ties a line to ground (`-CPUIN`, `-MMUIN`) that
   turns the drivers for its lines off. The Pico's firmware and the host
   tool: written, D135 (item 2 of the next list). (debug-card-theory §15)

### Found by the review before building (2026-11-16, D128)

Ken's review of the backplane, the debug card and the memory card before
ordering them. Three things were changed (D128: 1k on the strobes, a plane
between the bus's layers on the backplane, 7406s for the memory and
debug cards' open-drain gates). Still open, Ken's decisions:

1. **The arbiter's grant in a waited cycle** (item 1 of the list above) is
   the one thing between the bus and DMA beside a running CPU that uses a
   slow device. D128's resistors make the hand-over safe for the memory;
   they do not stop a device's cycle from being cut and run again. Since
   D99 there is no slot 10: its pair's pins on GAL ARB could take `-WAIT`.
2. **The debug card's firmware and host tool: written (D135),** and run
   against the netlist in `make test`, with everything listed here as never
   simulated: the trigger RAMs, post-trigger counts, the buffer's wrap,
   the 12 MHz mode, the clock running alone, a GO alone on the bus,
   `-WAIT` in a DMA cycle, and the LR and J3 groups, playing the CPU card
   to an MMU / data card alone. Left: the first build with the Pico SDK
   (not on this Mac). Writing it found four changes, all made (D136,
   Ken: "do 1,2,3,4"): R3 4.7k for old RP2350s; R8 and R9 to see the
   card's 5 V; the machine's reset from the card (spare gates onto
   PWROK); a 10k network on the MMU / data card so J3 does not float with
   the CPU card out (item 4 here).
3. **What the debug card cannot test of a bare backplane** (D128; and since
   D129 the DIN 96 shorts tester reaches all 96 pins of a connector, on a
   backplane or a card: docs/din96-tester.md): the 25
   spare lines (no pin on the card); `XCLK` and `-XCLKEN` (driven, not
   read); its own grant, `FAULT`, `FC0–3` and `STALL` (read, but with the
   CPU and MMU / data cards out nothing holds them: they float, on six
   sample-latch inputs). A 10k network on those seven, the two clock lines
   on two of the nine spare sample inputs, and the spares on more slices
   would close it: new parts and a new route of the card.
4. **Done (D135), alone: with the CPU card out** the firmware's `ALONE ON`
   drives the system lines released and the local link idle, J3 first,
   then LR as 0, so that `-RES`, the requests and the local link do not
   float. With the MMU / data card in, it keeps J3 driven and never drives
   LR while J3 puts that card on R (all 128 RSRC and CRSEL settings tried
   against its netlist). A GO's strobe is let go low at the cycle's end,
   which D128's 1k brings up in 0.1 µs. **Not alone, done too (D136):**
   before `ALONE ON`, after `ALONE OFF`, at LISTEN or with no debug card
   in, the MMU / data card's RN3 (10k × 8 to ground) holds RSRC0–3, MEM0–2
   and CCLK low, so its decoders put nothing on R.
5. **Done (D135, D136): the Mac's USB cable in and the machine off:** the
   Pico runs from USB and its pins would drive the card's unpowered 5 V
   logic through the chips' input diodes. It drives CRESN low and nothing
   high until the card's 5 V reads above 4.0 V (R8 and R9 onto GP26, D136)
   and a probe of a few microseconds shows the card answering (D135); the
   5 V is read again before every command, and the bus let go if it fails;
   since D151 also every 10 ms while the card waits for one (`dbg_idle()`):
   below 4.0 V the card is let go of and what it held forgotten, and with
   the power back it stays clear until the next command.
6. **The other cards' 74HC05s** (MMU / data, I/O, disk, video) were not in
   this review. Where a GAL drives one (the MMU / data card's `-LOCK`) the
   level is as marginal as it was on the memory card: for their reviews.
7. **`-BREQ0`'s pull-up on the MMU / data card is 10k**, and the debug
   card's 7406 on that line may leak 0.25 mA when off (its datasheet's
   figure, at 30 V): 2.5 V lost at the worst. 2.2k there, when that card
   is reviewed.
8. **The hold after a write's end** (address and data until `-WE` has
   risen) cannot be checked from the datasheets (no minimum delays): about
   20 ns by typical figures. A scope on the first memory card; a 7.5 ns GAL
   for MWE if it is short.
9. **Ordering the backplane: settled.** 1.6 mm (D133), the board held flat
   by its 18 screw holes, four of them among the sockets; 1 oz copper on
   the inner layers, not JLCPCB's default 0.5 oz, since 30 A crosses the
   planes (D134). By hand, as before: a socket's turn with a real
   connector, the termination's values on the bench.
10. **Done (D131): the plugs on every P32W card were 1.46 mm too far from
   the edge** (D130, found by the tester's check): `p32kicad_pcb.py` put a
   card's J1–J3 "pads 4 mm in", row a 6.54 mm from the edge. KiCad's
   footprint marks the edge 5.30 mm from row a, its model's shroud comes
   down in front of the board from 5.59 mm, and YACC1's built cards have
   5.08. As drawn, a plug could not sit down on a card, and a card that did
   go in would have stood 1.5 mm nearer the backplane than the socket
   allows; the backplane was not touched. Ken (2026-11-17): "do all p8x and
   p32w move connector to match success on YACC1 memory card" (row a
   5.08 mm from the edge), and "be sure to add verify position of connector
   to pcb check ... for yacc1,p32w,p8x". Done: every card's plugs at
   5.08 mm, moved with the cards' size and tracks kept, and `pcb-check`'s
   PLUG EDGE rule passing on all seven (D131). By hand: a real plug held
   against a card, done (Ken, 2026-11-18, the bus connector against a 1:1
   print of the DIN 96 tester: "all good").

11. **Parts the big distributors no longer stock** (found 2026-11-19,
   pricing the ICs of the next order at Unicorn, Digi-Key and Mouser; D139,
   D140):
   - **The SST39SF0x0 flash in the 32-pin DIP**, in all three sizes, and
     **the debug card's 24 CY7C199-15PC**: obsolete at Digi-Key and Mouser.
     **Settled for this order (D140):** Ken found suppliers for the
     CY7C199-15PC in DIP-28 and the SST39SF020A-70 in DIP-32, so the debug
     card and the memory card stay as designed, in DIP sockets. D139's
     PLCC-32 sockets for the boot ROMs were taken out again.
   - **Open: the CPU card's nine SST39SF010A-55** (seven of control store,
     two decode tables) are the same 32-pin DIP, and that card is not on
     this order. Before it is ordered: a supplier for the DIP part, as for
     the 020A. The other way, PLCC-32 sockets, was started and stopped
     (D140): the sockets need a new layout, and it had not finished
     routing. Its branch, `cpu-plcc`, was deleted on 2026-11-20 at Ken's
     word; D140 has what the attempt found.
   - If the DIPs cannot be had after all: Alliance's AS7C256C-15PCN (5 V,
     15 ns, a 0.3 inch DIP-28) fits the debug card's sockets as they are,
     and D139 is the memory card's way to the PLCC part.
   - **The Alliance part, checked against its data sheet** (Rev 1.0, June
     2024; 2026-11-20): the CY7C199's pin functions and read times. Its
     write wants 12 ns of address and of chip enable before the write ends
     (the CY7C199: 10) and 8 ns of data (9); with those, `p32timing.py`
     passes every run with the CY7C199's margins (D149, the pass put
     right: the debug card's 12 MHz mode has 0.7 ns with either, set by a
     path no RAM is in, and at the RAMs themselves the part has 16.7 ns in
     hand, the CY7C199 15.7). A high input wants 2.4 V: the capture RAMs'
     `-OE` (a GAL: 2.4 V promised) meets that exactly, their addresses
     (74F161s: 2.5 V) by 0.1 V. So it is the fallback, the CY7C199-15PC
     the part to buy: 24 on the debug card (3 in Ken's stock), 39 in the
     machine (the CPU card 8, the MMU / data card 7). Mouser had 216 at
     $15.40 that day (by Findchips).
   - **The debug card's sockets take slower RAMs than 15 ns** (D149,
     2026-11-21, by the timing pass put right; the "only 15 ns" told to
     Ken the day before came of its fault): a 0.3 inch 32K x 8 RAM of 20
     or 25 ns passes the card's 12 MHz mode with 14.7 ns in hand, one of
     35 ns with 9.7 (its times assumed); 55 ns fails. In Ken's stock
     besides the three CY7C199: seven "55328P-20", if they are the 0.3
     inch package. The CPU and MMU / data cards' 15 stay 15 ns parts.
     Which RAMs to buy for the debug card: Ken's to say.
   - **A fast RAM that is not a DIP has no through-hole socket that can be
     bought** (looked up 2026-11-20): none of this size is made in a PLCC,
     and a 28-lead SOJ's sockets are surface-mount, or test sockets. Short
     of SOJ chips on DIP-plug adapters in the same sockets (Aries
     28-350002-10, about $20 each), the debug card's RAMs are DIP parts.
     Ken, of `main`'s card: "Leave it DIP".
   - **Branch `non-dip-memory`, off `main`, explores both cards with DIP
     parts that can be bought, new layouts allowed** (Ken, 2026-11-20:
     "doing a new PCB layout is fine"; "Yes, both cards"; "Lay out both").
     As worked out that day (its D142, committed there on 2026-11-21 as
     `9864bca`): the memory card's boot ROM as four 28-pin DIP EEPROMs or
     EPROMs with one wait state on a ROM read, and the debug card's 24
     RAMs as 55 ns parts in 0.6 inch sockets with its timing mode at
     6 MHz. Netlists, timing and
     gate-level simulation done, and both boards routed there, each with
     nothing open and `pcb-check --layers 6` with no failures. The memory
     card with PLCC-32 sockets (D139's board, the branch's D141) is that
     branch's commit `3d0c135`. Nothing of it is on `main` but the two
     faults its work found in the timing pass, put right here (D149).

### Found by the power-up tests (2026-11-25, D153): for the held cards, nothing changed

- **CPU card, GAL CLKGEN: WPH and WPHN are two registers with no reset.** Both
  at 0, with the microword latch's WEL_N at 1, the register file's eight
  CY7C199 and its eight 74ACT574 write latches drive the same 64 lines. The
  first oscillator edge ends it; with the CLOCK switch at STEP at power-on
  only a CLOCK STEP press does. All registers at 0 is a 22V10's own power-up
  (Atmel / Microchip's and Lattice's sheets). Tried in a scratch copy, not
  made: `reset_high` for WPHN, CLKN and WSTBN, **with the GAL's pins held
  where they are** (left to itself the generator moves five outputs to other
  pins): the netlist the same, no fight at all registers 0, hello's and
  iocard_test's traces the same. Ken's decision. Made on the branch
  `clkgen-fix` (2026-11-27), not merged: only `CLKGEN.jed` changes (150
  fuses, pins 21, 17 and 15 and their polarity), the 88 GALs checked
  through their fuse maps, the timing the same; `p32sim` starts CLKGEN as
  the chip powers up (every register 0) in place of one press.
- **CPU card: one register written at power-up, before reset ends**
  (2026-11-26). U48, the late-write latch (a 74HCT574, no clear), wakes
  with any address and WEL_N; at WEL_N 0 the first -WSTB writes that
  register in both copies, each from its own write latch: two random
  values (seeds 4 and 6: r5 and r9). Harmless: a real CY7C199 wakes random
  anyway, and the reset microcode sets r0 (D4). Only the simulators' check
  that the copies agree tripped over it; on `clkgen-fix` they forget what
  the register file was given before the first commit out of reset
  (`sim_cpu.out_of_reset`). No board change.
- **Video card:** BUSY (a 74HCT74) is cleared only by the ATmega reading the
  command latch: it can wake at 1, and a command of any value is then read.
  `sim_video.py` and `vidcard_test` fail at such starts. Not followed further.
- **Disk and video cards:** at one start (seed 2, every card's registers at
  random) both drove `D` at the first settle. Not followed further.
- **The emulator and a timer:** TRELOAD never written (0), the timer stopped
  and acknowledged by one write to TCTRL: p32uemu -C takes a second
  interrupt, the gates do not.

### The DIN 96 shorts tester (D129, 2026-11-16): to do

Designed, laid out and checked in full (D130, 2026-11-17), its labels turned
upright for a board standing on its plug (D137, D138, 2026-11-18, the copper
as it was); not ordered, and nothing has run on a real one.
- On the first one built: that it works; the 5 ms wait on the biggest bare
  board; which way the TYPE switch's ON is; pin a1 of a real plug and a
  real socket against the footprints; foil across the plug's pins, and all
  96 named (the tester cannot see a break in its own wiring).
- A self-test for that last one, if wanted: a key on the serial port that
  expects all 96 joined and names the pins that are not.
- Pull-ups on the select lines: asked about and left off (D130), then
  designed in as RN2 (D132). Fit it or leave it out on the first one built:
  the sketch is the same either way.
- YACC1-D (and any other system on that connector) can point to
  docs/din96-tester.md; its pin table is `hardware/din96-tester/pins/yacc1.txt`,
  a copy of what its `tools/bus_pins.py` printed on 2026-11-16.
- Not asked for: a break in a line (a second unit, or a plug, at the far
  slot); the three P32W connectors at once.

### A master pre-PCB-order check, for every system (Ken, 2026-11-05)

One command and one report for P32W, P8X, YACC1-D, System-5 and any later
system, in a **new repo** (it makes "all 7" into "all 8": its name and
visibility still to settle, then update `~/.claude/CLAUDE.md` and its master
copy in `private-resources/claude`). To start after the clock counter and the
kernel Makefile. FAIL blocks an order, WARN needs Ken's eye; a small settings
file per board (name, rev, layers, exemptions); it ends with the by-hand steps
(the layer pictures, the 3D render, the fab's viewer, the order form). It
only reports: it never changes a board, so built and ordered cards are safe.

It gathers what is scattered today: `elf/tools/preorder_check.py` (10 checks,
YACC1-D already calls it), `check_order_zip.py`, `tools/kicad/plane_check.py`
and `compare_netlists.py`; `YACC1-D/tools/design_review.py`;
`p8x/generators/check_card.sh`.

**Ken's checks:**
- every layer is in the gerbers;
- the gerbers match the schematic's netlist;
- no VCC-GND shorts;
- a **big warning** if VCC and +5V both exist and are not joined;
- every part's reference and value on the silk, and as far as possible both
  visible with the parts fitted;
- the card's name and rev level, large enough to read;
- `cottageworker.com`, and FAIL on any `onepagecomputer.com`;
- a warning if there is no POWER LED.

**And, all accepted:**
1. A copper-level proof: connectivity rebuilt from the gerbers and drill file
   alone, against the schematic. Today the zip check reads only the net names
   KiCad writes into the gerbers, not the copper. This closes the netlist and
   shorts checks above. The biggest piece (numpy is not installed: plain
   Python or C).
2. The rest of YACC1's review: unpowered parts, floating inputs, two drivers
   on a net, open-collector with no pull-up, too few bypass caps.
3. One-pin nets (how `-IRQ` against `IRQ` was caught on P8X).
4. Footprint against the real part: substitutions listed and compared with a
   table of known parts (P8X's Pico was a 2x20 header, its CR2032 holder the
   wrong one, and both passed everything).
5. The bus connector's pinout against that system's bus definition.
6. Inner layer order on 4- and 6-layer boards, and a tidy zip (P32W's zips
   also carry adhesive, paste, courtyard and fab layers).
7. Polarity: capacitors and diodes the right way round, `+` and pin-1 marks
   visible with the parts fitted.
8. Mechanical fit: board size, connector position, mounting holes, part
   height against the slot pitch.
9. Fab limits: track, gap, drill, annular ring, silk size; a closed outline;
   plated and non-plated drill files.
10. Power track width: no 0.2 mm track carrying a card's supply.
11. Soldering: a warning when plane pins have no thermal reliefs. **P32W's
    generators connect every plane pin solidly** (`ZONE_CONNECTION_FULL`),
    which will be hard to hand-solder on 6 layers: look at it before any P32W
    order.
12. A clean committed tree, the zip made from that commit, and the rev the same
    on the silk, the schematic, the README and the zip's name.
13. The system's own tests pass (P32W's `make test`, timing and GAL files;
    P8X's gate simulation).
14. The BOM complete, sockets included.
15. The order recorded in `FABRICATED.md` with the commit hash, and a warning
    if an ordered board goes again with no rev change.
16. Ken's layout rules as warnings: the front edge, connectors facing out,
    jumpers at an edge.

**The order archive (Ken):** when a board is set for order, what was sent to
the fab is stored in that board's revision directory, with the matching
schematic PDF (the good ones, chips connected), the layout PDF and the PNGs.

### The card netlists: done
Every card's netlist is designed, from the cards as specified
([architecture.md](architecture.md), D50) and the generated bus (docs/bus.md,
`generators/p32bus.py`): the CPU, MMU / data, memory, disk, I/O, video and
debug cards and the backplane (D68), each from its `generators/card_*.py`,
and the DIN 96 tester's (D129). Every board is routed (§ "The board
layouts" above). Each card follows the board rules (D19, D21, D47, D48):
every part through-hole, bypass caps on 0.1" footprints, switches, LEDs and
cable connectors at the front edge.

### Gate-level simulation of the cards
Done, in `make test`: the CPU card (D61, `generators/sim_cpu.py`); the CPU
and MMU / data cards together over the local link (J3, D62), with the
memory, I/O and disk cards on the bus (`generators/sim_sys.py`); the video
card (`sim_video.py`, `sim_video_timing.py`); the debug card
(`sim_debug.py`) and its firmware (`sim_dbgfw.py`, D135, with the power
cycled while it waits, D151); the cards and `bus_test` on the Backplane's
own netlist, read from its board file, in all seven orders of the cards
(`sim_backplane.py static` and `turns`, D152; its 106 planted faults by
hand); and the debug card from power-up with its registers and RAMs at 1
and at random, alone and on the Backplane (D153). Left: a 5 MHz CPU card
(§ "5 MHz without a wait state").

### A jumper for the clock source: oscillator or an external clock
The CPU card's 15 MHz oscillator drives the clock GAL's clock input (D17).
Add a jumper choosing between that oscillator and an **external clock
brought in on a pin** (a header near the card edge, D48), labelled on the
silkscreen with each position (INT OSC / EXT CLK). Uses: a slower or faster
crystal for bring-up, a signal generator, or a clock from the control
panel (below). To decide with the CPU card: the input's buffering (a
74HCT14 Schmitt input, with a pull so an open pin cannot float), whether
the external pin is on the front edge or a header, and how it relates to
the debug card's fast clock (that one drives the bus `CLK`, after the GAL;
this one feeds the GAL).

### A probe header on the CPU card for the debug card
The debug card's logic analyzer reaches only what is on the backplane. A
**probe header on the CPU card's front edge** (D48) lets it see signals
inside the card: the microword, the sequencer's state, the internal data
bus. To decide with the netlist: which signals, the header's size, and how
the cable runs to the debug card.

### 5 MHz without a wait state
The memory microcycle (MAR → TLB → physical address → memory → R, about
200 ns) does not fit a 200 ns clock with the control store's share. At
5 MHz the MMU / data card asks for **one wait state on every access**
(`-WAIT` on itself, no microcode change); 3 MHz needs none. Measured with
`p32uemu -W N` on the three-bus datapath (P32W Unix booted, root logged in,
`halt`; 7,049,876 instructions with the address adder and the D59
instructions; 47% of all microcycles are bus cycles):

| Clock, waits | Cycles | Cycles / instruction | Speed |
|---|---|---|---|
| 3 MHz, 0 | 22,427,463 | 3.18 | 0.94 MIPS |
| 5 MHz, 1 on every bus cycle | 32,868,985 | 4.66 | **1.07 MIPS** |
| 5 MHz, 0 (translation earlier) | 22,427,463 | 3.18 | 1.57 MIPS (+47%) |

**Moving the address translation earlier**, into the microcycle that loads
MAR, removes the wait state and is worth another 47%. It changes the MMU /
data card and the microcode; to design after the wait-state version works.

### The ALU's division at 5 MHz
The ALU core's worst latency is 18 of its 108 MHz clocks (167 ns,
`fpga/alu/tb_p32alu.v`), set by division; every other function is a few
clocks. At 3 MHz the result is on R in time; at 5 MHz (200 ns, less the
operand and result transfers) a division needs its result cycle held a
clock (`busy` into the sequencer, as `STALL`), or the divider made faster.

### The VGA timing parts (done, D67)
Counters (74F163) and fast GALs on the 25.175 MHz dot clock; the card's RAM
16 bits wide, shared in four slots of each character time (text, the bus's
two halfwords, the overlay). docs/video-card.md.

### The power budget
Done (D69, docs/power-budget.md, generated by `generators/p32power.py`):
about 14 A typical, 26 A at most for the whole machine on +5 V, plus the
drives. Left: measure the Tang Nano 20K and the Picos, and replace the
estimated rows with datasheet figures as they are checked.

### An external control panel
The machine's controls are on the CPU card's front edge (D50): the RUN /
instruction-step switch, the RESET and STEP buttons, and the RUN and HALT
LEDs. A separate control panel, cabled to the card, would have room for
more: an address and data display, a clock-speed selector, and the
front-panel ports' switches and lights (the I/O card's ports at `FF20` /
`FF21`). To decide then: the cable (a header on the CPU card in place of
the parts), what the panel shows, and whether the debug card's probes and
fast clock join it.

### For the MMU / data card: a bigger or 2-way D-TLB
A hardware question for Ken, not decided. The D-TLB (D12, D13: 64 entries,
direct-mapped) costs about 8% of the time of a compile (see "Memory-system
statistics" below). Doubling it to 128 entries (one more index bit on the
D-TLB's SRAMs) cuts its misses about elevenfold; making it 2-way (a second
bank, two comparators, an LRU bit per set, and TLBW choosing the way) about
eightfold, with the same 64 entries. The I-TLB can stay as it is. The
emulators, the microcode and the kernel's refill would change with it (a
decision of its own).

### A DRAM memory card (later; Ken: "later, for now stick to Static")
For density: 30-pin or 72-pin SIMMs (4 or 16 MiB a module, DIP-free but
through-hole sockets) or DIP DRAMs, with a refresh controller and the
RAS / CAS timing on the card, answering in the same 16 MiB windows (D52) and
stretching its cycles with `-WAIT` as needed. To weigh against D60's static
cards (simple, no refresh, 16 MiB a card) when memory runs short.

### Faster disk transfers: DMA from the disk card
The CPU moves every 16-bit halfword through the CF data register: about
140 KB/s, and the CPU does nothing else meanwhile. That bounds swapping
(below) and all file I/O.

- **Software first (with swapping):** the sector loop in hand-written
  assembly (about 2×, ~280 KB/s; docs/bench.md), interrupt-driven waits
  (the CPU runs another process while the card is busy), READ/WRITE
  MULTIPLE. No hardware.
- **Then DMA: the engine is on the disk card (D75, 2026-10-28)**, verified
  at gate level and modelled in `p32emu` / `p32uemu`; per sector, the
  driver starts it once the drive has the data (docs/disk-card.md §4.1).
  To do: the driver (`kernel/`), using it for whole sectors and the
  interrupt instead of the copy loop; measure swap and file-I/O times.
  Later, if wanted: the drives' own multiword DMA, by reprogramming the
  card's GALs.

### Analysis: component speeds for a faster clock
**Done as an analysis: docs/speed.md** (2026-10-27): the clock jumper, the
SRAM control store, 5 MHz at worst case, DMA, the overlapped fetch.
The clock jumper is built into the design (D74, 2026-10-28): 4 MHz at
typical delays.

**The tool exists now** (D71, docs/timing.md, `generators/p32timing.py`):
it gives each path's delay and the fastest clock the machine allows, worst
case and typical; trying faster parts is a change to `p32delays.py` and a
rerun. First the 3 MHz failures it found (D71). (Analysis only, nothing to change yet.) The clock is 3 MHz, with 5 MHz the
design goal (D50; microarch.md §8). Find the critical paths card by card
and what each faster part would buy: 74HCT → 74AC / 74ACT / 74F / 74ABT
(through-hole availability, edge rates and ground bounce, drive into the
backplane), the microcode flash (55 ns, D18; or copied to fast SRAM at
reset), the register-file SRAM (15 → 10 / 12 ns), main memory SRAM and ROM
(55 ns → 10–25 ns SRAM, ROM shadowed in RAM), the TLB SRAM and tag
comparator, GAL speed grades (-25 / -15 / -10 / -7), the ALU FPGA's 5 V ↔
3.3 V buffers and latches, and the backplane itself (propagation, loading,
settling, and whether its long lines need termination). Structural
options: the earlier translation (above), wait states only for slow
devices, or a pipelined fetch. Result: a table of path → parts → delay →
achievable clock, and the cheapest steps to 6, 8 and 10 MHz.

### Idea (parked): a PDP-11/23 clone on this hardware
Could P32W's cards, or variants of them, run PDP-11/23 microcode and so the
11/23's operating systems (RT-11, RSX-11M, RSTS/E, Unix V6 / V7) and their
software? First analysis: [pdp11-analysis.md](pdp11-analysis.md): feasible
as a second microcode set with CPU card, MMU / data card and ALU FPGA
variants; the hard parts are the 64-byte MMU relocation and DMA disk
controllers. Emulator first (in `p32uemu`, checked against SIMH).

## Devices: what is left

### Networking
The network is a Pico 2 W with a W5500 behind a mailbox (D45,
docs/net-mailbox.md), doing TLS too. **Done:** the emulator model, kernel
sockets, `ifconfig`, `ping`, `telnet`, `fetch`, `telnetd`, `ftp`, `ntpdate`
(D45, D46). **Left:**

- **The coprocessor's firmware:** the mailbox's other side, then the W5500
  and TLS, then the bench.
- **httpd:** static files, and CGI (shell, awk or BASIC scripts writing
  HTML) for status pages (uptime, processes, `df`, the TLB statistics);
  then directory listings, downloads and uploads. **No HTTPS on P32W:** for
  access from outside, a proxy or VPN on another machine, never P32W
  exposed directly.
- **`select` / `poll`**, to wait on several descriptors at once.
- **SLIP** over a UART (RFC 1055), as a second interface under the same
  stack, if a machine without the network ever needs one.

### Wi-Fi: joining a wireless network
The Pico 2 W's CYW43439 radio gives Wi-Fi beside the W5500's Ethernet. The
mailbox speaks sockets, not Ethernet, so Wi-Fi changes the coprocessor's
firmware, not P32W's kernel. To do:

- **The firmware's socket layer** over the Pico SDK's lwIP for the radio,
  the coprocessor using whichever link is up.
- **The protocol gains a few commands:** scan for networks, join one
  (network name and password), the link's state and signal. P32W gains
  `ifconfig wifi NAME` (or a `wifi` command), the password kept in a file
  only root reads (`/etc/wifi.conf`), and a join at boot from `/etc/rc`.
- **The emulator's model** pretends: a list of networks, any password
  accepted, the Mac's own network behind it.
- **Security:** WPA2/WPA3 is done on the radio; the password must not be
  readable by other users on P32W.

### USB drives
**Done** (D43, D44, docs/usb-design.md): the CH376S model in the emulators,
the driver (`/dev/usb`, `/dev/rusb`) and the FAT tools (`dosdir`, `doscp`,
`dosrm`, `dosmkdir`, `dosformat`). **Left:** checking a real module's
pinout and USB socket position against the I/O card's front edge, then
trying real sticks on the bench; later, FAT mounted directly, which needs
a VFS (a filesystem switch).

### Graphics for programs
The video card (revision 2, D103) draws 640 × 480 dots with an HD63484,
from commands; none of the picture is on the bus, so there is no memory to
map into a process. **The device is done, D110: `/dev/video` takes the
chip's commands** (root and group staff; one program at a time; the
console's text is kept and the screen given back: `video(4)`,
docs/kernel.md). **So is the C library, D111: `libgfx`** (`cc ... -lgfx`,
`gfx(3)`: dots, lines, boxes, circles, ellipses, arcs and text in white,
black or XOR, clipped by the chip; `/usr/examples/clock.c`). **And
BASIC's statements over it, D112** (SCREEN, PSET, PRESET, LINE, CIRCLE,
COLOR, LOCATE, CLS, POINT; `basic(1)`; `/usr/examples/plot.bas`). Still to
do, on top of them:

- **the driver's speed**: a command word costs the CPU 137 microseconds
  on its way to the chip, 7,300 words a second, far below what the chip
  takes for a short command: 48 in `libgfx` and 89 in the kernel, 60 of
  those in the driver (measured below; D119's "8,000 words a second" was
  the library and the kernel together). It is not an interrupt a word: a
  frame of 412 words took 9 interrupts, and `vidintr` 0.3 ms of the
  kernel's 37. It is unoptimised C that handles every word three times:
  `vgwrite` copies it in (14 microseconds) and calls `vw` for it (19),
  and `pump` takes it out of the ring again for the chip (24). Reading
  the screen back, writing bits to it and `gfill` wait for that (D119:
  6.7 s to read the screen); so does every figure of many small
  commands, and any picture that moves. To do: the words copied into
  the ring a block at a time and out of it in one short loop. A fifth
  of the cost would be 20 ms of a 101 ms frame;
- **`libgfx`'s own cost a word**: `_gw` is a function called for every
  word (9 ms of that frame) and `gdraw` one for every line (7 ms). The
  word put into the buffer in line, or a call that takes many lines at
  once, would save about 10 ms;
- **a copy by whole words in the library**: `gstamp` and `gcopy` copy dot
  by dot (the chip's AGCPY), so a picture made out of sight (`gmake`,
  `gtarget`) takes the chip 470 ms to put down in a window of 640 by 384
  dots, where its CPY of whole words takes 30.5 ms. `flight.c` sends CPY
  itself, through `gwords`, from a page of its own in the spare memory.
  A kept picture that begins at a whole word and is put down at one
  could go that way behind the same call;
- **a second page shown, not copied**: the chip can start its picture
  anywhere in its memory (its registers 0xCC and 0xCE), but `/dev/video`
  passes the command FIFO and nothing else, so a program cannot draw in
  one page and show another. An `ioctl` for where the display starts
  would save the copy (30.5 ms of the chip's time a frame) and the tear
  it leaves. It matters once the CPU's side is quicker: the copy holds a
  moving picture to about 20 frames a second;
- **the mouse's rate**: a packet costs 1.26 ms of CPU (3,790 cycles:
  three interrupts, one a byte). A PS/2 mouse sends 100 a second while it
  moves: about 13% of the machine. The card's PS/2 section turns the
  mouse on and off and nothing more: a command for its sample rate (20
  to 40 a second would do for a pointer), or its packets handed over
  whole, one interrupt each;
- **a clock finer than the tick, for programs**: `times` and its 50 Hz
  are all there is, so a frame of 100 ms is timed as 4, 5 or 6 ticks,
  and what moves does so a little unevenly;
- **BASIC's PAINT, DRAW, GET and PUT, its line styles, VIEW and WINDOW**:
  the library now has what they need (D119: gfill, gstyle, gpattern,
  gfpoly, gkeep and gstamp, gread and gwrite);
- **the chip's own circles, ellipses, arcs and PAINT**: the library works
  its circles out itself and has the chip draw them as short runs (D111),
  1,224 words and 0.15 s for a radius of 100 where the chip's CRCL is two
  words. The
  emulators' model has none of these commands (such a command does nothing
  there and is named on the emulator's error output); once a real chip
  shows which dots it draws, the model can have them and the library a
  fast path behind the same calls;
- **the window system** (`wm`, D122: terminal windows; D123: programs'
  pictures in windows of their own; D124: `win`, a program in a window of
  its own; D125: the desk's programs `files`, `view` and `clock`, the
  Windows menu, Snapshot; D126: a terminal's window made bigger or
  smaller, its size the kernel's to tell; D127: a picture's too, where
  its program can draw it again). Next: telnetd asking a caller's
  terminal for its size and telnet telling it (NAWS), so that a login
  over the network fits its window as one under wm does; `gread` and
  kept pictures drawn into, for a program in a window; a pointer for a program that has the screen itself (`view` and
  `clock` there are worked by the keys; `files` needs wm); the keys from the card's own keyboard
  when wm is started on a serial line; wm started inside one of its own
  windows (it takes itself for a program in one);
- **a `video` group**: the node is root and group staff because the kernel
  knows one group a process, its own. With supplementary groups it can
  have a group to itself;
- **a screen grab of the console**: a program may open the device and
  read the console's picture as it is (the screen is not cleared on
  open): a command to save it as a PBM file. (Under wm the Desk menu's
  Snapshot does it, D125; `view` shows the file.)

**What a moving picture costs, measured (2026-11-20).** `flight.c` as it
was first committed (`2a804ad`; D142 has since given it a bigger world
and a panel of dials, which cost more), flying itself (`cycles flight -a
-t 100`, `cycles flight -t 300`) in `p32uemu -G` at 3 MHz: the count the
gate-level simulations match, with the chip's time from its model. Frames
a second, by the three ways it can change its picture:

| | lines and words a frame | copied from a spare page | cleared and drawn | line by line in XOR |
|---|---|---|---|---|
| coming in to land, everything in the window | 71, 412 | **9.7** | 10.0 | 5.6 |
| a take-off and climb | 48, 276 | **12.3** | 12.9 | 9.8 |
| the same two with the 4 MHz jumper (`-F`) | | 12.9, 14.3 | 13.2, 16.9 | 7.4, 13.0 |
| the same two with no lines on the ground (`-g 0`) | 62 and 41 lines | 12.2, 14.2 | | |

- **The CPU is the limit, not the chip.** The first row's frame is
  100.9 ms (from `-q` profiles, 150 frames less 50): the program's own
  arithmetic 39.2 (each corner turned to where the eye sees it and each
  line cut to the window, in whole numbers), `libgfx` 19.8, the kernel
  36.7 (the driver 24.8; traps, system calls and the clock 11.6; idle
  0.4), sending the lines and the panel 5.2. The chip's work for that
  frame is 49.6 ms and goes on alongside: the copy 30.5, the clear 10.7,
  the lines 8. At 4 MHz, on the take-off's lighter frames, the program
  already waits for the chip 11.5% of the time.
- **Flicker**, from the chip's commands logged with their times in a
  private copy of the emulator. Cleared and drawn: the window is not a
  whole picture for 36.7 ms of each frame (36%), 10.7 of them the clear.
  In XOR: old and new lines are mixed for 109 ms of 174 (63%), and dots
  are dark where lines cross (300 of the 9,700 lit). Copied from the
  spare page: it is never part drawn, and the copy's 30.5 ms is one tear
  down the window. The two that draw in white give the same dots.
- **What would make it quicker** (estimates, from 9.7): the driver's cost
  a word a fifth of what it is, 12.4; a compiler that optimises, the
  program and the library in half the time, 14 (`cc` has no optimiser:
  "Measure hand-written assembly against C", below); `libgfx` without a
  call a word, 11; all three, about 20, where the chip's copy is the
  limit. 5 MHz with a wait state on every bus cycle, 11; without, 16
  ("5 MHz without a wait state", above).
- **How far to trust it:** the CPU's part is the microcode's count. The
  chip's time is the manual's table, doubled while the display shares
  the memory: an estimate, but the chip would have to be twice as slow
  to become the limit. The model's chip takes a word at once when it is
  idle, where a real one may ask for its FIFO every eight words, 50
  times a frame and not 9: some 9 ms more. Give or take 15%, sooner
  lower than higher. In a window of wm the same program takes over
  three seconds a frame, as every call goes through wm: it is for the
  screen itself.

A second port that would put the picture on the bus as well, so that it
could be mapped into a process, was looked at and left off (D106;
docs/video-card-acrtc.md §6a).

### P8X's desk programs for wm (D144): to do
The finder is done (D144), the editor, `edit` (D145: P8X's `write`;
still to do in it: marking text with the mouse, and undoing), and
`paint` (D146; still to do in it: a shape picked up again, moved or
changed; a fill up to a border, which wants `gread` in a window), and
`sheet` (D147; still to do in it: columns of other widths, blocks of
cells copied), and `cube` and `house` with `g3d.h` (D148). All five are
done; what is left in them is above. Was:
4. **A library for 3D in lines**, then `cube` and `house`.

And for wm, when it is wanted: the mouse's other buttons told to a program
in a window (the finder's menu at the pointer), and a program's menus in
wm's bar. And for the library: the menu let down in a program's own
window, and the line at the foot that asks for a name, are the same code
in the finder, in edit, in paint and in sheet: one function of libgfx
for each.

### A windowing system on the video console
After the overlay: overlapping windows, a terminal window running a shell
(several sessions on one screen), mouse and keyboard focus, menus, and an
API for programs. P8X has a resident window manager and its design
(`docs/p8x-wm-design.md` in the P8X repository: chrome, painter's repaint,
launching programs, per-window state). On P32W, with protected processes,
it becomes a **server process** (`/etc/wm`) that clients talk to through
pipes or a device.

### getty discarding typed-ahead input (optional)
`getty` could discard input typed ahead on a terminal before it shows the
login prompt (a flush of the terminal's input queue), so that stray keys do
not reach `login`. Optional.

## Operating system and tools

### Swapping: whole processes to disk
When page frames run out, `fork`, `exec` or `sbrk` fails (or a growing
stack kills its process). V6/V7-style swapping writes an idle process's
pages (data, bss, heap, stack; text can be re-read from its program file
or stay while others share it) to a swap area on disk, frees its frames,
and reads it back when it can run. More processes than fit in RAM; no
process bigger than RAM.

- **Software only:** the MMU, the page tables and restartable faults are
  there. In the kernel: a swap area (a reserved region or partition), a
  map of each swapped-out process's swap blocks, and a swapper that picks
  a victim (the longest-sleeping) when frames run out and brings processes
  back.
- **How long a swap takes** (each way, at 3 MHz; only pages a process
  touched are written):

  | swapped | C loop (~140 KB/s) | assembly loop (~280 KB/s) |
  |---|---|---|
  | idle `sh`, ~150 KB | ~1 s | ~0.5 s |
  | a 1 MB program | ~7 s | ~3.5 s |
  | `cc` on `lvm.c`, ~3 MB | ~20 s | ~10 s |
  | a full 8 MiB process (D34) | ~55 s | ~28 s |

  With 16 MiB of RAM or more, everyday use fits without swapping; it is
  the safety net for heavy moments. The swapper should keep a process in
  for a good while once it is in, so two big jobs do not thrash.
- **Faster disk transfers first** (they speed up every file too): see
  above.
- `ps` / `top` should show swapped-out processes; `p32emu -m`'s statistics
  should count swaps.

### Demand paging (later, if needed)
Single pages go to disk and come back on a fault, so a process can be
bigger than RAM. Needs a replacement policy (the clock algorithm) and
"used" and "dirty" information, which the software TLB does not keep: the
kernel emulates them by mapping pages read-only or not at all and noting
the faults. A process much bigger than RAM thrashes. Only after swapping,
and only if a real need appears.

### A debugger: source level
`db` (machine level: breakpoints, stepping, finish, backtrace, registers,
memory, disassembly, symbols from the a.out) runs on `ptrace` (unix.md).
To do: **source level** (`p32cc -g` emitting a line table and the locals'
frame offsets / register variables, kept by the linker in the a.out;
`print var`, `break file:line`), **`core` dumps** on fatal signals for
post-mortem use, and a **`gdb`-style stub in `p32emu`** so the same
debugger runs on the host against the emulator. Best done with M7, whose
object format must leave room for the debug sections.

### Measure hand-written assembly against C
Inner loops are measured (docs/bench.md): C is 1.2–2.3× slower than
assembly, and `memcpy` and `memset` are in assembly. Still to do: the
trap / system-call path, whole commands (`wc`, `grep`, `sort`), a
boot-plus-login figure, and the `n--` test. Report the ratio per piece and
per whole program, and whether the gap is in the compiler (worth improving
it) or inherent (worth a few assembly routines in libc and the kernel).
The benchmarks live in `software/bench/`, so they can be rerun as the
compiler improves.

### MicroPython
MicroPython is `/usr/bin/python` (D35, D36, D39). Left:
- **`pow` correctly rounded** (about 93%: y·log x needs more than 62 bits,
  a two-part log).
- **`tgamma`, `lgamma`, `erf`, `erfc` in 62 bits:** musl's code rounds at
  every step and is a few ulp off (`tgamma(5.5)` 1.8 ulp). Rewritten on
  softfp's 62-bit core, as `sin` and `atan` are (D38), they would be
  correctly rounded about 99.5% of the time. Rarely used: later.
- **`select` and `asyncio`** need `poll()` in the kernel.
- **Startup takes 3.1 s** (D37); the rest is the CF loop itself (DMA).

### Lua
Lua 5.4 is `/usr/bin/lua` and `luac` (`software/lua/README.P32W`). Left:
- **Deep recursion is slow:** 4,000 levels (each making a small table and
  a string) take 36 s of P32W time. Find out where it goes (stack
  reallocation? the incremental GC?).
- **Startup takes 4.1 s.** Measure where the time goes.

### BASIC
`/bin/basic` (D27; basic(1)). Left, from basic(1)'s BUGS:
- more graphics (the statements are in, D112): PAINT, DRAW, GET and PUT,
  line styles, VIEW and WINDOW; they wait for the library (Graphics for
  programs, above);
- CHAIN / COMMON / AUTO / EDIT, and a DEF FN of more than one line.
  (PRINT USING is whole: D116; random-access files are in: D118.)

### vi
`software/cmd/vi.c` (unix.md). Later: line wrapping, named registers,
marks, `:g`, `:set`.

### The SQL database
`/bin/sql`, `sqlui` and `libsql` (sql(1), sqlui(1), sqldb(3)). Left:
- a decimal or floating-point type (BASIC uses doubles, D27; SQL has none,
  and AVG is truncated to an integer);
- joins of more than two tables. (Not attempted: subqueries, UNION, views,
  ALTER, defaults, foreign keys.)

### Commands
- `dc`, and `grep -E` (grep has only -vnci).
- `ps -l` (a long listing: priority, nice, wait channel).

### Known bugs (small)
- **sh:** no `until`, `trap`, `$@`, PS1 or job control; words split at
  spaces only (no IFS); `"a b"$X` is split at the quoted space; past the
  word limit the rest stays one word, silently; `sh -c`'s `$0` is the
  command text; `done > file` cannot redirect a loop (`{ ...; } > file`
  can); `wait` takes no pid; `exec > file` does nothing. (sh(1)'s BUGS.)
- **Kernel:** `#!` is in libc's execve, not the kernel (no setuid
  scripts); `wait` on a stopped traced child marks the stop reported
  before the copyout, so an EFAULT loses it.
- **cc -c** records the name of its temporary assembly file as the
  object's source (`cc24_0.s`: the process number), where the host's
  `p32cc` records the file's own (`gfx.s`). So an object built on P32W is
  never byte for byte the host's, or the one built before, though its
  code, symbols and relocations are (seen with libgfx, D111).
- **du** counts blocks from `st_size` (there is no `st_blocks`), so a file
  with holes is overcounted.
- **init**'s back-off is a `sleep(60)` in its main loop, so for that
  minute it does not reap or respawn the other terminals.
- **The kermit test is intermittent:** about 1 run in 3, under load,
  `kermit -s` (P32W sending to the host) ends with "the line closed". It is
  probably timing in `tools/pykermit.py` or the test, not Kermit on P32W.

### Follow-ons
- **The native cc's memory:** all the tokens exist before parsing starts;
  running the preprocessor and the parser together (a token at a time), or
  smaller tree nodes, would cut it further, if ever needed.
- **Code generation:** constant folding of addresses, compare-and-branch
  on immediates, leaf functions without a frame.
- **Disk writes** (10 MiB takes 143 s of kernel time): the buffer cache's
  lookup is a linear search (a hash), and the CF transfer loop in
  assembly.
- **Interrupt-driven disk transfers** (the disk card's interrupt, level 1)
  instead of polling.
- **LBA48** in the CF driver (and the emulator's CF model) for cards past
  128 GiB.
- **Partitions** on a disk (several filesystems on one big card or drive).
- **Speed on the hardware:** measure a boot with login; hand-written
  transfer loops for the disk driver and `memcpy`.

### Memory-system statistics: what they show
`p32emu -m` and the kernel's counts (`time -l`, `top`'s `Paging:` line)
show that the D-TLB thrashes on big programs. Compiling `ls.c`, a sort and
the shell tests: 24.6 M data accesses, 203,373 D-TLB misses (0.82%, 97% of
them conflicts), about **8% of all instructions spent refilling**. The same
accesses miss 25,826 times in a 64-entry 2-way TLB, 18,648 in a 128-entry
direct-mapped one, 7,504 in 128 2-way; a hashed index (VPN ^ VPN >> 6) is
worse (235,220). The I-TLB is fine (0.01%). See the D-TLB item above.
