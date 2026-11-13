#!/usr/bin/env python3
"""board_tracks.py -- how far each signal travels on a routed board: for every
net, the length of track from each pin that can drive it to every other pin.
Run under KiCad's bundled Python (it reads the board with pcbnew):

    PYK=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3
    "$PYK" generators/board_tracks.py <card>

Writes hardware/<card>/kicad/<card>-tracks.json:

    {"board":  the board file's SHA-256,
     "paths":  {net: {"REF.PIN" (a source): {"REF.PIN": [mm, of them mm on inner layers], ...}, ...}, ...},
     "length": {net: mm of track in all}}

A source is a pin that can put a level on the net: an output (the netlist's
types out, tri, bidir, oc: generators/p32net.py) or a connector's or a
jumper's pin (a signal comes in there). The path is the shortest way along
the net's own track segments, through its vias and its through-hole pads.
The supplies (planes) are left out.

    "$PYK" generators/board_tracks.py <card> --estimate <placed board.kicad_pcb> <out.json>

writes the same file for a board that is placed and NOT ROUTED: each net taken as
the rectilinear spanning tree of its pads (what the router's tracks come to,
within a tenth), every mm counted as on an inner layer, "board": "estimate". For
trying a placement in a minute instead of a routing's ten:
P32_PCB_OUT=<dir> p32kicad_pcb.py <card>, this, then
sim_video_timing.py --tracks <out.json> [--cost].

Why: the video card's kept part (docs/video-card-acrtc.md) has margins of a
few nanoseconds, and a track costs about 0.07 ns a centimetre.
generators/sim_video_timing.py runs the card's timing checks again with
these lengths as delays, and make test fails if the file is not this
board's (the SHA-256). generators/build_board.sh writes it after routing a
card that asks for it (card.track_check).
"""
import os, sys, json, math, heapq, hashlib
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "microcode"))
from p32net import CARDS, DEV  # noqa: E402

def card_of(name):
    if name not in CARDS:
        __import__("card_" + name.split("-")[0]).build()
    return CARDS[name]

NOT_A_SOURCE = ("R", "RNET9", "C100N", "C10U", "C22P", "LED", "TESTPOINT", "PTC", "XTAL")

def sources(card, pins):
    """the pins of a net a level can come from"""
    out = []
    for r, p in pins:
        dev = card.parts[r][0]
        t = DEV[dev]["pins"][p][1]
        if t in ("out", "tri", "bidir", "oc") or (t == "pas" and dev not in NOT_A_SOURCE):
            out.append((r, p))
    return out

def paths(board, card):
    """({net: {source (ref, pin): {(ref, pin): (mm, mm on inner layers)}}}, {net: mm of track})"""
    padnum = lambda ref, pin: DEV[card.parts[ref][0]]["pins"][pin][0]
    fps = {fp.GetReference(): fp for fp in board.GetFootprints()}
    outer = (pcbnew.F_Cu, pcbnew.B_Cu)
    segs, vias = {}, {}
    for t in board.GetTracks():
        n = t.GetNetname()
        if t.Type() == pcbnew.PCB_VIA_T:
            p = t.GetPosition()
            vias.setdefault(n, []).append((p.x, p.y))
        else:
            a, b = t.GetStart(), t.GetEnd()
            segs.setdefault(n, []).append(((a.x, a.y), (b.x, b.y), t.GetLayer()))
    out, total = {}, {}
    for net, pins in card.nets.items():
        if net in ("VCC", "GND") or net not in segs:
            continue
        adj = {}
        def link(u, v, w, inner):
            adj.setdefault(u, []).append((v, w, inner)); adj.setdefault(v, []).append((u, w, inner))
        S = segs[net]
        total[net] = round(pcbnew.ToMM(int(sum(math.hypot(a[0] - b[0], a[1] - b[1]) for a, b, layer in S))), 1)
        for a, b, layer in S:
            w = math.hypot(a[0] - b[0], a[1] - b[1])
            link((a, layer), (b, layer), w, 0.0 if layer in outer else w)
        ends = {(p, layer) for a, b, layer in S for p in (a, b)}
        for p, layer in ends:                           # an end that lands part-way along another segment of the net
            for a, b, l2 in S:
                if l2 != layer or p in (a, b):
                    continue
                dx, dy = b[0] - a[0], b[1] - a[1]
                L2 = dx * dx + dy * dy
                if not L2:
                    continue
                t = ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / L2
                if 0.0 < t < 1.0 and math.hypot(a[0] + t * dx - p[0], a[1] + t * dy - p[1]) < 2000:    # (2 um)
                    for q in (a, b):
                        w = math.hypot(p[0] - q[0], p[1] - q[1])
                        link((p, layer), (q, layer), w, 0.0 if layer in outer else w)
        for v in vias.get(net, []):                     # a via joins the layers at its place
            for p, layer in ends:
                if p == v:
                    link(("via", v), (p, layer), 0.0, 0.0)
        padnode = {}
        for ref, pin in pins:                           # a through-hole pad joins every layer under it
            fp = fps.get(ref)
            if fp is None:
                continue
            for pad in fp.Pads():
                if pad.GetNumber() != padnum(ref, pin) or pad.GetNetname() != net:
                    continue
                c = pad.GetPosition()
                node = ("pad", ref, pin)
                padnode[(ref, pin)] = node
                for p, layer in ends:
                    if pad.HitTest(pcbnew.VECTOR2I(int(p[0]), int(p[1]))):
                        w = math.hypot(p[0] - c.x, p[1] - c.y)
                        link(node, (p, layer), w, 0.0 if layer in outer else w)
        for src in sources(card, pins):
            if src not in padnode:
                continue
            dist, heap, n = {padnode[src]: (0.0, 0.0)}, [(0.0, 0, 0.0, padnode[src])], 0
            while heap:
                d, _, din, u = heapq.heappop(heap)
                if d > dist[u][0]:
                    continue
                for v, w, inner in adj.get(u, ()):
                    if v not in dist or d + w < dist[v][0]:
                        dist[v] = (d + w, din + inner)
                        n += 1
                        heapq.heappush(heap, (d + w, n, din + inner, v))
            got = {rp: tuple(round(pcbnew.ToMM(int(x)), 1) for x in dist[node])
                   for rp, node in padnode.items() if rp != src and node in dist}
            if got:
                out.setdefault(net, {})[src] = got
    return out, total

def estimate(board, card):
    """the same two tables from a placed board: each net its pads' rectilinear spanning tree, a path the way along it"""
    pos = {}
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            p = pad.GetPosition()
            pos[(fp.GetReference(), pad.GetNumber())] = (pcbnew.ToMM(p.x), pcbnew.ToMM(p.y))
    out, total = {}, {}
    for net, pins in card.nets.items():
        if net in ("VCC", "GND") or len(pins) < 2:
            continue
        at = {(r, p): pos[(r, DEV[card.parts[r][0]]["pins"][p][0])] for r, p in pins}
        d = lambda a, b: abs(at[a][0] - at[b][0]) + abs(at[a][1] - at[b][1])
        keys = list(at)
        todo, adj, tot = keys[1:], {k: [] for k in keys}, 0.0
        best = {k: (d(k, keys[0]), keys[0]) for k in todo}
        while todo:                                     # (Prim: the nearest pad not yet on the tree joins it)
            k = min(todo, key=lambda k: best[k][0])
            w, par = best[k]
            todo.remove(k); tot += w
            adj[k].append((par, w)); adj[par].append((k, w))
            for o in todo:
                if d(o, k) < best[o][0]:
                    best[o] = (d(o, k), k)
        total[net] = round(tot, 1)
        for src in sources(card, pins):
            dist, go = {src: 0.0}, [src]
            while go:
                u = go.pop()
                for v, w in adj[u]:
                    if v not in dist:
                        dist[v] = dist[u] + w; go.append(v)
            out.setdefault(net, {})[src] = {k: (round(v, 1), round(v, 1)) for k, v in dist.items() if k != src}
    return out, total

def write(out, sha, P, L):
    key = lambda rp: "%s.%s" % rp
    doc = {"board": sha,
           "paths": {n: {key(src): {key(rp): list(v) for rp, v in sorted(to.items())} for src, to in sorted(P[n].items())}
                     for n in sorted(P)},
           "length": {n: L[n] for n in sorted(L)}}
    with open(out, "w") as fh:
        json.dump(doc, fh, separators=(",", ":"), sort_keys=True)
        fh.write("\n")
    return doc

def main():
    name = sys.argv[1]
    card = card_of(name)
    if "--estimate" in sys.argv:
        brd, out = sys.argv[sys.argv.index("--estimate") + 1:][:2]
        P, L = estimate(pcbnew.LoadBoard(brd), card)
        write(out, "estimate", P, L)
        print("tracks, estimated from the placement -> %s: %d nets, %.0f mm of spanning tree" % (out, len(L), sum(L.values())))
        sys.stdout.flush()
        os._exit(0)
    if not getattr(card, "track_check", False) and "--force" not in sys.argv:
        return                                          # (a card that does not ask: nothing to do)
    brd = os.path.join(ROOT, "hardware", name, "kicad", name + ".kicad_pcb")
    board = pcbnew.LoadBoard(brd)
    P, L = paths(board, card)
    out = brd[:-len(".kicad_pcb")] + "-tracks.json"
    doc = write(out, hashlib.sha256(open(brd, "rb").read()).hexdigest(), P, L)
    longest = max(((v[0], n, src, rp) for n, m in doc["paths"].items() for src, to in m.items() for rp, v in to.items()),
                  default=(0, "", "", ""))
    print("tracks -> %s: %d nets, %d paths, %.0f mm of track; the longest path %.0f mm (%s, %s to %s)"
          % (os.path.relpath(out, ROOT), len(P), sum(len(to) for m in P.values() for to in m.values()), sum(L.values()),
             longest[0], longest[1], longest[2], longest[3]))
    sys.stdout.flush()
    os._exit(0)

if __name__ == "__main__":
    main()
