#!/usr/bin/env python3
"""p32ld -- the P32W linker (M1, D22).

    p32ld.py [options] file.o ... lib.a ... -o OUT

Joins relocatable objects (p32asm -c, p32cc) and pulls in archive members
(p32ar) that define symbols still undefined, lays out .text, .data and .bss,
resolves every relocation (p32obj.py lists the kinds) and writes:

  --format aout (default)   a P32W Unix executable: a 32-byte header, then
                            text, then data. Text is linked at 0x1000 (page 0
                            stays unmapped: NULL faults), data at the next
                            page boundary, bss after it (os-design.md 3.2).
  --format bin              a flat image for the boot ROM or a RAM load:
                            text at --text (default 0); data runs at --data
                            (default: straight after text) but is stored
                            after text in the image, so ROM programs copy it
                            to RAM at start-up (crt0 does, from _data_load).

Header of an a.out (little-endian words): magic 0x50333277 ("w2P3" in
memory: "P32w"), text size, data size, bss size, entry, flags (0), symbol
table size, 0. The symbol table (for debuggers; exec ignores it) follows the
data: every symbol of every object linked, global and local, and the
linker's own, sorted by value, then name, then type; each entry is the
value (word), a type letter (T D B A; lower case: local), the name's length
(byte) and the name, padded to a multiple of 4 bytes.

Symbols the linker defines: _text _etext _data _edata _data_load _bss _end
(all addresses; _end is the first free byte after bss, 4-aligned).

  -e SYM        entry symbol (default _start; for bin, the entry is not stored)
  -s            no symbol table (a smaller a.out)
  -M FILE       write a link map (sections, symbols by address)
"""
import os, sys, argparse

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import p32obj  # noqa: E402

MAGIC = 0x50333277
AOUT_TEXT = 0x1000
PAGE = 0x1000

class LinkError(Exception):
    pass

def align(x, n):
    return (x + n - 1) // n * n

def gather(paths):
    """The objects to link: every .o, and archive members as needed."""
    objs, archives = [], []
    for p in paths:
        o = p32obj.read(p)
        if o["format"] == "p32ar":
            archives.append((p, o))
        else:
            objs.append((p, o))
    def defined():
        return {n for _, o in objs for n, s in o["symbols"].items() if s["global"]}
    def undefined():
        d = defined()
        need = set()
        for _, o in objs:
            for r in o["relocs"]:
                if r["symbol"] not in p32obj.SECTIONS and r["symbol"] not in d:
                    need.add(r["symbol"])
        return need
    while True:
        need, added = undefined(), False
        for ap, a in archives:
            for mname, m in sorted(a["members"].items()):
                key = "%s(%s)" % (ap, mname)
                if any(k == key for k, _ in objs):
                    continue
                if any(n in need and s["global"] for n, s in m["symbols"].items()):
                    objs.append((key, m))
                    added = True
                    need = undefined()
        if not added:
            return objs

def link(paths, fmt="aout", text_at=None, data_at=None, entry="_start"):
    objs = gather(paths)
    if not objs:
        raise LinkError("nothing to link")
    # ---- layout
    base = {}                                           # (object index, section) -> address
    def place(sect, start):
        a = start
        for i, (_, o) in enumerate(objs):
            s = o["sections"][sect]
            a = align(a, s.get("align", 4))
            base[(i, sect)] = a
            a += s["size"] if sect == ".bss" else len(s["data"]) // 2
        return a
    t0 = AOUT_TEXT if fmt == "aout" and text_at is None else (text_at or 0)
    t_end = place(".text", t0)
    if fmt == "aout":
        d0 = align(t_end, PAGE) if data_at is None else data_at
    else:
        d0 = align(t_end, 4) if data_at is None else data_at
    d_end = place(".data", d0)
    b0 = align(d_end, 4)
    b_end = place(".bss", b0)
    end = align(b_end, 4)
    data_load = align(t_end, 4) if fmt == "bin" else d0
    # ---- symbols
    syms = {"_text": t0, "_etext": t_end, "_data": d0, "_edata": d_end, "_data_load": data_load,
            "_bss": b0, "_end": end}
    table = [(v, n, "A") for n, v in syms.items()]       # (the a.out's symbol table)
    for i, (p, o) in enumerate(objs):
        for n, s in o["symbols"].items():
            t = {"*ABS*": "A", ".text": "T", ".data": "D", ".bss": "B"}[s["section"]]
            v = s["value"] if s["section"] == "*ABS*" else base[(i, s["section"])] + s["value"]
            table.append((v & 0xFFFFFFFF, n, t if s["global"] else t.lower()))
    where = {}
    for i, (p, o) in enumerate(objs):
        for n, s in o["symbols"].items():
            if not s["global"]:
                continue
            v = s["value"] if s["section"] == "*ABS*" else base[(i, s["section"])] + s["value"]
            if n in syms:
                raise LinkError("%s defined twice (%s and %s)" % (n, where.get(n, "the linker"), p))
            syms[n], where[n] = v, p
    # ---- section contents and relocations
    text = bytearray(t_end - t0)
    data = bytearray(d_end - d0)
    missing = {}
    for i, (p, o) in enumerate(objs):
        for sect, buf, start in ((".text", text, t0), (".data", data, d0)):
            raw = bytes.fromhex(o["sections"][sect]["data"])
            off = base[(i, sect)] - start
            buf[off:off + len(raw)] = raw
        for r in o["relocs"]:
            sect = r["section"]
            buf, start = (text, t0) if sect == ".text" else (data, d0)
            P = base[(i, sect)] + r["offset"]
            if r["symbol"] in p32obj.SECTIONS:
                S = base[(i, r["symbol"])]
            elif r["symbol"] in syms:
                S = syms[r["symbol"]]
            else:
                missing.setdefault(r["symbol"], set()).add(o.get("source", p))
                continue
            v = S + r["addend"]
            k = P - start
            w = int.from_bytes(buf[k:k + 4], "little")
            kind = r["kind"]
            if kind == "ABS32":
                w = v & 0xFFFFFFFF
            elif kind == "HI16":
                w = (w & 0xFFFF0000) | (v >> 16 & 0xFFFF)
            elif kind == "LO16":
                w = (w & 0xFFFF0000) | (v & 0xFFFF)
            elif kind == "J24":
                d = v - (P + 4)
                if d % 4 or not -(1 << 25) <= d < (1 << 25):
                    raise LinkError("%s: jump to %s out of range or unaligned" % (o.get("source", p), r["symbol"]))
                w = (w & 0xFF000000) | ((d // 4) & 0xFFFFFF)
            else:
                raise LinkError("unknown relocation kind %s" % kind)
            buf[k:k + 4] = w.to_bytes(4, "little")
    if missing:
        raise LinkError("undefined: " + ", ".join("%s (in %s)" % (n, ", ".join(sorted(s)))
                                                  for n, s in sorted(missing.items())))
    if fmt == "aout" and entry not in syms:
        raise LinkError("entry symbol %s is not defined" % entry)
    layout = dict(text=(t0, t_end), data=(d0, d_end), bss=(b0, b_end), end=end, data_load=data_load)
    table.sort(key=lambda e: (e[0], e[1].encode("latin-1"), e[2]))
    return dict(text=bytes(text), data=bytes(data), bss=b_end - b0, syms=syms, layout=layout, table=table,
                entry=syms.get(entry, t0), objs=[p for p, _ in objs], base=base, objlist=objs)

def symtab(table):
    out = bytearray()
    for v, n, t in table:
        b = n.encode("latin-1")[:255]
        e = v.to_bytes(4, "little") + t.encode() + bytes([len(b)]) + b
        out += e + bytes(-len(e) % 4)
    return bytes(out)

def output(res, fmt, strip=False):
    if fmt == "aout":
        st = b"" if strip else symtab(res["table"])
        hdr = [MAGIC, len(res["text"]), len(res["data"]), res["bss"], res["entry"], 0, len(st), 0]
        return b"".join(w.to_bytes(4, "little") for w in hdr) + res["text"] + res["data"] + st
    return res["text"].ljust(res["layout"]["data_load"] - res["layout"]["text"][0], b"\0") + res["data"]

def mapfile(res):
    L = []
    for s in ("text", "data", "bss"):
        a, b = res["layout"][s]
        L.append("%-5s %08X-%08X  %d bytes" % (s, a, b, b - a))
    L.append("data stored at %08X" % res["layout"]["data_load"])
    L.append("")
    for i, (p, o) in enumerate(res["objlist"]):
        L.append("%-40s text %08X data %08X bss %08X" % (p, res["base"][(i, ".text")],
                 res["base"][(i, ".data")], res["base"][(i, ".bss")]))
    L.append("")
    for n, v in sorted(res["syms"].items(), key=lambda kv: (kv[1], kv[0])):
        L.append("%08X  %s" % (v, n))
    return "\n".join(L) + "\n"

def main():
    ap = argparse.ArgumentParser(description="P32W linker")
    ap.add_argument("files", nargs="+")
    ap.add_argument("-o", "--out", default="a.out")
    ap.add_argument("--format", choices=("aout", "bin"), default="aout")
    ap.add_argument("--text", type=lambda s: int(s, 0))
    ap.add_argument("--data", type=lambda s: int(s, 0))
    ap.add_argument("-e", "--entry", default="_start")
    ap.add_argument("-M", "--map")
    ap.add_argument("-s", action="store_true", help="no symbol table")
    a = ap.parse_args()
    try:
        res = link(a.files, a.format, a.text, a.data, a.entry)
    except (LinkError, ValueError, OSError) as e:
        sys.exit("p32ld: %s" % e)
    with open(a.out, "wb") as fh:
        fh.write(output(res, a.format, a.s))
    if a.map:
        with open(a.map, "w") as fh:
            fh.write(mapfile(res))

if __name__ == "__main__":
    main()
