#!/usr/bin/env python3
"""A card's schematic sheets (the KiCad schematic, p32kicad_sch.py, and the
PDF, p32schem.py): its parts grouped by function, a sheet a function.

A card lists its functional groups, each a title and its parts; sheets()
lays each group out in rows and adds two sheets of its own: the backplane
connectors (J1-J3), and the power (the decoupling and bulk capacitors, the
POWER LED and its resistor). Every other part must be in exactly one group:
a part left out, or listed twice, is an error, so the schematic never
silently loses one.

    c.sheets = sheets(c, [("Bus interface", ["U1", "U2", ...]), ...])
"""

def is_power(card, ref):
    dev, f, v = card.parts[ref]
    return (f.startswith(("decoupling", "bypass", "bulk")) or f == "POWER"
            or f == "POWER LED resistor")

def sheets(card, groups):
    """[(title, rows of refs)] for the card's functional groups, then the
    connectors and the power. A group is a flat list of parts (one row, which
    the schematic flows on at the paper's width), or a list of rows (each
    starting a new line: a sub-function a line)."""
    seen, out = {}, []
    for title, refs in groups:
        rows = refs if refs and isinstance(refs[0], list) else [refs]
        for r in (r for row in rows for r in row):
            if r not in card.parts:
                raise SystemExit("%s: sheet %r: no part %s" % (card.name, title, r))
            if r in seen:
                raise SystemExit("%s: %s is on sheets %r and %r" % (card.name, r, seen[r], title))
            seen[r] = title
        out.append((title, rows))
    conns = [r for r, (d, f, v) in card.parts.items() if d in ("DIN96", "DIN96F") and r not in seen]
    power = [r for r in card.parts if r not in seen and r not in conns and is_power(card, r)]
    left = [r for r in card.parts if r not in seen and r not in conns and r not in power]
    if left:
        raise SystemExit("%s: parts on no sheet: %s" % (card.name, ", ".join(left)))
    if conns:
        out.append(("Backplane connectors", [conns]))
    if power:
        out.append(("Power: decoupling and bulk capacitors", [power]))
    return out
