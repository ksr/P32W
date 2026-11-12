# YACC1-D's video card V3: the netlist and the timing check P32W's video card is held to

Two files of Ken's YACC1-D repository (github.com/ksr/YACC1-D), **copied
byte for byte, each under the path it has there. Do not edit them.**

| Here | In YACC1-D, as of its commit |
|---|---|
| `hardware/cards/video/kicad/v3.0/v30_netlist.py` | `db48aa5` (2026-11-11) |
| `tests/video/v30_netsim.py` | `12565a7` (2026-11-11) |

(The paths are kept because the second file finds the first by its own.)

P32W's video card, revision 2 (docs/video-card-acrtc.md, D103), keeps 35
ICs of that card as they are: they are simulated gate for gate and timed
with their data-sheet delays there (`tests/video/v30_netsim.py`).
`generators/card_video.py` transcribes those parts and `kept_check()`
proves the transcription equal to this file: every kept part, every pin,
every net, but for the 11 pins of the seam that the generator lists with
their reasons. `make test` runs that check, and the netlist's own
(`python3 hardware/cards/video/kicad/v3.0/v30_netlist.py`).

`generators/sim_video_timing.py` then runs `v30_netsim.py`'s two scripts,
with that file's models and checks unchanged, on P32W's netlist: the kept
part's timing, at eight corners (D107). It also borrows that file's engine
for P32W's own bus side. `v30_netsim.py` run by itself here stops at its
first step, which reads YACC1's microcode: that step is YACC1's bus side,
not kept.

The copies are here so that the checks run wherever P32W is checked out.
If YACC1-D's netlist changes, the check's line in `make test` says so (on
a machine that has both repositories); taking the change is then a
decision, since the kept part's margins belong to the parts wired this way.

YACC1-D's V3 is not being built for the YACC1 (D103).
