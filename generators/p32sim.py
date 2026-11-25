#!/usr/bin/env python3
"""A small netlist-level simulator for the P32W cards.

It evaluates a card AS WIRED -- every chip's pins looked up in the card's
netlist -- so a test through it checks the wiring, the polarities and the
GAL equations together, not just the behaviour the designer had in mind.

  * nets carry 0, 1 or None (floating). Each step settles the combinational
    logic by repeated evaluation; a net with two drivers that disagree is an
    error (a bus fight), a net nobody drives takes its pull-up / pull-down
    (R or RNET9 to VCC / GND) or floats.
  * sequential chips (74HCT163, 74HCT374/377, GAL registers) act on the
    rising edge of their clock net, sampling their inputs as they were
    before the edge.
  * GALs evaluate the very equations p32gal.py verified (registered outputs
    with their polarity fuse, 3-state outputs with their OE term).

Only the chips a test needs have models; asking for another raises.

Power-up (D153). By default every register starts at 0 and a floating input is
read as 0 or as inactive. Environment variables change that, so that any test
can be run so without being changed:
  P32SIM_START   0 (the default), 1, or a seed (2 and up): what every flip-flop,
                 counter and register (the GALs' too) holds when the run begins
                 (but the CPU card's clock GAL, clkgen_pressed() below, unless
                 P32SIM_CLKGEN is "asis")
  P32SIM_RAM     the same for the SRAMs' cells
  P32SIM_ONLY    START and RAM for the chips of these cards alone ("DBG." or "DBG. MEM.")
  P32SIM_STRICT  "report" or "error": a floating clear, preset, clock, enable,
                 select or direction; a floating input taken by a register at
                 its clock or written to an SRAM; a floating GAL input that
                 decides an output or a register's next state. Looked at where
                 everything has settled, once the first clock edge has come.
                 report: listed on standard error at the end; error: stops.
"""
import atexit, os, random, sys, zlib
from p32net import DEV
from p32gal import eval_sop

class SimError(Exception):
    pass

def twin(dev):
    """A faster family's part acts as its 74HCT twin (D72): 74ACT157 as 74HCT157."""
    import re
    from p32net import FASTER
    if dev in FASTER:
        return FASTER[dev]
    m = re.match(r"74(ACT|AC|F|LS|ALS)(\d+)$", dev)
    if m and "74HCT" + m.group(2) in TWINS:
        return "74HCT" + m.group(2)
    return dev

TWINS = set()

START = int(os.environ.get("P32SIM_START", "0"), 0)
RAM = int(os.environ.get("P32SIM_RAM", "0"), 0)
ONLY = tuple(os.environ.get("P32SIM_ONLY", "").split())
STRICT = os.environ.get("P32SIM_STRICT", "")
assert STRICT in ("", "report", "error"), STRICT
FOUND = {}                                              # what STRICT found: text -> times

def _tell():
    for k in sorted(FOUND):
        sys.stderr.write("STRICT %s (%d times)\n" % (k, FOUND[k]))
if STRICT == "report":
    atexit.register(_tell)

def clkgen_pressed(st):
    """The CPU card's CLKGEN as one oscillator edge (one CLOCK STEP press) leaves it, at the phase the seed gave. With
    WPH and WPHN both 0 before the first edge the register file and its write latches drive the same lines: a fault of
    the CPU card's own, found 2026-11-25 (docs/backlog.md), which would stop every run here at its first step."""
    ph = (st["P0"] | st["P1"] << 1 | st["P2"] << 2) - 1 & 7
    st["CLK"], st["CLKN"] = int(ph in (7, 0, 1, 2)), int(ph not in (7, 0, 1, 2))
    st["WPH"], st["WPHN"] = int(ph in (7, 0, 1)), int(ph not in (7, 0, 1))
    st["WSTBN"] = int(ph != 0)
PRESET = {} if os.environ.get("P32SIM_CLKGEN") == "asis" else {"CLKGEN": clkgen_pressed}

def alu_guard(alu, known):
    """the ALU's model stops the program at a function code it does not know: as it should in a program, but a random
    microword (START) has any code before the first one is read, and the FPGA answers something"""
    known = set(known)
    return (lambda f, a, b: alu(f, a, b) if f in known else a ^ b ^ 0x5A5A5A5A) if START else alu

def startbit(start, *where):
    """a register bit's level at power-up (0 for a chip P32SIM_ONLY leaves out): the same for the same seed and place, whatever order the chips are met in"""
    if ONLY and not str(where[0]).startswith(ONLY):
        return 0
    return start if start in (0, 1) else random.Random(zlib.crc32(("%d %s" % (start, " ".join(map(str, where)))).encode())).getrandbits(1)

def isclear(p):
    return p.lstrip("-").lstrip("0123456789") in ("MR", "CLR", "PRE", "SRCLR", "CLEAR")
def iscontrol(dev, p):
    """a pin whose floating is a fault whenever: clears, presets, clocks, enables, selects, directions, loads"""
    q = p.lstrip("-").lstrip("0123456789")
    return isclear(p) or q in ("CLK", "CP", "SRCLK", "RCLK", "RCK", "SRCK", "OE", "OE1", "OE2", "E", "EI", "G", "G1", "G2", "G2A", "G2B",
                               "S", "S0", "S1", "S2", "DIR", "CE", "WE", "ENP", "ENT", "LOAD", "SRLOAD", "LS") and not (
        dev in ("74HCT138", "74HCT154", "74F283") and q in ("S0", "S1", "S2"))

CLOCKPINS = {"74HCT595": ("SRCLK", "RCLK"), "74HCT597": ("RCK", "SRCK"), "74HCT175": ("CP",)}

def _num(word, bits):
    return [(word >> i) & 1 for i in range(bits)]

class Sim:
    def __init__(self, card, gals=()):
        self.card = card
        self.pinnet = {}
        for net, pins in card.nets.items():
            for rp in pins:
                self.pinnet[rp] = net
        self.gal = {}                                   # ref -> Gal
        for g in gals:
            for ref, (dev, f, v) in card.parts.items():
                if dev == "GAL_" + g.name:
                    self.gal[ref] = g
        self.pull = {}                                  # net -> 0/1 from resistors
        for ref, (dev, f, v) in card.parts.items():
            if dev in ("R", "RNET9"):
                ends = [self.pinnet.get((ref, p)) for p in DEV[dev]["pins"]]
                rail = 1 if any(e == "VCC" or (e or "").split(".")[-1].startswith("+3V3") for e in ends) else 0 if "GND" in ends else None
                if rail is not None:
                    for n in ends:
                        if n not in ("VCC", "GND", None):
                            self.pull[n] = rail
        self.ext = {"VCC": 1, "GND": 0}                 # nets forced from outside (the bus)
        self.rom = {}                                   # flash contents by ref
        self.uart_out = {}                              # bytes a UART sent, by ref
        self.alu = None                                 # the ALU function (an FPGA model needs it)
        self.val = dict(self.ext)
        self.state = {}                                 # ref -> chip state
        self.models = {}
        for ref, (dev, f, v) in card.parts.items():
            m = self._model(dev)
            if m:
                self.models[ref] = m
        self.clocknets = set()                          # nets some chip clocks on (edges found while settling)
        for ref, (dev, f, v) in card.parts.items():
            if twin(dev) in ("74HCT163", "74HCT161", "74F161", "74F163", "74HCT374", "74HCT377", "74HCT574", "74LVC574", "74ACT574", "74HCT273"):
                self.clocknets.add(self.net(ref, "CLK"))
            elif dev in CLOCKPINS:
                for cp in CLOCKPINS[dev]:
                    self.clocknets.add(self.net(ref, cp))
            elif ref in self.gal and self.gal[ref].clock:
                self.clocknets.add(self.net(ref, self.gal[ref].clock))
        self.clocknets.discard(None)
        # event-driven evaluation: who reads each net, who is clocked by it
        self.listen = {}
        self.clocked = {}
        for ref in self.models:
            dev = card.parts[ref][0]
            for p in DEV[dev]["pins"]:
                n = self.pinnet.get((ref, p))
                if n is not None:
                    self.listen.setdefault(n, set()).add(ref)
            ck = None
            if twin(dev) in ("74HCT163", "74HCT161", "74F161", "74F163", "74HCT374", "74HCT377", "74HCT574", "74LVC574", "74ACT574", "74HCT273"):
                ck = self.pinnet.get((ref, "CLK"))
            elif dev in CLOCKPINS:                        # (parts with two clocks)
                for cp in CLOCKPINS[dev]:
                    n = self.pinnet.get((ref, cp))
                    if n is not None:
                        self.clocked.setdefault(n, set()).add(ref)
            elif twin(dev) == "74HCT74":
                for k in (1, 2):                          # (both flip-flops' clocks)
                    n = self.pinnet.get((ref, "%dCLK" % k))
                    if n is not None:
                        self.clocked.setdefault(n, set()).add(ref)
            elif dev == "TL16C550":
                ck = self.pinnet.get((ref, "-WR1"))       # a write takes effect at the end of -WR
            elif ref in self.gal and self.gal[ref].clock:
                ck = self.pinnet.get((ref, self.gal[ref].clock))
            if ck is not None:
                self.clocked.setdefault(ck, set()).add(ref)
        self.contrib = {}                               # net -> {(ref, pin): value} of drivers
        self._extdirty = set(self.ext)
        self._first = True
        self._fights = set()
        self._began = False                             # a clock edge has come (STRICT looks from then on)
        self._ctl = None
        self._wrote = set()                             # SRAM cells written since all last settled
        self._poke = set()                              # chips whose state a model changed: evaluate them

    # ------------------------------------------------------------ pins
    def net(self, ref, pin):
        return self.pinnet.get((ref, pin))
    def get(self, ref, pin):
        n = self.net(ref, pin)
        return None if n is None else self.val.get(n)
    def _force(self, n, v):
        """Drive (v = 0/1) or release (None) a net from outside."""
        if v is None:
            if n in self.ext:
                del self.ext[n]
                self._extdirty.add(n)
        else:
            v = 1 if v else 0
            if self.ext.get(n) != v:
                self.ext[n] = v
                self._extdirty.add(n)
    def set(self, **nets):
        """Drive nets from outside (bus signals); None releases a net."""
        for n, v in nets.items():
            self._force(n.replace("_", "-") if n.startswith("_") else n, v)
    def word(self, prefix, bits, start=0):
        vals = [self.val.get("%s%d" % (prefix, start + i)) for i in range(bits)]
        if None in vals:
            raise SimError("%s%d..: floating bit(s) %s" % (prefix, start, vals))
        return sum(b << i for i, b in enumerate(vals))
    def set_word(self, prefix, bits, value, start=0):
        for i, b in enumerate(_num(value, bits) if value is not None else [None] * bits):
            self._force("%s%d" % (prefix, start + i), b)

    # ------------------------------------------------------------ power-up, and strictness (D153)
    def _fresh(self, ref, what, bits):
        return sum(startbit(START, ref, what, i) << i for i in range(bits))
    def _fresh4(self, ref):
        st = self.state.get(ref)
        if st is None:
            st = self.state[ref] = {"q": self._fresh(ref, "q", 4)}
        return st
    def _fresh8(self, ref, n=8):
        st = self.state.get(ref)
        if st is None:
            st = self.state[ref] = {"q": [startbit(START, ref, "q", i) for i in range(n)]}
        return st
    def _found(self, text):
        FOUND[text] = FOUND.get(text, 0) + 1
        if STRICT == "error":
            raise SimError("STRICT " + text)
    def _taken(self, ref, pin):
        """an input a register takes at its clock: None is a floating one"""
        x = self.get(ref, pin)
        if x is None and STRICT and self._began:
            self._found("%s %s (%s) takes %s floating" % (self.card.parts[ref][0], ref, pin, self.net(ref, pin)))
        return x or 0
    def _look(self):
        """where all is settled: the control pins and the GALs"""
        if self._ctl is None:
            self._ctl, self._galnets = {}, {}
            for ref, (dev, f, v) in self.card.parts.items():
                if ref not in self.models:
                    continue
                if ref in self.gal:
                    for p in self.gal[ref].inputs:
                        n = self.net(ref, p)
                        if n:
                            self._galnets.setdefault(n, set()).add(ref)
                elif dev in DEV:
                    for p in DEV[dev]["pins"]:
                        if iscontrol(twin(dev), p) and self.net(ref, p):
                            self._ctl.setdefault(self.net(ref, p), []).append((ref, p))
        gals = set()
        for n, pins in self._ctl.items():
            if self.val.get(n) is None:
                ref, p = pins[0]
                self._found("%s floats: %s of %s %s%s" % (n, p, self.card.parts[ref][0], ref,
                                                          " and %d more" % (len(pins) - 1) if len(pins) > 1 else ""))
        for n, refs in self._galnets.items():
            if self.val.get(n) is None:
                gals |= refs
        for ref in gals:
            g = self.gal[ref]
            loose = [n for n in g.inputs if self.get(ref, n) is None and self.net(ref, n)]
            for n in loose[:6]:
                if self._decides(ref, n):
                    self._found("%s floats and decides GAL %s" % (self.net(ref, n), g.name))
    def _decides(self, ref, name):
        """does this input's level change an output or a next state, the other floating ones at 0"""
        g, res = self.gal[ref], []
        for x in (0, 1):
            v = {n: (self.get(ref, n) or 0) for n in g.inputs}
            v[name] = x
            if g.reset == name and x == 0:
                return True
            for o in g.outs.values():
                v[o.name] = (self.state[ref][o.name] ^ (1 if o.inverted else 0)) if o.kind == "reg" else 0
            for _ in range(8):
                for o in g.outs.values():
                    if o.kind != "reg":
                        v[o.name] = int(eval_sop(o.eq, v)) ^ (1 if o.inverted else 0)
            res.append([(o.name, int(eval_sop(o.eq, v)), o.kind == "tri" and int(eval_sop(o.oe, v))) for o in g.outs.values()])
        return res[0] != res[1]

    # ------------------------------------------------------------ models
    def _model(self, dev):
        if dev.startswith("GAL_"):
            return self._gal
        models = {"74HCT163": self._163, "74HCT161": self._163, "74HCT374": self._374,
                "74HCT540": self._540, "74HCT541": self._541, "74HCT245": self._245,
                "74HCT00": self._gate(lambda a, b: 1 - (a & b)), "74HCT08": self._gate(lambda a, b: a & b),
                "74HCT32": self._gate(lambda a, b: a | b), "74HCT27": self._nor3, "74HCT30": self._nand8,
                "74HC05": self._od_inv, "7406": self._od_inv, "74HCT4002": self._nor4, "74HCT377": self._377,
                "74HCT244": self._244, "74HCT157": self._mux2(False), "74HCT257": self._mux2(True),
                "74HCT153": self._mux4(False), "74HCT253": self._mux4(True), "74HCT688": self._688,
                "CY7C199": self._sram, "AS6C4008": self._sram, "74HCT125": self._125, "74HCT138": self._138,
                "74HCT14": self._inv6, "74HCT148": self._148, "74HCT151": self._151,
                "74HCT574": self._374, "74LVC574": self._374, "74LVC244": self._244, "CD74HC4050": self._4050,
                "SST39SF010A": self._rom, "SST39SF010A_PLCC": self._rom, "SST39SF020A": self._rom,
                "TL16C550": self._uart, "TANG_NANO_20K": self._nano,
                "74HCT85": self._85, "74F283": self._283, "74LS145": self._145,
                "74HCT154": self._154, "74HCT74": self._74, "74HCT273": self._273, "74F161": self._161, "74ACT574": self._374,
                "74LVC245": self._245,
                "74F163": self._163, "74ACT541": self._541, "74ACT04": self._inv6, "74F151": self._151,
                "74HCT595": self._595, "74HCT597": self._597, "74HCT175": self._175, "74HCT393": self._393,
                "74HC280": self._280, "TIL311": self._til311}
        TWINS.update(k for k in models if k.startswith("74HCT"))
        return models.get(dev) or models.get(twin(dev))

    def _gal(self, ref, edge):
        g = self.gal[ref]
        st = self.state.get(ref)
        if st is None:
            st = self.state[ref] = {o.name: startbit(START, ref, o.name) for o in g.outs.values() if o.kind == "reg"}
            if START and g.name in PRESET and (not ONLY or ref.startswith(ONLY)):
                PRESET[g.name](st)
        v = {}
        for n in g.inputs:
            x = self.get(ref, n)
            v[n] = 0 if x is None else x                # (a floating GAL input reads as 0 here)
        comb = self.state.setdefault(ref + "#comb", {})  # combinational outputs, fed back inside
        for o in g.outs.values():                       # feedback: registers / macrocells
            if o.kind == "reg":
                v[o.name] = st[o.name] ^ (1 if o.inverted else 0)
            else:
                v[o.name] = comb.get(o.name, 0)
        if not edge and g.reset and self.get(ref, g.reset) == 0:
            for k in st:                                # asynchronous reset (AR)
                st[k] = 0
        if edge:                                        # clock edge: registers take their equations
            if g.clock and edge.get(self.net(ref, g.clock)):
                for o in g.outs.values():
                    if o.kind == "reg":
                        st[o.name] = int(eval_sop(o.eq, v))
            return {}
        # a combinational output's feedback is its macrocell's value, whether
        # or not the pin goes anywhere: iterate to a fixed point inside
        for _ in range(8):
            changed = False
            for o in g.outs.values():
                if o.kind != "reg":
                    x = int(eval_sop(o.eq, v)) ^ (1 if o.inverted else 0)
                    if v[o.name] != x:
                        v[o.name] = x
                        changed = True
            if not changed:
                break
        out = {}
        for o in g.outs.values():
            if o.kind == "reg":
                pin = st[o.name] ^ (1 if o.inverted else 0)
            else:
                pin = v[o.name]
                comb[o.name] = pin
            if o.kind == "tri" and not eval_sop(o.oe, v):
                pin = None
            out[o.name] = pin
        return out

    def _163(self, ref, edge):
        st = self._fresh4(ref)
        if edge:
            if edge.get(self.net(ref, "CLK")):
                g = lambda p: self.get(ref, p) or 0
                if g("-CLR") == 0:
                    st["q"] = 0
                elif g("-LOAD") == 0:
                    st["q"] = sum(self._taken(ref, p) << i for i, p in enumerate("ABCD"))
                elif g("ENP") and g("ENT"):
                    st["q"] = (st["q"] + 1) & 15
            return {}
        q = st["q"]
        ent = self.get(ref, "ENT") or 0
        return {"QA": q & 1, "QB": q >> 1 & 1, "QC": q >> 2 & 1, "QD": q >> 3 & 1,
                "RCO": int(ent and q == 15)}

    def _374(self, ref, edge):
        st = self._fresh8(ref)
        if edge:
            if edge.get(self.net(ref, "CLK")):
                st["q"] = [self._taken(ref, "D%d" % i) for i in range(8)]
            return {}
        if self.get(ref, "-OE") != 0:
            return {"Q%d" % i: None for i in range(8)}
        return {"Q%d" % i: st["q"][i] for i in range(8)}

    def _377(self, ref, edge):
        st = self._fresh8(ref)
        if edge:
            if edge.get(self.net(ref, "CLK")) and self.get(ref, "-E") == 0:
                st["q"] = [self._taken(ref, "D%d" % i) for i in range(8)]
            return {}
        return {"Q%d" % i: st["q"][i] for i in range(8)}

    def _244(self, ref, edge):
        if edge:
            return {}
        out = {}
        for h in (1, 2):
            on = self.get(ref, "-%dG" % h) == 0
            for i in range(1, 5):
                a = self.get(ref, "%dA%d" % (h, i))
                out["%dY%d" % (h, i)] = a if on else None
        return out

    def _mux2(self, tri):
        def m(ref, edge):
            if edge:
                return {}
            en = self.get(ref, "-OE" if tri else "-E") == 0
            s_ = self.get(ref, "S")
            out = {}
            for k in range(1, 5):
                v = self.get(ref, "%d%s" % (k, "B" if s_ else "A")) if s_ is not None else None
                out["%dY" % k] = (v if en else None) if tri else (v if en else 0)
            return out
        return m

    def _mux4(self, tri):
        def m(ref, edge):
            if edge:
                return {}
            s0, s1 = self.get(ref, "S0"), self.get(ref, "S1")
            out = {}
            for h in (1, 2):
                en = self.get(ref, ("-%dOE" if tri else "-%dE") % h) == 0
                v = None if s0 is None or s1 is None else self.get(ref, "%dI%d" % (h, s1 << 1 | s0))
                out["%dY" % h] = (v if en else None) if tri else (v if en else 0)
            return out
        return m

    def _688(self, ref, edge):
        if edge:
            return {}
        ps = [self.get(ref, "P%d" % i) for i in range(8)]
        qs = [self.get(ref, "Q%d" % i) for i in range(8)]
        g = self.get(ref, "-G")
        if None in ps or None in qs or g is None:
            return {"-P=Q": None}
        return {"-P=Q": 0 if (g == 0 and ps == qs) else 1}

    def _sram(self, ref, edge):
        """Asynchronous SRAM (CY7C199, AS6C4008): writes while -WE and -CE are low."""
        st = self.state.setdefault(ref, {"mem": {}})
        if edge:
            return {}
        big = self.card.parts[ref][0] == "AS6C4008"
        IO = "DQ%d" if big else "IO%d"
        a = [self.get(ref, "A%d" % i) for i in range(19 if big else 15)]
        if None in a:
            addr = None
        else:
            addr = sum(b << i for i, b in enumerate(a))
        ce, oe, we = self.get(ref, "-CE"), self.get(ref, "-OE"), self.get(ref, "-WE")
        if ce == 0 and we == 0 and addr is not None:
            st["mem"][addr] = [self.get(ref, IO % i) for i in range(8)]
            self._wrote.add((ref, addr))
            return {IO % i: None for i in range(8)}
        if ce == 0 and oe == 0 and we == 1 and addr is not None:
            word = st["mem"].get(addr) or [startbit(RAM, ref, addr, i) for i in range(8)]
            return {IO % i: word[i] for i in range(8)}
        return {IO % i: None for i in range(8)}

    # flash: contents from self.rom[ref] (bytes); read-only here
    def _rom(self, ref, edge):
        if edge:
            return {}
        dev = self.card.parts[ref][0]
        n = 18 if dev == "SST39SF020A" else 17
        if self.get(ref, "-CE") != 0 or self.get(ref, "-OE") != 0:
            return {"DQ%d" % i: None for i in range(8)}
        a = [self.get(ref, "A%d" % i) for i in range(n)]
        if None in a:
            return {"DQ%d" % i: None for i in range(8)}
        addr = sum(b << i for i, b in enumerate(a))
        data = self.rom.get(ref, b"")
        byte = data[addr] if addr < len(data) else 0xFF
        return {"DQ%d" % i: byte >> i & 1 for i in range(8)}

    def _125(self, ref, edge):
        if edge:
            return {}
        return {"%dY" % k: (self.get(ref, "%dA" % k) if self.get(ref, "-%dOE" % k) == 0 else None) for k in range(1, 5)}

    def _138(self, ref, edge):
        if edge:
            return {}
        en = self.get(ref, "G1") == 1 and self.get(ref, "-G2A") == 0 and self.get(ref, "-G2B") == 0
        sel = [self.get(ref, p) for p in "ABC"]
        if 0 in (self.get(ref, "G1"),) or 1 in (self.get(ref, "-G2A"), self.get(ref, "-G2B")):
            return {"Y%d" % i: 1 for i in range(8)}     # (disabled: all high, whatever is selected)
        if None in sel:
            return {"Y%d" % i: None for i in range(8)}
        k = sel[0] | sel[1] << 1 | sel[2] << 2
        return {"Y%d" % i: 0 if (en and i == k) else 1 for i in range(8)}

    def _inv6(self, ref, edge):
        if edge:
            return {}
        out = {}
        for k in range(1, 7):
            a = self.get(ref, "%dA" % k)
            out["%dY" % k] = None if a is None else 1 - a
        return out

    def _148(self, ref, edge):
        """8-to-3 priority encoder, active low, input 7 highest (74x148)."""
        if edge:
            return {}
        if self.get(ref, "-EI") != 0:
            return {"-A0": 1, "-A1": 1, "-A2": 1, "-GS": 1, "-EO": 1}
        ins = [self.get(ref, "-I%d" % i) for i in range(8)]
        act = [i for i in range(8) if ins[i] == 0]
        if not act:
            return {"-A0": 1, "-A1": 1, "-A2": 1, "-GS": 1, "-EO": 0}
        k = max(act)
        return {"-A0": 1 - (k & 1), "-A1": 1 - (k >> 1 & 1), "-A2": 1 - (k >> 2 & 1), "-GS": 0, "-EO": 1}

    def _151(self, ref, edge):
        if edge:
            return {}
        if self.get(ref, "-E") != 0:
            return {"Y": 0, "-W": 1}
        sel = [self.get(ref, p) for p in ("S0", "S1", "S2")]
        if None in sel:
            return {"Y": None, "-W": None}
        y = self.get(ref, "D%d" % (sel[0] | sel[1] << 1 | sel[2] << 2))
        return {"Y": y, "-W": None if y is None else 1 - y}

    def _uart(self, ref, edge):
        """TL16C550, enough to print: THR writes collect in self.uart_out[ref];
        LSR reads 0x60 (always ready to send, nothing received)."""
        st = self.state.setdefault(ref, {"lcr": 0, "regs": {}})
        sel = lambda: (self.get(ref, "CS0") == 1 and self.get(ref, "CS1") == 1 and self.get(ref, "-CS2") == 0)
        reg = lambda: sum((self.get(ref, "A%d" % i) or 0) << i for i in range(3))
        if edge:
            if sel():
                r = reg()
                v = sum((self.get(ref, "D%d" % i) or 0) << i for i in range(8))
                if r == 3:
                    st["lcr"] = v
                elif r == 0 and not st["lcr"] & 0x80:
                    self.uart_out.setdefault(ref, []).append(v)
                else:
                    st["regs"][(r, bool(st["lcr"] & 0x80))] = v
            return {}
        if sel() and self.get(ref, "-RD1") == 0:
            r = reg()
            v = {5: 0x60, 2: 0x01, 3: st["lcr"], 6: 0xB0}.get(r, st["regs"].get((r, bool(st["lcr"] & 0x80)), 0))
            return {"D%d" % i: v >> i & 1 for i in range(8)}
        return {"D%d" % i: None for i in range(8)}

    def _nano(self, ref, edge):
        """The CPU card's FPGA module, the ALU (D21), behaviourally: self.alu
        (the emulator's ALU). Its FD-bus protocol (reading the operand
        latches a half at a time, writing the result latches) is abstracted:
        the result latches always hold alu(function, operand A, operand B)
        of what the operand and function latches hold -- they change only at
        an ALUGO edge, and the FPGA has the result well inside the next
        microcycle (D21). The latches are found by their nets: an operand
        latch's Q is on FD and its D on the A or B bus (ABn / BBn), a result
        latch's D on FD and its Q on R (LRn)."""
        st = self.state.get(ref)
        if st is None:
            st = self.state[ref] = {"in": [], "out": [], "last": None}
            fd = {self.net(ref, p) for p in DEV["TANG_NANO_20K"]["pins"] if p.startswith("FD")}
            for r, (dev, f, v) in self.card.parts.items():
                if dev != "74LVC574":
                    continue
                for i in range(8):
                    q, d = self.net(r, "Q%d" % i), self.net(r, "D%d" % i)
                    qb, db = (q or "").split(".")[-1], (d or "").split(".")[-1]     # (a card's local nets: CARD.NAME)
                    if q in fd and db[:2] in ("AB", "BB"):
                        st["in"].append((r, i, db[:2], int(db[2:])))
                    elif d in fd and qb.startswith("LR"):
                        st["out"].append((r, i, int(qb[2:])))
        if edge:
            return {}
        ops = {"AB": 0, "BB": 0}
        for r, i, bus, bit in st["in"]:
            ops[bus] |= self.state.get(r, {"q": [0] * 8})["q"][i] << bit
        f = sum((self.get(ref, "ALUF%d" % i) or 0) << i for i in range(5))
        key = (f, ops["AB"], ops["BB"])
        if key != st["last"]:
            st["last"] = key
            r_ = self.alu(f, ops["AB"], ops["BB"]) if self.alu else 0
            for r, i, bit in st["out"]:
                q = self.state.setdefault(r, {"q": [0] * 8})["q"]
                if q[i] != r_ >> bit & 1:
                    q[i] = r_ >> bit & 1
                    self._poke.add(r)
        out = {"SELAL_N": 1, "SELAH_N": 1, "SELBL_N": 1, "SELBH_N": 1, "CKLO": 0, "CKHI": 0}
        out.update({"FD%d" % i: None for i in range(16)})
        return out

    def _85(self, ref, edge):
        """4-bit magnitude comparator with cascade inputs (74x85)."""
        if edge:
            return {}
        a = [self.get(ref, "A%d" % i) for i in range(4)]
        b = [self.get(ref, "B%d" % i) for i in range(4)]
        c = [self.get(ref, p) for p in ("IA>B", "IA=B", "IA<B")]
        if None in a or None in b or None in c:
            return {"QA>B": None, "QA=B": None, "QA<B": None}
        av, bv = sum(x << i for i, x in enumerate(a)), sum(x << i for i, x in enumerate(b))
        if av > bv:
            return {"QA>B": 1, "QA=B": 0, "QA<B": 0}
        if av < bv:
            return {"QA>B": 0, "QA=B": 0, "QA<B": 1}
        gt, eq, lt = c
        if eq:                                          # (the 85's truth table: IA=B high wins)
            return {"QA>B": 0, "QA=B": 1, "QA<B": 0}
        return {"QA>B": int(not lt), "QA=B": 0, "QA<B": int(not gt)} if (gt or lt) else \
               {"QA>B": 1, "QA=B": 0, "QA<B": 1}

    def _til311(self, ref, edge):
        """A TIL311 hex display: its latch follows A-D while -LS is low and
        holds them when it is high; self.state[ref]["digit"] is what it shows
        (None while BLANK is high)."""
        st = self.state.setdefault(ref, {"latch": self._fresh(ref, "latch", 4), "digit": 0})
        if not edge and self.get(ref, "-LS") == 0:
            st["latch"] = sum((self.get(ref, p) or 0) << i for i, p in enumerate("ABCD"))
        st["digit"] = None if self.get(ref, "BLANK") else st["latch"]
        return {}

    def _273(self, ref, edge):
        st = self._fresh8(ref)
        if self.get(ref, "-MR") == 0:
            st["q"] = [0] * 8
        elif edge:
            if edge.get(self.net(ref, "CLK")):
                st["q"] = [self._taken(ref, "D%d" % i) for i in range(8)]
            return {}
        if edge:
            return {}
        return {"Q%d" % i: st["q"][i] for i in range(8)}

    def _154(self, ref, edge):
        if edge:
            return {}
        en = self.get(ref, "-G1") == 0 and self.get(ref, "-G2") == 0
        sel = [self.get(ref, p) for p in "ABCD"]
        if 1 in (self.get(ref, "-G1"), self.get(ref, "-G2")):
            return {"Y%d" % i: 1 for i in range(16)}    # (disabled: all high, whatever is selected)
        if None in sel:
            return {"Y%d" % i: None for i in range(16)}
        k = sel[0] | sel[1] << 1 | sel[2] << 2 | sel[3] << 3
        return {"Y%d" % i: 0 if (en and i == k) else 1 for i in range(16)}

    def _161(self, ref, edge):
        """74x161: as the 163, but -CLR clears at once (asynchronously)."""
        st = self._fresh4(ref)
        if not edge and self.get(ref, "-CLR") == 0:
            st["q"] = 0
        return self._163(ref, edge)

    def _74(self, ref, edge):
        """Dual D flip-flop: -PRE / -CLR asynchronous (both low: Q and -Q high)."""
        st = self.state.get(ref)
        if st is None:
            st = self.state[ref] = {1: startbit(START, ref, 1), 2: startbit(START, ref, 2)}
        out = {}
        for k in (1, 2):
            pre, clr = self.get(ref, "-%dPRE" % k), self.get(ref, "-%dCLR" % k)
            if edge:
                if edge.get(self.net(ref, "%dCLK" % k)) and pre != 0 and clr != 0:
                    st[k] = self._taken(ref, "%dD" % k)
                continue
            if pre == 0 and clr == 0:
                out.update({"%dQ" % k: 1, "-%dQ" % k: 1})
                continue
            if pre == 0:
                st[k] = 1
            elif clr == 0:
                st[k] = 0
            out.update({"%dQ" % k: st[k], "-%dQ" % k: 1 - st[k]})
        return out

    def _145(self, ref, edge):
        """BCD-to-decimal decoder, open-collector outputs (74x145): output n
        low when DCBA = n (0-9); 10-15 none."""
        if edge:
            return {}
        x = [self.get(ref, p) for p in "ABCD"]
        if None in x:
            return {"O%d" % k: None for k in range(10)}
        n = x[0] | x[1] << 1 | x[2] << 2 | x[3] << 3
        return {"O%d" % k: 0 if k == n else None for k in range(10)}

    def _595(self, ref, edge):
        """74x595: SRCLK shifts SER in at QA (QA -> QB ... QH -> QH'); RCLK copies
        the shift register to the 3-state output register; -SRCLR clears the
        shift register at once."""
        st = self.state.setdefault(ref, {"sr": self._fresh(ref, "sr", 8), "out": self._fresh(ref, "out", 8)})
        g = lambda p: self.get(ref, p)
        if g("-SRCLR") == 0:
            st["sr"] = 0
        if edge:
            sr = st["sr"]                                # (both clocks see the register as it was)
            if edge.get(self.net(ref, "RCLK")):
                st["out"] = sr
            if edge.get(self.net(ref, "SRCLK")):
                st["sr"] = ((sr << 1) | (g("SER") or 0)) & 0xFF
            return {}
        out = {"QH'": st["sr"] >> 7 & 1}
        on = g("-OE") == 0
        for i, k in enumerate("ABCDEFGH"):
            out["Q" + k] = (st["out"] >> i & 1) if on else None
        return out

    def _597(self, ref, edge):
        """74x597: RCK takes A-H into the input register; -SRLOAD low copies it
        into the shift register (at once, and for as long as it is low); SRCK
        shifts toward QH (SER in at A); QH' is the last stage."""
        st = self.state.setdefault(ref, {"in": self._fresh(ref, "in", 8), "sr": self._fresh(ref, "sr", 8)})
        g = lambda p: self.get(ref, p)
        if edge:
            if edge.get(self.net(ref, "RCK")):
                st["in"] = sum((g(k) or 0) << i for i, k in enumerate("ABCDEFGH"))
                if g("-SRLOAD") == 0:
                    st["sr"] = st["in"]
            if edge.get(self.net(ref, "SRCK")) and g("-SRLOAD") != 0 and g("-SRCLR") != 0:
                st["sr"] = ((st["sr"] << 1) | (g("SER") or 0)) & 0xFF
            return {}
        if g("-SRCLR") == 0:
            st["sr"] = 0
        elif g("-SRLOAD") == 0:
            st["sr"] = st["in"]
        return {"QH'": st["sr"] >> 7 & 1}

    def _175(self, ref, edge):
        st = self._fresh8(ref, 4)
        if self.get(ref, "-MR") == 0:
            st["q"] = [0] * 4
        elif edge:
            if edge.get(self.net(ref, "CP")):
                st["q"] = [self._taken(ref, "D%d" % k) for k in range(1, 5)]
            return {}
        if edge:
            return {}
        out = {}
        for k in range(1, 5):
            out["Q%d" % k] = st["q"][k - 1]
            out["-Q%d" % k] = 1 - st["q"][k - 1]
        return out

    def _393(self, ref, edge):
        """Dual 4-bit ripple counter: each counts as its CP falls (MR high clears)."""
        st = self.state.setdefault(ref, {1: self._fresh(ref, 1, 4), 2: self._fresh(ref, 2, 4), "cp1": None, "cp2": None})
        if edge:
            return {}
        out = {}
        for k in (1, 2):
            cp = self.get(ref, "%dCP" % k)
            if self.get(ref, "%dMR" % k) == 1:
                st[k] = 0
            elif st["cp%d" % k] == 1 and cp == 0:
                st[k] = (st[k] + 1) & 15
            st["cp%d" % k] = cp
            for i in range(4):
                out["%dQ%d" % (k, i)] = st[k] >> i & 1
        return out

    def _280(self, ref, edge):
        if edge:
            return {}
        bits = [self.get(ref, p) for p in "ABCDEFGHI"]
        if None in bits:
            return {"SEVEN": None, "SODD": None}
        odd = sum(bits) & 1
        return {"SEVEN": 1 - odd, "SODD": odd}

    def _283(self, ref, edge):
        """4-bit full adder (pins A1-A4, B1-B4, S1-S4, C0, C4)."""
        if edge:
            return {}
        a = [self.get(ref, "A%d" % i) for i in range(1, 5)]
        b = [self.get(ref, "B%d" % i) for i in range(1, 5)]
        c = self.get(ref, "C0")
        if None in a or None in b or c is None:
            return {"S1": None, "S2": None, "S3": None, "S4": None, "C4": None}
        t = sum(x << i for i, x in enumerate(a)) + sum(x << i for i, x in enumerate(b)) + c
        return {"S1": t & 1, "S2": t >> 1 & 1, "S3": t >> 2 & 1, "S4": t >> 3 & 1, "C4": t >> 4 & 1}

    def _buf(self, ref, inv):
        on = self.get(ref, "-OE1") == 0 and self.get(ref, "-OE2") == 0
        out = {}
        for i in range(8):
            a = self.get(ref, "A%d" % i)
            out["Y%d" % i] = (None if a is None else a ^ inv) if on else None
        return out
    def _540(self, ref, edge): return {} if edge else self._buf(ref, 1)
    def _541(self, ref, edge): return {} if edge else self._buf(ref, 0)

    def _245(self, ref, edge):
        if edge:
            return {}
        out = {}
        on = self.get(ref, "-OE") == 0
        a_to_b = self.get(ref, "DIR") == 1
        for i in range(1, 9):
            out["A%d" % i] = (self.get(ref, "B%d" % i) if not a_to_b else None) if on else None
            out["B%d" % i] = (self.get(ref, "A%d" % i) if a_to_b else None) if on else None
        return out

    def _gate(self, f):
        def m(ref, edge):
            if edge:
                return {}
            out = {}
            for k in range(1, 5):
                a, b = self.get(ref, "%dA" % k), self.get(ref, "%dB" % k)
                y = {f(p, q) for p in ((0, 1) if a is None else (a,)) for q in ((0, 1) if b is None else (b,))}
                out["%dY" % k] = y.pop() if len(y) == 1 else None      # (a floating input the other one decides over)
            return out
        return m
    def _nor3(self, ref, edge):
        if edge:
            return {}
        out = {}
        for k in range(1, 4):
            ins = [self.get(ref, "%d%s" % (k, x)) for x in "ABC"]
            out["%dY" % k] = None if None in ins else int(not any(ins))
        return out
    def _nor4(self, ref, edge):
        if edge:
            return {}
        out = {}
        for k in (1, 2):
            ins = [self.get(ref, "%d%s" % (k, x)) for x in "ABCD"]
            out["%dY" % k] = None if None in ins else int(not any(ins))
        return out
    def _nand8(self, ref, edge):
        if edge:
            return {}
        ins = [self.get(ref, x) for x in "ABCDEFGH"]
        return {"Y": None if None in ins else int(not all(ins))}
    def _4050(self, ref, edge):                         # hex buffer (a level shifter)
        if edge:
            return {}
        return {"%dY" % k: self.get(ref, "%dA" % k) for k in range(1, 7)}

    def _od_inv(self, ref, edge):                       # open drain: pulls low or lets go
        if edge:
            return {}
        out = {}
        for k in range(1, 7):
            a = self.get(ref, "%dA" % k)
            out["%dY" % k] = 0 if a == 1 else None
        return out

    # ------------------------------------------------------------ evaluation
    def _resolve(self, n):
        ds = self.contrib.get(n)
        if n in self.ext:
            if ds and n not in ("VCC", "GND") and any(v != self.ext[n] for v in ds.values()):
                raise SimError("net %s: driven %s by %s but forced %d from outside"
                               % (n, set(ds.values()), ["%s.%s" % k for k in ds], self.ext[n]))
            return self.ext[n]
        if ds:
            vals = set(ds.values())
            if len(vals) > 1:                           # a fight: unknown; an error only if it lasts
                self._fights.add(n)
                return None
            self._fights.discard(n)
            return vals.pop()
        self._fights.discard(n)
        return self.pull.get(n)

    def settle(self, limit=100000):
        """Event-driven: evaluate the chips whose nets changed, in waves; a
        clock net rising in a wave clocks its chips with the values from
        before the wave (their inputs as they were at the edge)."""
        if self._first:
            queue = set(self.models)
            dirty = set(self.pinnet.values()) | set(self.ext)
            self._first = False
        else:
            queue = set()
            dirty = set()
        dirty |= self._extdirty
        self._extdirty = set()
        for _ in range(limit):
            for ref in queue:
                for pin, v in self.models[ref](ref, None).items():
                    n = self.pinnet.get((ref, pin))
                    if n is None:
                        continue
                    c = self.contrib.setdefault(n, {})
                    key = (ref, pin)
                    if v is None:
                        if key in c:
                            del c[key]
                            dirty.add(n)
                    elif c.get(key) != v:
                        c[key] = v
                        dirty.add(n)
            new = {}
            for n in dirty:
                v = self._resolve(n)
                if v != self.val.get(n):
                    new[n] = v
            dirty = set()
            if not new and self._poke:                  # a model changed another chip's state
                queue, self._poke = self._poke, set()
                continue
            if not new:
                for n in self._fights:                  # still fighting once everything settled
                    ds = self.contrib.get(n, {})
                    if len(set(ds.values())) > 1:
                        raise SimError("bus fight on %s: %s" % (n, [("%s.%s" % k, v) for k, v in ds.items()]))
                self._fights = set()
                if STRICT and self._began:
                    self._look()
                    for ref, a in self._wrote:
                        if None in self.state[ref]["mem"].get(a, ()):
                            self._found("%s %s is written with a floating bit" % (self.card.parts[ref][0], ref))
                self._wrote.clear()
                return
            edged = set()
            for n, v in new.items():
                if v == 1 and self.val.get(n) == 0 and n in self.clocked:
                    self._began = True
                    for ref in self.clocked[n]:
                        self.models[ref](ref, {n: True})
                        edged.add(ref)
            self.val.update(new)
            queue = set(edged) | self._poke
            self._poke = set()
            for n in new:
                queue |= self.listen.get(n, set())
        raise SimError("logic did not settle (oscillation?)")

    def step(self, **nets):
        """Apply bus inputs and settle."""
        self.set(**nets)
        self.settle()

    def clock(self, net, level):
        """Drive a clock net from outside; edges are handled while settling."""
        self._force(net, level)
        self.settle()
