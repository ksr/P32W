"""p32obj.py -- the P32W relocatable object and archive formats (M1, D22;
binary since M7, D24).

The assembler (p32asm -c) writes objects, p32ar bundles them, p32ld links
them -- and so do the native as, ar and ld on P32W Unix, byte for byte the
same files (software/cmd/as.c, ld.c, ar.c). In memory (read / make) an
object is a dict:

  {"format": "p32obj", "source": "file.s",
   "sections": {".text": {"data": "<hex>", "align": 4},
                ".data": {"data": "<hex>", "align": 4},
                ".bss":  {"size": N, "align": 4}},
   "symbols":  {"name": {"section": ".text", "value": OFFSET, "global": true}, ...},
   "relocs":   [{"section": ".text", "offset": OFF, "kind": KIND,
                 "symbol": NAME, "addend": A}, ...]}

A relocation's symbol is a section name (".text", ".data", ".bss": this
object's own section) or a global / external symbol name. Kinds (S = the
symbol's final address, A = addend, P = the address of the word):
  ABS32  word = S + A
  HI16   low 16 bits of the word = (S + A) >> 16       (LUI)
  LO16   low 16 bits of the word = (S + A) & 0xFFFF     (ORI: zero-extends,
                                                         so no carry fix-up)
  J24    low 24 bits = ((S + A) - (P + 4)) / 4          (JMP / CALL)

Binary object (version 2; little-endian 32-bit words unless noted; every
part padded to a multiple of 4 with zeros):
   0 "P32O"   4 version (2)   8 text size  12 data size  16 bss size
  20 text align  24 data align  28 bss align  32 nsyms  36 nrelocs
  40 string table size  44 the source name (string table offset)
  48 text bytes, then data bytes
     symbols, nsyms x 12: name, value, section (byte: 0 absolute, 1 .text,
                          2 .data, 3 .bss), global (byte), 2 zero bytes
     relocs, nrelocs x 16: section (byte), kind (byte: 0 ABS32, 1 HI16,
                          2 LO16, 3 J24), 2 zero bytes, offset, symbol
                          (string table offset), addend (signed)
     string table: NUL-terminated names
  Canonical order, so two tools write the same bytes: symbols sorted by
  name (byte order); relocations as the assembler emitted them; the string
  table holds the source name, then the symbol names in that order, then
  each relocation's symbol on its first use.

Binary archive (version 2):
   0 "P32A"   4 version (2)   8 members  12 index entries  16 string table size
  20 the index, entries x 8: a global symbol (string table offset), its
     member's number -- sorted by symbol name, then member
     the string table: the member names in order, then the index names in
     order (each string once, on its first use)
     the members, in name order: name (string table offset), size, then the
     object (size bytes, padded to 4)

Version 1 (JSON, the M1 format) is still read.
"""
import json, struct

KINDS = ("ABS32", "HI16", "LO16", "J24")
SECTIONS = (".text", ".data", ".bss")
SECNO = {"*ABS*": 0, ".text": 1, ".data": 2, ".bss": 3}
SECNAME = {v: k for k, v in SECNO.items()}

def make(source, images, bss, align, symbols, relocs):
    secs = {}
    for s in (".text", ".data"):
        secs[s] = {"data": bytes(images.get(s, b"")).hex(), "align": align.get(s, 4)}
    secs[".bss"] = {"size": bss, "align": align.get(".bss", 4)}
    return {"format": "p32obj", "version": 2, "source": source, "sections": secs,
            "symbols": symbols,
            "relocs": [dict(section=s, offset=o, kind=k, symbol=n, addend=a) for s, o, k, n, a in relocs]}

def _pad(b):
    return bytes(b) + bytes(-len(b) % 4)

class _Strings:
    def __init__(self):
        self.buf, self.at = bytearray(), {}
    def add(self, s):
        if s not in self.at:
            self.at[s] = len(self.buf)
            self.buf += s.encode("latin-1") + b"\0"
        return self.at[s]

def encode_object(o):
    st = _Strings()
    src = st.add(o.get("source", ""))
    names = sorted(o["symbols"], key=lambda n: n.encode("latin-1"))
    for n in names:
        st.add(n)
    for r in o["relocs"]:
        st.add(r["symbol"])
    text = bytes.fromhex(o["sections"][".text"]["data"])
    data = bytes.fromhex(o["sections"][".data"]["data"])
    sec = o["sections"]
    out = bytearray(b"P32O")
    out += struct.pack("<11I", 2, len(text), len(data), sec[".bss"]["size"], sec[".text"].get("align", 4),
                       sec[".data"].get("align", 4), sec[".bss"].get("align", 4), len(names), len(o["relocs"]),
                       len(_pad(st.buf)), src)
    out += _pad(text) + _pad(data)
    for n in names:
        s = o["symbols"][n]
        out += struct.pack("<IIBBH", st.at[n], s["value"] & 0xFFFFFFFF, SECNO[s["section"]], int(bool(s["global"])), 0)
    for r in o["relocs"]:
        out += struct.pack("<BBHIIi", SECNO[r["section"]], KINDS.index(r["kind"]), 0, r["offset"],
                           st.at[r["symbol"]], r["addend"])
    out += _pad(st.buf)
    return bytes(out)

def _str(tab, off):
    return tab[off:tab.index(b"\0", off)].decode("latin-1")

def decode_object(b, where="object"):
    if b[:4] != b"P32O":
        raise ValueError("%s: not a P32W object" % where)
    (ver, tsz, dsz, bsz, tal, dal, bal, nsym, nrel, stsz, src) = struct.unpack_from("<11I", b, 4)
    if ver != 2:
        raise ValueError("%s: object version %d" % (where, ver))
    p = 48
    text = b[p:p + tsz]; p += tsz + (-tsz % 4)
    data = b[p:p + dsz]; p += dsz + (-dsz % 4)
    symp = p; p += 12 * nsym
    relp = p; p += 16 * nrel
    tab = b[p:p + stsz]
    syms = {}
    for i in range(nsym):
        name, value, sec, glob, _ = struct.unpack_from("<IIBBH", b, symp + 12 * i)
        syms[_str(tab, name)] = {"section": SECNAME[sec], "value": value, "global": bool(glob)}
    relocs = []
    for i in range(nrel):
        sec, kind, _, off, sym, add = struct.unpack_from("<BBHIIi", b, relp + 16 * i)
        relocs.append({"section": SECNAME[sec], "offset": off, "kind": KINDS[kind],
                       "symbol": _str(tab, sym), "addend": add})
    return {"format": "p32obj", "version": 2, "source": _str(tab, src),
            "sections": {".text": {"data": text.hex(), "align": tal}, ".data": {"data": data.hex(), "align": dal},
                         ".bss": {"size": bsz, "align": bal}},
            "symbols": syms, "relocs": relocs}

def encode_archive(members):
    """members: {name: object dict}"""
    names = sorted(members, key=lambda n: n.encode("latin-1"))
    index = sorted(((s, i) for i, n in enumerate(names) for s, v in members[n]["symbols"].items() if v["global"]),
                   key=lambda e: (e[0].encode("latin-1"), e[1]))
    st = _Strings()
    for n in names:
        st.add(n)
    for s, _ in index:
        st.add(s)
    out = bytearray(b"P32A")
    out += struct.pack("<4I", 2, len(names), len(index), len(_pad(st.buf)))
    for s, i in index:
        out += struct.pack("<II", st.at[s], i)
    out += _pad(st.buf)
    for n in names:
        ob = encode_object(members[n])
        out += struct.pack("<II", st.at[n], len(ob)) + ob
    return bytes(out)

def decode_archive(b, where="archive"):
    ver, nmem, nidx, stsz = struct.unpack_from("<4I", b, 4)
    if ver != 2:
        raise ValueError("%s: archive version %d" % (where, ver))
    p = 20 + 8 * nidx
    tab = b[p:p + stsz]; p += stsz
    members = {}
    for _ in range(nmem):
        name, size = struct.unpack_from("<II", b, p); p += 8
        members[_str(tab, name)] = decode_object(b[p:p + size], where)
        p += size + (-size % 4)
    return {"format": "p32ar", "version": 2, "members": members}

def write(path, obj):
    with open(path, "wb") as fh:
        fh.write(encode_archive(obj["members"]) if obj["format"] == "p32ar" else encode_object(obj))

def read(path):
    with open(path, "rb") as fh:
        b = fh.read()
    if b[:4] == b"P32O":
        return decode_object(b, path)
    if b[:4] == b"P32A":
        return decode_archive(b, path)
    try:
        obj = json.loads(b)
    except ValueError:
        raise ValueError("%s: not a P32W object or archive" % path)
    if obj.get("format") not in ("p32obj", "p32ar") or obj.get("version") != 1:
        raise ValueError("%s: not a P32W object or archive" % path)
    return obj

if __name__ == "__main__":
    import sys
    if len(sys.argv) != 3 or sys.argv[1] != "dump":
        sys.exit("usage: p32obj.py dump FILE      (an object or archive as JSON)")
    json.dump(read(sys.argv[2]), sys.stdout, indent=1, sort_keys=True)
    print()
