#!/usr/bin/env python3
"""test_link.py -- the M1 toolchain's own tests (make test): relocatable
assembly, the four relocation kinds, archives, the a.out format and the
linker's errors. Each case assembles small sources with p32asm -c, links
them, and checks the result word by word.

    python3 assembler/test_link.py      (prints PASS / FAIL lines; exit 1 on failure)
"""
import os, sys, tempfile, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import p32asm, p32ld, p32obj  # noqa: E402

fails = []
def check(name, cond, detail=""):
    if not cond:
        fails.append("%s %s" % (name, detail))

def obj(src, name="t.s"):
    asm, lines, _ = p32asm.assemble(name, src, True)
    if asm.errors:
        raise AssertionError("\n".join(asm.errors))
    o = p32asm.obj_of(asm, name)
    if asm.errors:
        raise AssertionError("\n".join(asm.errors))
    return o

def errs(src):
    asm, lines, _ = p32asm.assemble("t.s", src, True)
    if not asm.errors:
        p32asm.obj_of(asm, "t.s")
    return asm.errors

def link(objs, **kw):
    d = tempfile.mkdtemp()
    paths = []
    for i, o in enumerate(objs):
        p = os.path.join(d, "o%d.%s" % (i, "a" if o["format"] == "p32ar" else "o"))
        p32obj.write(p, o)
        paths.append(p)
    return p32ld.link(paths, **kw)

def word(buf, off):
    return int.from_bytes(buf[off:off + 4], "little")

# ---- 1. cross-file call, data reference, all four relocation kinds
a = obj("""
        .text
        .global start
start:  call  far               ; J24 to another object
        la    r1, table         ; HI16 + LO16
        jmp   start
        .data
        .global table
table:  .word far, table+8      ; ABS32 (external, local + addend)
""", "a.s")
b = obj("""
        .text
        .global far
far:    ret
""", "b.s")
r = link([a, b], fmt="bin", text_at=0x1000, data_at=0x20000, entry="start")
far, table = r["syms"]["far"], r["syms"]["table"]
check("J24", word(r["text"], 0) == (0x61 << 24 | ((far - 0x1004) // 4 & 0xFFFFFF)), hex(word(r["text"], 0)))
check("HI16", word(r["text"], 4) & 0xFFFF == table >> 16)
check("LO16", word(r["text"], 8) & 0xFFFF == table & 0xFFFF)
check("J24 backwards", word(r["text"], 12) == (0x60 << 24 | ((0x1000 - 0x1010) // 4 & 0xFFFFFF)))
check("ABS32 external", word(r["data"], 0) == far)
check("ABS32 local+addend", word(r["data"], 4) == table + 8)
check("layout", r["layout"]["data"][0] == 0x20000 and far == 0x1010, str(r["layout"]))

# ---- 2. a.out: text at 0x1000, data on the next page, header fields
r = link([a, b], fmt="aout", entry="start")
img = p32ld.output(r, "aout")
check("a.out magic", word(img, 0) == p32ld.MAGIC)
check("a.out sizes", (word(img, 4), word(img, 8), word(img, 12)) == (len(r["text"]), 8, 0))
check("a.out entry", word(img, 16) == 0x1000)
check("a.out data page", r["layout"]["data"][0] == 0x2000)

# ---- 3. archives: only the members that are needed
lib = {"format": "p32ar", "version": 1, "members": {
    "far.o": b,
    "unused.o": obj("        .text\n        .global unused\nunused: ret\n", "u.s")}}
r = link([a, lib], fmt="bin", entry="start")
check("archive pulls far.o", "far" in r["syms"])
check("archive leaves unused.o", "unused" not in r["syms"])

# ---- 4. sections: .bss takes no space in the image; _end; local labels stay local
c = obj("""
        .text
        .global start
start:  la    r1, buf
.Lx:    beq   r0, r0, .Lx
        .bss
buf:    .space 100
""", "c.s")
r = link([c], fmt="bin", text_at=0, data_at=0x100000, entry="start")
check("bss size", r["bss"] == 100)
check("_end aligned", r["syms"]["_end"] == 0x100000 + 100)
check("local label not exported", ".Lx" not in c["symbols"])
check("bss not in image", len(p32ld.output(r, "bin")) == len(r["text"]))

# ---- 5. errors
try:
    link([a], fmt="bin", entry="start")
    check("undefined symbol is an error", False)
except p32ld.LinkError as e:
    check("undefined symbol named", "far" in str(e), str(e))
try:
    link([b, b], fmt="bin", entry="far")
    check("duplicate symbol is an error", False)
except p32ld.LinkError as e:
    check("duplicate named", "far" in str(e), str(e))
check("branch out of section refused", any("leaves the section" in e for e in errs("""
        .text
x:      beq   r0, r0, y
        .data
y:      .word 0
""")))
check("load with relocatable offset refused", any("LA the address" in e for e in errs("""
        .text
        lw    r1, v(r0)
        .data
v:      .word 0
""")))
check("hi() only in LUI", any("lo() in ORI" in e for e in errs("        .text\n        addi r1, r0, lo(v)\n        .data\nv: .word 0\n")))
check(".org refused", any(".org" in e for e in errs("        .org 0x100\n")))
check(".global undefined", any(".global" in e for e in errs("        .global nothing\n        .text\n")))

# ---- 6. the flat mode is unchanged: the same source assembles as before
asm, lines, img = p32asm.assemble("f.s", "start: li r1, 0x12345\n jmp start\n")
check("flat mode", not asm.errors and word(img, 8) == 0x60000000 | (-3 & 0xFFFFFF), str(asm.errors))

if fails:
    print("FAIL link tests: " + "; ".join(fails))
    sys.exit(1)
print("PASS link tests: J24 HI16 LO16 ABS32, a.out, archives, bss, %d error cases" % 7)
