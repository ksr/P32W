#!/bin/sh
# build_board.sh <card> -- build and route one P32W card's KiCad board (the P8X
# pipeline: generate board -> export DSN -> Freerouting -> import routing ->
# gerbers, drill, renders, placement -> DRC). Needs KiCad and a Freerouting jar
# (FRJAR, default ~/freerouting/freerouting.jar). GENERATORS ARE CANON.
card="$1"
PYK=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3
CLI=/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli
FRJAR="${FRJAR:-$HOME/freerouting/v2.5.0/freerouting-2.5.0.jar}"   # Freerouting 2.5 (needs Java 25:
FRJAVA="${FRJAVA:-$HOME/freerouting/jre25/Contents/Home/bin/java}"  # a private Temurin 25 JRE,
[ -x "$FRJAVA" ] || FRJAVA=$(/usr/libexec/java_home 2>/dev/null)/bin/java      # or the system's: 25 or later)
ROOT=$(cd "$(dirname "$0")/.." && pwd)
d="$ROOT/hardware/$card/kicad"; brd="$d/$card.kicad_pcb"
# Freerouting's result depends on the order of the DSN's elements, and KiCad
# writes them in a new order on every placement run (fresh UUIDs): a route
# that leaves connections open is not improved by routing again, only by
# placing again (P8X's lesson). So place-and-route up to ROUTE_TRIES (4)
# times until nothing is left unconnected.
if [ -z "$ROUTE_TRY" ] && [ "$card" != backplane ]; then
    for t in $(seq 1 "${ROUTE_TRIES:-4}"); do
        ROUTE_TRY=$t sh "$0" "$card"; st=$?
        if [ $st -eq 0 ] && grep -q "Found 0 unconnected" "$d/$card-drc.rpt" 2>/dev/null; then exit 0; fi
        [ "$t" -lt "${ROUTE_TRIES:-4}" ] && echo "-- $card: connections left unrouted (try $t); placing again"
    done
    exit 1
fi
quiet() { grep -viE 'debug|traits|fontconfig' || true; }
if [ "$card" = backplane ]; then GEN=p32backplane.py; PARITY="";   # (the backplane's own generator comes with its netlist)
 else GEN=p32kicad_pcb.py; PARITY=${PARITY---schematic-parity}; fi   # (PARITY= : a trial card with no schematic yet)
"$PYK" "$ROOT/generators/$GEN" $( [ "$card" = backplane ] || echo "$card" ) 2>&1 | quiet
[ -f "$brd" ] || exit 1
# placement gate: colliding parts short nets, and Freerouting then searches
# forever instead of failing -- check before routing
"$CLI" pcb drc --severity-error -o "$d/$card-place.rpt" "$brd" > /dev/null 2>&1
if grep -qE "^\[(shorting_items|courtyards_overlap|pth_inside_courtyard|holes_co_located)\]" "$d/$card-place.rpt"; then
    echo "PLACEMENT FAILED ($card) -- colliding parts, see $d/$card-place.rpt"; exit 3
fi
rm -f "$d/$card-place.rpt"
if [ "$card" = backplane ]; then   # routed by its generator: one straight channel per net
    "$PYK" "$ROOT/generators/kicad_tools.py" finish "$brd" 2>&1 | quiet
    "$CLI" pcb drc --schematic-parity --severity-error --severity-warning -o "$d/$card-drc.rpt" "$brd" 2>&1 | quiet
    grep -E "Found|unconnected" "$d/$card-drc.rpt" | head -5
    exit 0
fi
"$PYK" "$ROOT/generators/kicad_tools.py" export_dsn "$brd" 2>&1 | quiet
rm -f "$d/$card.ses"
# time limit (FRTIME s, default 40 min). Freerouting 2.5 stops by itself when
# passes stop improving and saves what it routed; the limit is the backstop.
# The watchdog keeps no pipe open (its sleep would hold the caller's output).
# caffeinate -i: on battery the Mac idle-sleeps and Freerouting freezes with it.
# Optimizer off: on a large card one 2.5 optimizer pass took 53 min and was
# rejected (made it worse); KiCad's DRC checks the board. Analytics off.
# No neck-down: it thinned tracks to 0.10 mm; every track stays 0.2 mm.
# strict_drc (D82): Freerouting 2.5 rejects a route with a clearance fault
# from pass 3 on without putting back what it ripped up for it (its issue
# #1054), unless strict_drc keeps the snapshot to restore.
# The limit reads the wall clock: sleep(1) does not count time the Mac slept.
( cd "$d" && { caffeinate -i "$FRJAVA" -jar "$FRJAR" -de "$card.dsn" -do "$card.ses" \
      --router.autorouter.max_passes=30 --router.optimizer.enabled=false \
      --router.automatic_neckdown=false --router.strict_drc=true \
      --usage_and_diagnostic_data.disable_analytics=true --gui.enabled=false > fr.log 2>&1 &
  fr=$!; end=$(( $(date +%s) + ${FRTIME:-2400} ))
  ( while [ "$(date +%s)" -lt "$end" ] && kill -0 $fr 2>/dev/null; do sleep 10; done; kill $fr ) > /dev/null 2>&1 &
  dog=$!; wait $fr; kill $dog 2>/dev/null; } )
grep -E "Auto-routing stage completed|Optimization stage completed|finished with state" "$d/fr.log" \
    | sed 's/.*INFO *\[[^]]*\] //' | cut -c1-160
if [ ! -f "$d/$card.ses" ]; then echo "ROUTE FAILED ($card) -- see $d/fr.log"; tail -3 "$d/fr.log"; exit 2; fi
"$PYK" "$ROOT/generators/kicad_tools.py" import_ses "$brd" "$d/$card.ses" 2>&1 | quiet
"$PYK" "$ROOT/generators/kicad_tools.py" finish "$brd" 2>&1 | quiet
"$CLI" pcb drc $PARITY --severity-error --severity-warning -o "$d/$card-drc.rpt" "$brd" 2>&1 | quiet
grep -E "Found|unconnected" "$d/$card-drc.rpt" | head -5
