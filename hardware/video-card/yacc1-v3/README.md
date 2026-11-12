# YACC1-D's video card V3: the netlist P32W's video card is checked against

`v30_netlist.py` here is a **copy, byte for byte**, of
`hardware/cards/video/kicad/v3.0/v30_netlist.py` in Ken's YACC1-D
repository (github.com/ksr/YACC1-D), as of its commit `db48aa5`
(2026-11-11). **Do not edit it.**

P32W's video card, revision 2 (docs/video-card-acrtc.md, D103), keeps 35
ICs of that card as they are: they are simulated gate for gate and timed
with their data-sheet delays there (`tests/video/v30_netsim.py`).
`generators/card_video.py` transcribes those parts and `kept_check()`
proves the transcription equal to this file: every kept part, every pin,
every net, but for the 11 pins of the seam that the generator lists with
their reasons. `make test` runs that check, and this file's own
(`python3 v30_netlist.py`).

The copy is here so that the check runs wherever P32W is checked out. If
YACC1-D's file changes, the check's line in `make test` says so (on a
machine that has both repositories); taking the change is then a decision,
since the kept part's margins belong to the parts wired this way.

YACC1-D's V3 is not being built for the YACC1 (D103).
