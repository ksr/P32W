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
"""
from p32net import DEV
from p32gal import eval_sop

class SimError(Exception):
    pass

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
                rail = 1 if any(e == "VCC" or (e or "").startswith("+3V3") for e in ends) else 0 if "GND" in ends else None
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
            if dev in ("74HCT163", "74HCT161", "74HCT374", "74HCT377", "74HCT574", "74LVC574"):
                self.clocknets.add(self.net(ref, "CLK"))
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
            if dev in ("74HCT163", "74HCT161", "74HCT374", "74HCT377", "74HCT574", "74LVC574"):
                ck = self.pinnet.get((ref, "CLK"))
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

    # ------------------------------------------------------------ models
    def _model(self, dev):
        if dev.startswith("GAL_"):
            return self._gal
        return {"74HCT163": self._163, "74HCT161": self._163, "74HCT374": self._374,
                "74HCT540": self._540, "74HCT541": self._541, "74HCT245": self._245,
                "74HCT00": self._gate(lambda a, b: 1 - (a & b)), "74HCT08": self._gate(lambda a, b: a & b),
                "74HCT32": self._gate(lambda a, b: a | b), "74HCT27": self._nor3, "74HCT30": self._nand8,
                "74HC05": self._od_inv, "74HCT4002": self._nor4, "74HCT377": self._377,
                "74HCT244": self._244, "74HCT157": self._mux2(False), "74HCT257": self._mux2(True),
                "74HCT153": self._mux4(False), "74HCT253": self._mux4(True), "74HCT688": self._688,
                "CY7C199": self._sram, "AS6C4008": self._sram, "74HCT125": self._125, "74HCT138": self._138,
                "74HCT14": self._inv6, "74HCT148": self._148, "74HCT151": self._151,
                "74HCT574": self._374, "74LVC574": self._374, "74LVC244": self._244, "CD74HC4050": self._4050,
                "SST39SF010A": self._rom, "SST39SF010A_PLCC": self._rom, "SST39SF020A": self._rom,
                "TL16C550": self._uart, "TANG_NANO_20K": self._nano}.get(dev)

    def _gal(self, ref, edge):
        g = self.gal[ref]
        st = self.state.setdefault(ref, {o.name: 0 for o in g.outs.values() if o.kind == "reg"})
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
        st = self.state.setdefault(ref, {"q": 0})
        if edge:
            if edge.get(self.net(ref, "CLK")):
                g = lambda p: self.get(ref, p) or 0
                if g("-CLR") == 0:
                    st["q"] = 0
                elif g("-LOAD") == 0:
                    st["q"] = sum(g(p) << i for i, p in enumerate("ABCD"))
                elif g("ENP") and g("ENT"):
                    st["q"] = (st["q"] + 1) & 15
            return {}
        q = st["q"]
        ent = self.get(ref, "ENT") or 0
        return {"QA": q & 1, "QB": q >> 1 & 1, "QC": q >> 2 & 1, "QD": q >> 3 & 1,
                "RCO": int(ent and q == 15)}

    def _374(self, ref, edge):
        st = self.state.setdefault(ref, {"q": [0] * 8})
        if edge:
            if edge.get(self.net(ref, "CLK")):
                st["q"] = [self.get(ref, "D%d" % i) or 0 for i in range(8)]
            return {}
        if self.get(ref, "-OE") != 0:
            return {"Q%d" % i: None for i in range(8)}
        return {"Q%d" % i: st["q"][i] for i in range(8)}

    def _377(self, ref, edge):
        st = self.state.setdefault(ref, {"q": [0] * 8})
        if edge:
            if edge.get(self.net(ref, "CLK")) and self.get(ref, "-E") == 0:
                st["q"] = [self.get(ref, "D%d" % i) or 0 for i in range(8)]
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
            return {IO % i: None for i in range(8)}
        if ce == 0 and oe == 0 and we == 1 and addr is not None:
            word = st["mem"].get(addr, [0] * 8)
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
        """The CPU card's FPGA module, the ALU (D21), behaviourally: fpga/alu/gen_alu.py's
        ALU (= the emulator's). Its read and write protocol over FD is
        abstracted -- p32alu_top.v does it in Verilog, verified with the card's
        chips in tb_p32alu.v: here the module takes an operand straight from
        the input latches and writes the result straight into the output
        latches. It loads in the datapath phase (CLK falling) from the latches
        and LDA / LDB, which hold from the rising edge all microcycle -- as the
        FPGA does about 60 ns after the edge."""
        st = self.state.get(ref)
        if st is None:
            st = self.state[ref] = {"a": 0, "b": 0, "clk": None, "in": [], "out": []}
            fd = {self.net(ref, p) for p in DEV["TANG_NANO_20K"]["pins"] if p.startswith("FD")}
            for r, (dev, f, v) in self.card.parts.items():
                if dev != "74LVC574":
                    continue
                for i in range(8):
                    q, d = self.net(r, "Q%d" % i), self.net(r, "D%d" % i)
                    if q in fd:                     # input latch: Q -> FD, bus bit from its D net
                        st["in"].append((r, i, int(d.split(":")[-1][1:])))
                    elif d in fd:                   # output latch: FD -> D, bus bit from its Q net
                        st["out"].append((r, i, int(q.split(":")[-1][1:])))
        if edge:
            return {}
        g = lambda p: self.get(ref, p) or 0
        clk = self.get(ref, "BCLK")
        if self.get(ref, "RES_N") == 0:
            st["a"] = st["b"] = 0
        elif st["clk"] == 1 and clk == 0 and (g("LDA") or g("LDB")):
            v = 0
            for r, i, bit in st["in"]:
                v |= self.state.get(r, {"q": [0] * 8})["q"][i] << bit
            if g("LDA"):
                st["a"] = v
            if g("LDB"):
                st["b"] = v
        st["clk"] = clk
        a, b = st["a"], st["b"]
        r_ = self.alu(sum(g("ALUF%d" % i) << i for i in range(5)), a, b) if self.alu else 0
        for r, i, bit in st["out"]:
            q = self.state.setdefault(r, {"q": [0] * 8})["q"]
            if q[i] != r_ >> bit & 1:
                q[i] = r_ >> bit & 1
                self._poke.add(r)
        sa = a - (1 << 32) if a >> 31 else a
        sb = b - (1 << 32) if b >> 31 else b
        out = {"EQ": int(a == b), "LT": int(sa < sb), "LTU": int(a < b),
               "SELLO_N": 1, "SELHI_N": 1, "CKLO": 0, "CKHI": 0}
        out.update({"FD%d" % i: None for i in range(16)})
        return out

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
                out["%dY" % k] = None if a is None or b is None else f(a, b)
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
                return
            edged = set()
            for n, v in new.items():
                if v == 1 and self.val.get(n) == 0 and n in self.clocked:
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
