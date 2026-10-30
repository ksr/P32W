#!/bin/sh
# build_schematics.sh [card ...] -- every card's KiCad schematic, checked, and
# the PDFs: write hardware/<card>/kicad/<card>.kicad_sch (p32kicad_sch.py:
# a sheet a function, generators/p32sheets.py; a sheet's own nets wired,
# buses and backplane signals labelled), run KiCad's ERC and compare KiCad's
# netlist with the canonical one, plot hardware/<card>/kicad/<card>-schematic.pdf,
# and join them all into docs/pdf/p32w-schematics.pdf.
# Needs KiCad, node with elkjs (cd generators && npm ci) and pdfunite (poppler).
# GENERATORS ARE CANON.
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CLI=/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli
CARDS=${*:-"cpu-card mmu-card mem-card io-card disk-card video-card debug-card backplane"}
ok=1
for c in $CARDS; do
    d="$ROOT/hardware/$c/kicad"
    ( cd "$ROOT/generators" && python3 p32kicad_sch.py "$c" "$d" --check ) || ok=0
    "$CLI" sch export pdf -o "$d/$c-schematic.pdf" "$d/$c.kicad_sch" > /dev/null 2>&1 || ok=0
done
mkdir -p "$ROOT/docs/pdf"
set --
for c in cpu-card mmu-card mem-card io-card disk-card video-card debug-card backplane; do
    [ -f "$ROOT/hardware/$c/kicad/$c-schematic.pdf" ] && set -- "$@" "$ROOT/hardware/$c/kicad/$c-schematic.pdf"
done
pdfunite "$@" "$ROOT/docs/pdf/p32w-schematics.pdf" && echo "-> docs/pdf/p32w-schematics.pdf ($# cards)"
[ $ok = 1 ]
