#!/usr/bin/env python3
"""P32W card netlists: device library, netlist helpers and electrical checks
(P8X convention: netlists are canonical and live in Python; schematics,
boards and documents are generated from them).

A card is built with
    c = Card("cpu-card", "P32W CPU CARD")
    c.part("U1", "74HCT138", "DOE decode")
    c.net("-DOEREG", ("U1", "Y1"), ("U5", "-OE"))
    c.bus("D0", ("U9", "A1"))          # bus net: also wired to its J1-J3 pin
    CARDS[c.name] = c
and checked with check(card). Every device pin has a type, so the checker
can find floating inputs, outputs fighting each other, unconnected pins and
power/decoupling mistakes before any CAD tool sees the card.
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from p32bus import pinnet as busnet, ALLPINS, SIGNALS, CONNECTORS  # noqa: E402  (the bus, D50)

# ---------------------------------------------------------------- device library
# pin types: in, out, tri (3-state out), bidir, oc (open-drain), pwr, pwr3 (3.3 V rail), gnd, pas (passive)
DEV = {}

def D(name, pkg, pins, value=None):
    """pins: {pin name: (pin number, type)}"""
    DEV[name] = dict(pkg=pkg, pins=pins, value=value or name)

def _gates(ins, out):
    """Quad 2-input gate, 14-pin (74x08 / 74x32 pinout)."""
    p = {"VCC": ("14", "pwr"), "GND": ("7", "gnd")}
    for g, (a, b, y) in enumerate(((1, 2, 3), (4, 5, 6), (9, 10, 8), (12, 13, 11)), 1):
        p["%dA" % g] = (str(a), "in"); p["%dB" % g] = (str(b), "in"); p["%dY" % g] = (str(y), "out")
    return p

D("74HCT08", "DIP14", _gates(2, 1))                     # quad AND
D("74HCT32", "DIP14", _gates(2, 1))                     # quad OR
D("74HCT138", "DIP16", {
    "A": ("1", "in"), "B": ("2", "in"), "C": ("3", "in"), "-G2A": ("4", "in"),
    "-G2B": ("5", "in"), "G1": ("6", "in"), "Y7": ("7", "out"), "GND": ("8", "gnd"),
    "Y6": ("9", "out"), "Y5": ("10", "out"), "Y4": ("11", "out"), "Y3": ("12", "out"),
    "Y2": ("13", "out"), "Y1": ("14", "out"), "Y0": ("15", "out"), "VCC": ("16", "pwr")})
D("74HCT161", "DIP16", {
    "-CLR": ("1", "in"), "CLK": ("2", "in"), "A": ("3", "in"), "B": ("4", "in"),
    "C": ("5", "in"), "D": ("6", "in"), "ENP": ("7", "in"), "GND": ("8", "gnd"),
    "-LOAD": ("9", "in"), "ENT": ("10", "in"), "QD": ("11", "out"), "QC": ("12", "out"),
    "QB": ("13", "out"), "QA": ("14", "out"), "RCO": ("15", "out"), "VCC": ("16", "pwr")})
D("74HCT244", "DIP20", {
    "-1G": ("1", "in"), "1A1": ("2", "in"), "2Y4": ("3", "tri"), "1A2": ("4", "in"),
    "2Y3": ("5", "tri"), "1A3": ("6", "in"), "2Y2": ("7", "tri"), "1A4": ("8", "in"),
    "2Y1": ("9", "tri"), "GND": ("10", "gnd"), "2A1": ("11", "in"), "1Y4": ("12", "tri"),
    "2A2": ("13", "in"), "1Y3": ("14", "tri"), "2A3": ("15", "in"), "1Y2": ("16", "tri"),
    "2A4": ("17", "in"), "1Y1": ("18", "tri"), "-2G": ("19", "in"), "VCC": ("20", "pwr")})
D("74HCT245", "DIP20", dict(
    [("DIR", ("1", "in")), ("-OE", ("19", "in")), ("GND", ("10", "gnd")), ("VCC", ("20", "pwr"))] +
    [("A%d" % i, (str(i + 1), "bidir")) for i in range(1, 9)] +
    [("B%d" % i, (str(19 - i), "bidir")) for i in range(1, 9)]))
D("CY7C199", "DIP28-300", dict(                         # 32K x 8 fast SRAM, 15 ns, 62256 pinout
    [("A%d" % a, (n, "in")) for a, n in ((14, "1"), (12, "2"), (7, "3"), (6, "4"), (5, "5"),
                                         (4, "6"), (3, "7"), (2, "8"), (1, "9"), (0, "10"),
                                         (10, "21"), (11, "23"), (9, "24"), (8, "25"), (13, "26"))] +
    [("IO%d" % i, (n, "bidir")) for i, n in enumerate(("11", "12", "13", "15", "16", "17", "18", "19"))] +
    [("GND", ("14", "gnd")), ("-CE", ("20", "in")), ("-OE", ("22", "in")),
     ("-WE", ("27", "in")), ("VCC", ("28", "pwr"))]), value="CY7C199-15PC")
def _reg374(first, qtype):
    """374/377 pinout: 1 = -OE or -E, 11 = CLK, D/Q pairs."""
    p = {first: ("1", "in"), "CLK": ("11", "in"), "GND": ("10", "gnd"), "VCC": ("20", "pwr")}
    for i, (d, q) in enumerate(((3, 2), (4, 5), (7, 6), (8, 9), (13, 12), (14, 15), (17, 16), (18, 19))):
        p["D%d" % i] = (str(d), "in"); p["Q%d" % i] = (str(q), qtype)
    return p

D("74HCT374", "DIP20", _reg374("-OE", "tri"))                 # octal D flip-flop, 3-state
D("74HCT377", "DIP20", _reg374("-E", "out"))                  # octal D flip-flop with clock enable
def _buf541(otype):
    p = {"-OE1": ("1", "in"), "-OE2": ("19", "in"), "GND": ("10", "gnd"), "VCC": ("20", "pwr")}
    for i in range(8):
        p["A%d" % i] = (str(2 + i), "in"); p["Y%d" % i] = (str(18 - i), otype)
    return p
D("74HCT541", "DIP20", _buf541("tri"))                        # octal buffer, 3-state
D("74HCT540", "DIP20", _buf541("tri"))                        # octal inverting buffer, 3-state
D("74HCT157", "DIP16", {
    "S": ("1", "in"), "1A": ("2", "in"), "1B": ("3", "in"), "1Y": ("4", "out"),
    "2A": ("5", "in"), "2B": ("6", "in"), "2Y": ("7", "out"), "GND": ("8", "gnd"),
    "3Y": ("9", "out"), "3B": ("10", "in"), "3A": ("11", "in"), "4Y": ("12", "out"),
    "4B": ("13", "in"), "4A": ("14", "in"), "-E": ("15", "in"), "VCC": ("16", "pwr")})
D("74HCT151", "DIP16", {
    "D3": ("1", "in"), "D2": ("2", "in"), "D1": ("3", "in"), "D0": ("4", "in"),
    "Y": ("5", "out"), "-W": ("6", "out"), "-E": ("7", "in"), "GND": ("8", "gnd"),
    "S2": ("9", "in"), "S1": ("10", "in"), "S0": ("11", "in"), "D7": ("12", "in"),
    "D6": ("13", "in"), "D5": ("14", "in"), "D4": ("15", "in"), "VCC": ("16", "pwr")})
D("74HCT153", "DIP16", {
    "-1E": ("1", "in"), "S1": ("2", "in"), "1I3": ("3", "in"), "1I2": ("4", "in"),
    "1I1": ("5", "in"), "1I0": ("6", "in"), "1Y": ("7", "out"), "GND": ("8", "gnd"),
    "2Y": ("9", "out"), "2I0": ("10", "in"), "2I1": ("11", "in"), "2I2": ("12", "in"),
    "2I3": ("13", "in"), "S0": ("14", "in"), "-2E": ("15", "in"), "VCC": ("16", "pwr")})
D("74HCT14", "DIP14", {
    "1A": ("1", "in"), "1Y": ("2", "out"), "2A": ("3", "in"), "2Y": ("4", "out"),
    "3A": ("5", "in"), "3Y": ("6", "out"), "GND": ("7", "gnd"), "4Y": ("8", "out"),
    "4A": ("9", "in"), "5Y": ("10", "out"), "5A": ("11", "in"), "6Y": ("12", "out"),
    "6A": ("13", "in"), "VCC": ("14", "pwr")})                # hex Schmitt inverter
D("74HCT27", "DIP14", {
    "1A": ("1", "in"), "1B": ("2", "in"), "2A": ("3", "in"), "2B": ("4", "in"),
    "2C": ("5", "in"), "2Y": ("6", "out"), "GND": ("7", "gnd"), "3Y": ("8", "out"),
    "3A": ("9", "in"), "3B": ("10", "in"), "3C": ("11", "in"), "1Y": ("12", "out"),
    "1C": ("13", "in"), "VCC": ("14", "pwr")})                # triple 3-input NOR
D("74HCT4002", "DIP14", {
    "1Y": ("1", "out"), "1A": ("2", "in"), "1B": ("3", "in"), "1C": ("4", "in"),
    "1D": ("5", "in"), "NC6": ("6", "pas"), "GND": ("7", "gnd"), "NC8": ("8", "pas"),
    "2A": ("9", "in"), "2B": ("10", "in"), "2C": ("11", "in"), "2D": ("12", "in"),
    "2Y": ("13", "out"), "VCC": ("14", "pwr")})               # dual 4-input NOR
D("SST39SF010A", "DIP32-600", dict(                         # 128K x 8 flash, 55 ns (D18)
    [("A%d" % a, (n, "in")) for a, n in ((16, "2"), (15, "3"), (12, "4"), (7, "5"), (6, "6"), (5, "7"),
                                         (4, "8"), (3, "9"), (2, "10"), (1, "11"), (0, "12"), (10, "23"),
                                         (11, "25"), (9, "26"), (8, "27"), (13, "28"), (14, "29"))] +
    [("DQ%d" % i, (n, "tri")) for i, n in enumerate(("13", "14", "15", "17", "18", "19", "20", "21"))] +
    [("NC1", ("1", "pas")), ("NC30", ("30", "pas")), ("GND", ("16", "gnd")), ("-CE", ("22", "in")),
     ("-OE", ("24", "in")), ("-WE", ("31", "in")), ("VCC", ("32", "pwr"))]), value="SST39SF010A-55")
D("74HCT00", "DIP14", _gates(2, 1))                     # quad NAND
D("SST39SF020A", "DIP32-600", dict(                         # 256K x 8 flash, 70 ns PDIP: boot ROM (D50)
    [(k, v) for k, v in DEV["SST39SF010A"]["pins"].items() if k != "NC30"] + [("A17", ("30", "in"))]),
    value="SST39SF020A-70")
D("AS6C4008", "DIP32-600", dict(                            # 512K x 8 SRAM, 55 ns, 5 V (D50)
    [("A%d" % a, (n, "in")) for a, n in ((18, "1"), (16, "2"), (14, "3"), (12, "4"), (7, "5"), (6, "6"),
                                         (5, "7"), (4, "8"), (3, "9"), (2, "10"), (1, "11"), (0, "12"),
                                         (10, "23"), (11, "25"), (9, "26"), (8, "27"), (13, "28"),
                                         (17, "30"), (15, "31"))] +
    [("DQ%d" % i, (n, "bidir")) for i, n in enumerate(("13", "14", "15", "17", "18", "19", "20", "21"))] +
    [("GND", ("16", "gnd")), ("-CE", ("22", "in")), ("-OE", ("24", "in")), ("-WE", ("29", "in")),
     ("VCC", ("32", "pwr"))]), value="AS6C4008-55PCN")
D("74HC05", "DIP14", dict(                                  # hex inverter, open-drain outputs
    [("%dA" % g, (a, "in")) for g, a in ((1, "1"), (2, "3"), (3, "5"), (4, "9"), (5, "11"), (6, "13"))] +
    [("%dY" % g, (y, "oc")) for g, y in ((1, "2"), (2, "4"), (3, "6"), (4, "8"), (5, "10"), (6, "12"))] +
    [("GND", ("7", "gnd")), ("VCC", ("14", "pwr"))]))
D("JUMPER2", "PINHDR_1X2", {"1": ("1", "pas"), "2": ("2", "pas")}, value="jumper")
D("74HCT163", "DIP16", dict(DEV["74HCT161"]["pins"]))       # as 161, synchronous clear (I/O timer)
D("74HCT30", "DIP14", dict(                                 # 8-input NAND
    [(n, (p, "in")) for n, p in zip("ABCDEFGH", ("1", "2", "3", "4", "5", "6", "11", "12"))] +
    [("Y", ("8", "out")), ("GND", ("7", "gnd")), ("VCC", ("14", "pwr")),
     ("NC9", ("9", "pas")), ("NC10", ("10", "pas")), ("NC13", ("13", "pas"))]))
D("TL16C550", "DIP40-600", dict(                            # UART with 16-byte FIFOs, 5 V (D50)
    [("D%d" % i, (str(i + 1), "bidir")) for i in range(8)] +
    [("RCLK", ("9", "in")), ("SIN", ("10", "in")), ("SOUT", ("11", "out")), ("CS0", ("12", "in")),
     ("CS1", ("13", "in")), ("-CS2", ("14", "in")), ("-BAUDOUT", ("15", "out")), ("XIN", ("16", "in")),
     ("XOUT", ("17", "out")), ("-WR1", ("18", "in")), ("WR2", ("19", "in")), ("GND", ("20", "gnd")),
     ("RD2", ("21", "in")), ("-RD1", ("22", "in")), ("DDIS", ("23", "out")), ("-TXRDY", ("24", "out")),
     ("-ADS", ("25", "in")), ("A2", ("26", "in")), ("A1", ("27", "in")), ("A0", ("28", "in")),
     ("-RXRDY", ("29", "out")), ("INTRPT", ("30", "out")), ("-OUT2", ("31", "out")), ("-RTS", ("32", "out")),
     ("-DTR", ("33", "out")), ("-OUT1", ("34", "out")), ("MR", ("35", "in")), ("-CTS", ("36", "in")),
     ("-DCD", ("37", "in")), ("-DSR", ("38", "in")), ("-RI", ("39", "in")), ("VCC", ("40", "pwr"))]),
    value="TL16C550CN")
D("OSC1M8432", "OSC_DIP14", {"EN": ("1", "in"), "GND": ("7", "gnd"), "OUT": ("8", "out"), "VCC": ("14", "pwr")},
  value="1.8432 MHz oscillator")
D("RNET9", "SIP9", dict([("COM", ("1", "pas"))] + [("R%d" % i, (str(i + 1), "pas")) for i in range(1, 9)]),
  value="10k x 8 bussed")
D("IDC40", "IDC_2X20", {str(n): (str(n), "pas") for n in range(1, 41)}, value="IDC 2x20 header")
D("IDC20", "IDC_2X10", {str(n): (str(n), "pas") for n in range(1, 21)}, value="IDC 2x10 box header")
D("IDC10", "IDC_2X5", {str(n): (str(n), "pas") for n in range(1, 11)}, value="IDC 2x5 box header")
D("CD74HC4050", "DIP16", dict(                          # hex buffer at 3.3 V whose inputs take 5 V (or more):
    [("%dA" % g, (a, "in")) for g, a in ((1, "3"), (2, "5"), (3, "7"), (4, "9"), (5, "11"), (6, "14"))] +   # 5 V -> 3.3 V
    [("%dY" % g, (y, "out")) for g, y in ((1, "2"), (2, "4"), (3, "6"), (4, "10"), (5, "12"), (6, "15"))] +
    [("VCC", ("1", "pwr3")), ("GND", ("8", "gnd")), ("NC13", ("13", "pas")), ("NC16", ("16", "pas"))]),
  value="CD74HC4050E")
D("HDR1X6", "PINHDR_1X6", {str(n): (str(n), "pas") for n in range(1, 7)}, value="1x6 header")
D("74HCT257", "DIP16", {k.replace("-E", "-OE"): (v[0], "tri" if v[1] == "out" else v[1])     # 2:1 mux, 3-state
                       for k, v in DEV["74HCT157"]["pins"].items()})
D("74HCT253", "DIP16", {k.replace("-1E", "-1OE").replace("-2E", "-2OE"): (v[0], "tri" if v[1] == "out" else v[1])
                       for k, v in DEV["74HCT153"]["pins"].items()})                          # dual 4:1 mux, 3-state
def faster(name, base):
    """A faster family's part with its 74HCT twin's pinout (D72: 74ACT157 for
    74HCT157 on the critical paths); the simulator runs it as the twin."""
    if name not in DEV:
        D(name, DEV[base]["pkg"], dict(DEV[base]["pins"]), value=name)
    return name

def _lvc(name, base):
    pins = {k: (v[0], "pwr3" if v[1] == "pwr" else v[1]) for k, v in DEV[base]["pins"].items()}
    D(name, "DIP20", pins)
# the faster parts on the critical paths (D72): each its 74HCT twin's pinout
# (the 74F521 the '688's; the 74ABT377 the '377's), defined after the twins below
FASTER = {"74ACT157": "74HCT157", "74ACT08": "74HCT08", "74ACT32": "74HCT32", "74ACT00": "74HCT00",
          "74ABT377": "74HCT377", "74F138": "74HCT138", "74F153": "74HCT153", "74F253": "74HCT253",
          "74F257": "74HCT257", "74F521": "74HCT688"}

# 74LVC: 3.3 V, every input (and an output when off) takes 5 V -- the ALU
# card's 5 V <-> 3.3 V boundary (D21: TI makes them in PDIP-20)
D("74HCT574", "DIP20", dict([("-OE", ("1", "in")), ("GND", ("10", "gnd")), ("CLK", ("11", "in")), ("VCC", ("20", "pwr"))] +
  [("D%d" % i, (str(2 + i), "in")) for i in range(8)] + [("Q%d" % i, (str(19 - i), "tri")) for i in range(8)]))
_lvc("74LVC574", "74HCT574")
_lvc("74LVC244", "74HCT244")
# real-time clock (D23): a DS12C887 module -- crystal and a 10-year lithium
# cell inside, MC146818-compatible registers; Intel bus mode (MOT = GND): AS
# latches the register number from AD0-7, DS = read strobe, R/W = write
# strobe. Pin numbers as the MC146818 / DS12887 (TO VERIFY with the datasheet).
D("DS12C887", "DIP24-600", dict(
    [("MOT", ("1", "in")), ("GND", ("12", "gnd")), ("-CS", ("13", "in")), ("AS", ("14", "in")),
     ("-WR", ("15", "in")), ("-RD", ("17", "in")), ("-RESET", ("18", "in")), ("-IRQ", ("19", "oc")),
     ("-RCLR", ("21", "in")), ("SQW", ("23", "out")), ("VCC", ("24", "pwr"))] +
    [("AD%d" % i, (str(4 + i), "bidir")) for i in range(8)] +
    [("NC%d" % p, (str(p), "pas")) for p in (2, 3, 16, 20, 22)]), value="DS12C887+")
D("LD1117V33", "TO220", {"GND": ("1", "gnd"), "OUT": ("2", "psrc"), "IN": ("3", "pas")}, value="LD1117V33")
D("74HCT688", "DIP20", dict(                                 # 8-bit identity comparator
    [("-G", ("1", "in")), ("GND", ("10", "gnd")), ("-P=Q", ("19", "out")), ("VCC", ("20", "pwr"))] +
    [("P%d" % i, (str(p), "in")) for i, p in enumerate((2, 4, 6, 8, 11, 13, 15, 17))] +
    [("Q%d" % i, (str(p), "in")) for i, p in enumerate((3, 5, 7, 9, 12, 14, 16, 18))]))
D("74HCT125", "DIP14", {                                # quad buffer, individual active-low enables
    "-1OE": ("1", "in"), "1A": ("2", "in"), "1Y": ("3", "tri"), "-2OE": ("4", "in"),
    "2A": ("5", "in"), "2Y": ("6", "tri"), "GND": ("7", "gnd"), "3Y": ("8", "tri"),
    "3A": ("9", "in"), "-3OE": ("10", "in"), "4Y": ("11", "tri"), "4A": ("12", "in"),
    "-4OE": ("13", "in"), "VCC": ("14", "pwr")})
D("74HCT148", "DIP16", {                                # 8-to-3 priority encoder, active-low
    "-I4": ("1", "in"), "-I5": ("2", "in"), "-I6": ("3", "in"), "-I7": ("4", "in"),
    "-EI": ("5", "in"), "-A2": ("6", "out"), "-A1": ("7", "out"), "GND": ("8", "gnd"),
    "-A0": ("9", "out"), "-I0": ("10", "in"), "-I1": ("11", "in"), "-I2": ("12", "in"),
    "-I3": ("13", "in"), "-GS": ("14", "out"), "-EO": ("15", "out"), "VCC": ("16", "pwr")})
# the control-store flash in a PLCC-32 socket: same pin numbers as the DIP-32 part
D("SST39SF010A_PLCC", "PLCC32_SOCKET", dict(DEV["SST39SF010A"]["pins"]), value="SST39SF010A-55 (PLCC-32)")
D("MCP23S17", "DIP28", dict(                             # 16-bit SPI I/O expander (D50)
    [("GPB%d" % i, (str(1 + i), "bidir")) for i in range(8)] +
    [("GPA%d" % i, (str(21 + i), "bidir")) for i in range(8)] +
    [("VDD", ("9", "pwr")), ("VSS", ("10", "gnd")), ("-CS", ("11", "in")), ("SCK", ("12", "in")),
     ("SI", ("13", "in")), ("SO", ("14", "tri")), ("A0", ("15", "in")), ("A1", ("16", "in")),
     ("A2", ("17", "in")), ("-RESET", ("18", "in")), ("INTB", ("19", "out")), ("INTA", ("20", "out"))]),
  value="MCP23S17-E/SP")
_PICO_GND = (3, 8, 13, 18, 23, 28, 33, 38)
_PICO_GP = dict(zip([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 26, 27, 28],
                    [1, 2, 4, 5, 6, 7, 9, 10, 11, 12, 14, 15, 16, 17, 19, 20, 21, 22, 24, 25, 26, 27, 29, 31, 32, 34]))
D("PICO", "PICO_THT", dict(
    [("GP%d" % g, (str(n), "bidir")) for g, n in _PICO_GP.items()] +
    [("GND%d" % n, (str(n), "gnd")) for n in _PICO_GND] +
    [("RUN", ("30", "pas")), ("ADC_VREF", ("35", "pas")), ("3V3", ("36", "psrc")), ("3V3_EN", ("37", "pas")),
     ("VSYS", ("39", "pas")), ("VBUS", ("40", "pas"))]), value="Raspberry Pi Pico")
D("D_SCHOTTKY", "DO41", {"K": ("1", "pas"), "A": ("2", "pas")}, value="1N5817")
D("OSC15M", "OSC_DIP14", {"EN": ("1", "in"), "GND": ("7", "gnd"), "OUT": ("8", "out"), "VCC": ("14", "pwr")},
  value="15 MHz oscillator")
D("IDC50", "IDC_2X25", {str(n): (str(n), "pas") for n in range(1, 51)}, value="IDC 2x25 header")
D("SW_PUSH", "SW_PUSH_6MM", {"1": ("1", "pas"), "2": ("2", "pas")}, value="push button")
D("SW_DIP2", "SW_DIP_2", {"1": ("1", "pas"), "2": ("2", "pas"), "3": ("3", "pas"), "4": ("4", "pas")},
  value="2-way DIP switch")

def gal_device(g):
    """A device for one GAL definition: pin names are its signals (D17)."""
    pins = {"GND": ("12", "gnd"), "VCC": ("24", "pwr")}
    for n, pin in g.inputs.items():
        pins[n] = (str(pin), "in")
    for o in g.outs.values():
        pins[o.name] = (str(o.pin), "tri" if o.kind == "tri" else "out")
    for pin in list(range(1, 12)) + [13] + list(range(14, 24)):
        if str(pin) not in {v[0] for v in pins.values()}:
            pins["NC%d" % pin] = (str(pin), "pas")
    name = "GAL_" + g.name
    assert name not in DEV or DEV[name]["pins"] == pins, "two different GALs named %s" % g.name
    D(name, "DIP24", pins, value="ATF22V10C-10 (%s)" % g.name)
    return name

D("DIN96", "DIN41612-C96-R", {"%s%d" % (r, n): ("%s%d" % (r.lower(), n), "pas")   # pad numbers as in the
      for r in "ABC" for n in range(1, 33)},                                        # KiCad footprint: a1..c32
  value="DIN 41612 96-pin male, right angle")
D("C100N", "C_DISC", {"1": ("1", "pas"), "2": ("2", "pas")}, value="100nF")
# the backplane's own parts (D49; card_backplane.py, its schematic)
D("DIN96F", "DIN41612-C96-F", dict(DEV["DIN96"]["pins"]), value="DIN41612 C 3x32 F")
D("C1000U", "CP_RADIAL_10", {"+": ("1", "pas"), "-": ("2", "pas")}, value="1000uF 16V")
D("TERM2", "MSTB_2", {"1": ("1", "pas"), "2": ("2", "pas")}, value="5 V in")
# ATX 24-pin power (D68): the board header, Molex Mini-Fit Jr 5566-24A (pads 1-12 one row, 13-24 the other, as ATX numbers them)
D("ATX24", "ATX24_V", {str(n): (str(n), "pas") for n in range(1, 25)}, value="ATX 24-pin (Molex 5566-24A)")
D("MH_M3", "MH_M3_PAD", {"1": ("1", "pas")}, value="M3 mounting hole")
D("C10U", "CP_RADIAL", {"+": ("1", "pas"), "-": ("2", "pas")}, value="10uF 16V")
D("R", "R_AXIAL", {"1": ("1", "pas"), "2": ("2", "pas")})
D("LED", "LED_3MM", {"A": ("1", "pas"), "K": ("2", "pas")}, value="LED 3mm")
# bus termination parts: an RC per line and Schottky clamps
D("RNET16", "DIP16", dict([("R%dA" % i, (str(i), "pas")) for i in range(1, 9)] +          # 8 isolated resistors:
                          [("R%dB" % i, (str(17 - i), "pas")) for i in range(1, 9)]),     # R1 is pins 1-16, ...
  value="100R x 8 isolated, DIP-16")
D("C150P", "C_DISC_P254", {"1": ("1", "pas"), "2": ("2", "pas")}, value="150pF")
D("BAT85", "DO35", {"K": ("1", "pas"), "A": ("2", "pas")}, value="BAT85")
D("SN74S1053", "DIP20", dict(                           # 16 Schottky clamps: each D a diode to VCC and from GND
    [("D%02d" % i, (str(1 + i), "pas")) for i in range(1, 9)] +                # D01-D08: pins 2-9
    [("D%02d" % i, (str(3 + i), "pas")) for i in range(9, 17)] +               # D09-D16: pins 12-19 (TI SDLS017A)
    [("VCC", ("1", "pwr")), ("VCC2", ("20", "pwr")), ("GND", ("10", "gnd")), ("GND2", ("11", "gnd"))]),
  value="SN74S1053N")
# the I/O card (D43, D50): the CH376S module's 16-pin header plugs
# into a 2x8 socket; RUN's capacitor stretches the coprocessor's RESET pulse
D("SOCK2X8", "PINSOCK_2X8", {str(n): (str(n), "pas") for n in range(1, 17)}, value="2x8 socket (CH376S module)")
D("C2N2", "C_DISC_P254", {"1": ("1", "pas"), "2": ("2", "pas")}, value="2.2nF")
# WIZnet WIZ850io (W5500 + RJ45) in two 1x6 sockets (gen_footprints.py): pads
# 1-6 its J1, 7-12 its J2 (docs.wiznet.io/Product/ioModule/WIZ850io)
D("WIZ850IO", "WIZ850IO_SOCKET", {"GND1": ("1", "pas"), "GND2": ("2", "pas"), "MOSI": ("3", "in"), "SCLK": ("4", "in"),
                                  "SCNn": ("5", "in"), "INTn": ("6", "out"), "GND7": ("7", "pas"), "3V3A": ("8", "pas"),
                                  "3V3B": ("9", "pas"), "NC10": ("10", "pas"), "RSTn": ("11", "in"), "MISO": ("12", "tri")},
  value="WIZ850io")

# the I/O card's interrupt levels (D64): BCD-to-decimal decoder, open-collector outputs
D("74LS145", "DIP16", dict(
    [("O%d" % k, (str(p), "oc")) for k, p in enumerate((1, 2, 3, 4, 5, 6, 7, 9, 10, 11))] +
    [("A", ("15", "in")), ("B", ("14", "in")), ("C", ("13", "in")), ("D", ("12", "in")),
     ("GND", ("8", "gnd")), ("VCC", ("16", "pwr"))]), value="SN74LS145N")
# the I/O card's serial ports (D50): RS-232 transceivers and DB9 plugs at the front edge
D("MAX232A", "DIP16", {"C1+": ("1", "pas"), "V+": ("2", "pas"), "C1-": ("3", "pas"), "C2+": ("4", "pas"),
                       "C2-": ("5", "pas"), "V-": ("6", "pas"), "T2OUT": ("7", "out"), "R2IN": ("8", "in"),
                       "R2OUT": ("9", "out"), "T2IN": ("10", "in"), "T1IN": ("11", "in"), "R1OUT": ("12", "out"),
                       "R1IN": ("13", "in"), "T1OUT": ("14", "out"), "GND": ("15", "gnd"), "VCC": ("16", "pwr")},
  value="MAX232A")
D("DB9M_RA", "DSUB9_M_RA", dict([(str(n), (str(n), "pas")) for n in range(1, 10)] + [("SHELL", ("0", "pas"))]),
  value="DB9 male, right angle")
# the disk card (D66): a right-angle 40-pin header at the front edge, for a CF-to-IDE adapter
D("HDR2X20_RA", "PINHDR_2X20_RA", {str(n): (str(n), "pas") for n in range(1, 41)}, value="2x20 header, right angle")
# the debug card (D64, docs/debug-card.md)
D("74HCT154", "DIP24-600", dict(                       # 4-to-16 decoder, active-low outputs
    [("Y%d" % k, (str(p), "out")) for k, p in enumerate((1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 14, 15, 16, 17))] +
    [("GND", ("12", "gnd")), ("-G1", ("18", "in")), ("-G2", ("19", "in")), ("D", ("20", "in")), ("C", ("21", "in")),
     ("B", ("22", "in")), ("A", ("23", "in")), ("VCC", ("24", "pwr"))]))
D("74HCT74", "DIP14", {                                # dual D flip-flop, preset and clear
    "-1CLR": ("1", "in"), "1D": ("2", "in"), "1CLK": ("3", "in"), "-1PRE": ("4", "in"), "1Q": ("5", "out"),
    "-1Q": ("6", "out"), "GND": ("7", "gnd"), "-2Q": ("8", "out"), "2Q": ("9", "out"), "-2PRE": ("10", "in"),
    "2CLK": ("11", "in"), "2D": ("12", "in"), "-2CLR": ("13", "in"), "VCC": ("14", "pwr")})
D("74HCT273", "DIP20", {                               # octal D flip-flop, master reset (the control registers)
    "-MR": ("1", "in"), "Q0": ("2", "out"), "D0": ("3", "in"), "D1": ("4", "in"), "Q1": ("5", "out"),
    "Q2": ("6", "out"), "D2": ("7", "in"), "D3": ("8", "in"), "Q3": ("9", "out"), "GND": ("10", "gnd"),
    "CLK": ("11", "in"), "Q4": ("12", "out"), "D4": ("13", "in"), "D5": ("14", "in"), "Q5": ("15", "out"),
    "Q6": ("16", "out"), "D6": ("17", "in"), "D7": ("18", "in"), "Q7": ("19", "out"), "VCC": ("20", "pwr")})
D("74F161", "DIP16", dict(DEV["74HCT161"]["pins"]), value="74F161")           # (24 MHz: the capture counters)
D("74ACT574", "DIP20", dict(DEV["74HCT574"]["pins"]), value="74ACT574")       # (24 MHz: the capture latches)
_lvc("74LVC245", "74HCT245")
D("SW_SPDT_RA", "SW_SPDT_RA", {"1": ("1", "pas"), "2": ("2", "pas"), "3": ("3", "pas")}, value="SPDT toggle, right angle")
# the CPU card (docs/cpu-card.md, D58, D59)
D("74HCT85", "DIP16", {                                 # 4-bit magnitude comparator, cascadable
    "B3": ("1", "in"), "IA<B": ("2", "in"), "IA=B": ("3", "in"), "IA>B": ("4", "in"),
    "QA>B": ("5", "out"), "QA=B": ("6", "out"), "QA<B": ("7", "out"), "GND": ("8", "gnd"),
    "B0": ("9", "in"), "A0": ("10", "in"), "B1": ("11", "in"), "A1": ("12", "in"),
    "A2": ("13", "in"), "B2": ("14", "in"), "A3": ("15", "in"), "VCC": ("16", "pwr")})
D("74F283", "DIP16", {                                  # 4-bit binary full adder, fast carry (the address adder)
    "S2": ("1", "out"), "B2": ("2", "in"), "A2": ("3", "in"), "S1": ("4", "out"),
    "A1": ("5", "in"), "B1": ("6", "in"), "C0": ("7", "in"), "GND": ("8", "gnd"),
    "C4": ("9", "out"), "S4": ("10", "out"), "B4": ("11", "in"), "A4": ("12", "in"),
    "S3": ("13", "out"), "A3": ("14", "in"), "B3": ("15", "in"), "VCC": ("16", "pwr")}, value="SN74F283N")
D("OSC24M", "OSC_DIP14", {"EN": ("1", "in"), "GND": ("7", "gnd"), "OUT": ("8", "out"), "VCC": ("14", "pwr")},
  value="24 MHz oscillator")
# The ALU (D21): a Sipeed Tang Nano 20K in two 1x20 sockets, powered from 5 V.
# Its interface (docs/cpu-card.md): the 16-bit FD bus to the operand and
# result latches, the function and a GO toggle in, the latch selects and the
# result clocks out. The pads are the module's header pins as Sipeed's
# datasheet (v1.3, "Pinout") shows them from the top, USB-C up: pads 1-20
# the left row from the USB end, 21-40 the right row (D70). Each signal's
# GW2AR-18 pin is in NANO_GPIO, for the FPGA's constraints file. The FPGA
# pins that also reach parts on the module avoid the BL616's high-speed SPI
# where they can; the latch selects and result clocks are on the six LED
# pins (their LEDs show the ALU working).
_NANO_PADS = {
    1: ("GO", 73), 2: ("RES_N", 74), 3: ("IO75", 75), 4: ("FD0", 85), 5: ("FD1", 77),
    6: ("SELAL_N", 15), 7: ("SELAH_N", 16), 8: ("FD2", 27), 9: ("FD3", 28), 10: ("FD4", 25),
    11: ("FD5", 26), 12: ("FD6", 29), 13: ("FD7", 30), 14: ("FD8", 31), 15: ("SELBL_N", 17),
    16: ("SELBH_N", 20), 17: ("CKLO", 19), 18: ("CKHI", 18), 19: ("3V3", None), 20: ("GND20", None),
    21: ("5V", None), 22: ("GND22", None), 23: ("IO76", 76), 24: ("FD9", 80), 25: ("FD10", 42),
    26: ("FD11", 41), 27: ("ALUF0", 56), 28: ("ALUF1", 54), 29: ("ALUF3", 51), 30: ("FD12", 48),
    31: ("ALUF2", 55), 32: ("FD13", 49), 33: ("IO86", 86), 34: ("ALUF4", 79), 35: ("GND35", None),
    36: ("3V3B", None), 37: ("IO72", 72), 38: ("IO71", 71), 39: ("FD14", 53), 40: ("FD15", 52)}
NANO_GPIO = {n: g for n, g in _NANO_PADS.values() if g is not None}
# what else each of those FPGA pins reaches on the module (the datasheet's "onboard component")
NANO_SHARED = {73: "HSPI_DIN2 (BL616)", 74: "HSPI_DIN3 (BL616)", 75: "HSPI_DIR", 85: "SDIO_D1 (microSD)",
               77: "LCD_CLK", 15: "LED0", 16: "LED1", 27: "LCD_B7", 28: "LCD_B6", 25: "LCD_HS", 26: "LCD_VS",
               29: "LCD_B5", 30: "LCD_B4", 31: "LCD_B3", 17: "LED2", 20: "LED5", 19: "LED4", 18: "LED3",
               76: "HSPI_DAT", 80: "SDIO_D2 (microSD)", 42: "LCD_R3", 41: "LCD_R4", 56: "I2S_BCLK (audio amp)",
               54: "I2S_DIN (audio amp)", 51: "PA_EN (audio amp)", 48: "LCD_DE", 55: "I2S_LRCK (audio amp)",
               49: "LCD_BL", 86: "HSPI_CSN", 79: "WS2812 DIN", 72: "HSPI_DIN1", 71: "HSPI_DIN0",
               53: "EDID_CLK (HDMI)", 52: "EDID_DAT (HDMI)"}
_NT = {"5V": "pwr", "GND20": "gnd", "GND22": "gnd", "GND35": "gnd", "3V3": "pas", "3V3B": "pas", "GO": "in",
       "RES_N": "in", "SELAL_N": "out", "SELAH_N": "out", "SELBL_N": "out", "SELBH_N": "out", "CKLO": "out",
       "CKHI": "out"}
D("TANG_NANO_20K", "NANO20K_SOCKET", {n: (str(pad), _NT.get(n, "bidir" if n.startswith("FD") else
                                            "in" if n.startswith("ALUF") else "pas"))
                                      for pad, (n, g) in _NANO_PADS.items()}, value="Sipeed Tang Nano 20K")

# the video card (D67, docs/video-card.md)
D("74F163", "DIP16", dict(DEV["74HCT161"]["pins"]), value="74F163")           # (25 MHz: the dot, line and frame counters)
D("74ACT541", "DIP20", dict(DEV["74HCT541"]["pins"]), value="74ACT541")       # (the video RAM's address drivers, the VGA drivers)
D("74ACT04", "DIP14", dict(DEV["74HCT14"]["pins"]), value="74ACT04")          # hex inverter (the dot clock)
D("74F151", "DIP16", dict(DEV["74HCT151"]["pins"]), value="74F151")           # 8:1 mux (a character's dots)
D("74HCT595", "DIP16", dict(                            # shift register, 3-state output register (PS/2 receive)
    [("Q%s" % k, (str(p), "tri")) for k, p in zip("BCDEFGH", range(1, 8))] +
    [("QA", ("15", "tri")), ("QH'", ("9", "out")), ("-SRCLR", ("10", "in")), ("SRCLK", ("11", "in")),
     ("RCLK", ("12", "in")), ("-OE", ("13", "in")), ("SER", ("14", "in")), ("GND", ("8", "gnd")), ("VCC", ("16", "pwr"))]))
D("74HCT597", "DIP16", dict(                            # input register, parallel-load shift register (PS/2 send)
    [(k, (str(p), "in")) for k, p in zip("BCDEFGH", range(1, 8))] +
    [("A", ("15", "in")), ("QH'", ("9", "out")), ("-SRCLR", ("10", "in")), ("SRCK", ("11", "in")),
     ("RCK", ("12", "in")), ("-SRLOAD", ("13", "in")), ("SER", ("14", "in")), ("GND", ("8", "gnd")), ("VCC", ("16", "pwr"))]))
D("74HCT175", "DIP16", {                                # quad D flip-flop, Q and -Q, master reset
    "-MR": ("1", "in"), "Q1": ("2", "out"), "-Q1": ("3", "out"), "D1": ("4", "in"), "D2": ("5", "in"),
    "-Q2": ("6", "out"), "Q2": ("7", "out"), "GND": ("8", "gnd"), "CP": ("9", "in"), "Q3": ("10", "out"),
    "-Q3": ("11", "out"), "D3": ("12", "in"), "D4": ("13", "in"), "-Q4": ("14", "out"), "Q4": ("15", "out"),
    "VCC": ("16", "pwr")})
D("74HCT393", "DIP14", {                                # dual 4-bit ripple counter (counts on the clock's fall)
    "1CP": ("1", "in"), "1MR": ("2", "in"), "1Q0": ("3", "out"), "1Q1": ("4", "out"), "1Q2": ("5", "out"),
    "1Q3": ("6", "out"), "GND": ("7", "gnd"), "2Q3": ("8", "out"), "2Q2": ("9", "out"), "2Q1": ("10", "out"),
    "2Q0": ("11", "out"), "2MR": ("12", "in"), "2CP": ("13", "in"), "VCC": ("14", "pwr")})
D("74HC280", "DIP14", {                                 # 9-bit parity generator
    "G": ("1", "in"), "H": ("2", "in"), "NC3": ("3", "pas"), "I": ("4", "in"), "SEVEN": ("5", "out"),
    "SODD": ("6", "out"), "GND": ("7", "gnd"), "A": ("8", "in"), "B": ("9", "in"), "C": ("10", "in"),
    "D": ("11", "in"), "E": ("12", "in"), "F": ("13", "in"), "VCC": ("14", "pwr")}, value="74HC280")
D("OSC25M", "OSC_DIP14", {"EN": ("1", "in"), "GND": ("7", "gnd"), "OUT": ("8", "out"), "VCC": ("14", "pwr")},
  value="25.175 MHz oscillator")
D("HD15F_RA", "DSUB15HD_F_RA", dict([(str(n), (str(n), "pas")) for n in range(1, 16)] + [("SHELL", ("0", "pas"))]),
  value="DE-15 (VGA) socket, right angle")
D("MINIDIN6F_RA", "MINIDIN6_F_RA", dict([(str(n), (str(n), "pas")) for n in range(1, 7)] + [("SHELL", ("0", "pas"))]),
  value="mini-DIN-6 socket, right angle (Same Sky MD-60SM)")
D("PTC", "PTC_RADIAL", {"1": ("1", "pas"), "2": ("2", "pas")}, value="MF-RG500 (0.5 A polyfuse)")

# KiCad library footprint for each package (schematic export and board builder)
FOOTPRINT = {
    "DIP14": "Package_DIP:DIP-14_W7.62mm",
    "DIP16": "Package_DIP:DIP-16_W7.62mm",
    "DIP20": "Package_DIP:DIP-20_W7.62mm",
    "DIP28-300": "Package_DIP:DIP-28_W7.62mm",
    "DIN41612-C96-R": "Connector_DIN:DIN41612_C_3x32_Male_Horizontal_THT",
    "C_DISC": "Capacitor_THT:C_Disc_D3.0mm_W2.0mm_P2.50mm",   # 100 nF bypass: 0.1" lead spacing (Ken's rule)
    "C_DISC_P254": "Capacitor_THT:C_Disc_D3.0mm_W2.0mm_P2.50mm",   # (Ken's 0.1" lead spacing)
    "DO35": "Diode_THT:D_DO-35_SOD27_P7.62mm_Horizontal",
    "DIN41612-C96-F": "Connector_DIN:DIN41612_C_3x32_Female_Vertical_THT",
    "CP_RADIAL_10": "Capacitor_THT:CP_Radial_D10.0mm_P5.00mm",
    "MSTB_2": "Connector_Phoenix_MSTB:PhoenixContact_MSTBA_2,5_2-G-5,08_1x02_P5.08mm_Horizontal",
    "MH_M3_PAD": "MountingHole:MountingHole_3.2mm_M3_Pad_Via",
    "ATX24_V": "Connector_Molex:Molex_Mini-Fit_Jr_5566-24A_2x12_P4.20mm_Vertical",
    "CP_RADIAL": "Capacitor_THT:CP_Radial_D5.0mm_P2.00mm",
    "R_AXIAL": "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
    "LED_3MM": "LED_THT:LED_D3.0mm",
    "DIP24": "Package_DIP:DIP-24_W7.62mm",
    "DIP32-600": "Package_DIP:DIP-32_W15.24mm",
    "PLCC32_SOCKET": "Package_LCC:PLCC-32_THT-Socket",
    "DIP28": "Package_DIP:DIP-28_W7.62mm",
    "PICO_THT": "Module:RaspberryPi_Pico_Common_THT",
    "DO41": "Diode_THT:D_DO-41_SOD81_P10.16mm_Horizontal",
    "OSC_DIP14": "Oscillator:Oscillator_DIP-14",
    "IDC_2X25": "Connector_IDC:IDC-Header_2x25_P2.54mm_Vertical",
    "SW_PUSH_6MM": "Button_Switch_THT:SW_PUSH_6mm",
    "SW_DIP_2": "Button_Switch_THT:SW_DIP_SPSTx02_Slide_9.78x7.26mm_W7.62mm_P2.54mm",
    "PINHDR_1X2": "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical",
    "PINHDR_1X6": "Connector_PinHeader_2.54mm:PinHeader_1x06_P2.54mm_Vertical",
    "DIP40-600": "Package_DIP:DIP-40_W15.24mm",
    "SIP9": "Resistor_THT:R_Array_SIP9",
    "IDC_2X20": "Connector_IDC:IDC-Header_2x20_P2.54mm_Vertical",
    "IDC_2X10": "Connector_IDC:IDC-Header_2x10_P2.54mm_Vertical",
    "IDC_2X5": "Connector_IDC:IDC-Header_2x05_P2.54mm_Vertical",
    "PINSOCK_2X8": "Connector_PinSocket_2.54mm:PinSocket_2x08_P2.54mm_Vertical",
    "TO220": "Package_TO_SOT_THT:TO-220-3_Vertical",
    "DIP24-600": "Package_DIP:DIP-24_W15.24mm",
    "NANO20K_SOCKET": "P32W:Sipeed_Tang_Nano_20K_Socket",          # hardware/lib/P32W.pretty
    "WIZ850IO_SOCKET": "P32W:WIZ850io_Socket",
    "SW_SPDT_RA": "Button_Switch_THT:SW_E-Switch_EG1224_SPDT_Angled",
    "PINHDR_2X20_RA": "Connector_PinHeader_2.54mm:PinHeader_2x20_P2.54mm_Horizontal",
    "DSUB15HD_F_RA": "Connector_Dsub:DSUB-15-HD_Socket_Horizontal_P2.29x1.90mm_EdgePinOffset3.03mm_Housed_MountingHolesOffset4.94mm",
    "MINIDIN6_F_RA": "P32W:MiniDIN-6_Female_Horizontal_SameSky_MD-60SM",   # (to add to hardware/lib/P32W.pretty)
    "PTC_RADIAL": "Fuse:Fuse_Bourns_MF-RG500",
    "DSUB9_M_RA": "Connector_Dsub:DSUB-9_Pins_Horizontal_P2.77x2.84mm_EdgePinOffset7.70mm_Housed_MountingHolesOffset9.12mm",
}

for _n, _b in FASTER.items():
    faster(_n, _b)

# ---------------------------------------------------------------- cards
CARDS = {}

class Card:
    def __init__(self, name, title):
        self.name, self.title = name, title
        self.parts = {}            # ref -> (device, function, value)
        self.nets = {}             # net -> [(ref, pin)]
        self.nc = set()            # (ref, pin) deliberately unconnected
        for j in CONNECTORS:                # J1-J3 (J3: the local link in slots 1-2, else power)
            self.part(j, "DIN96", "backplane connector " + j)

    def part(self, ref, dev, function="", value=None):
        assert ref not in self.parts, ref
        assert dev in DEV, dev
        self.parts[ref] = (dev, function, value or DEV[dev]["value"])

    def net(self, name, *pins):
        for ref, pin in pins:
            assert ref in self.parts, (name, ref)
            assert pin in DEV[self.parts[ref][0]]["pins"], (name, ref, pin)
        self.nets.setdefault(name, []).extend(pins)

    def bus(self, name, *pins):
        """A backplane signal: wired to its connector pin(s) from busnet()."""
        assert name in SIGNALS, name
        self.net(name, *pins)

    def unused(self, ref, *pins):
        for p in pins:
            self.nc.add((ref, p))

    def finish(self):
        """Wire the connectors (power, ground, used bus signals) and add one
        100 nF decoupling cap per IC plus a bulk cap (P8X card standard)."""
        for conn, pin in ALLPINS:
            n = busnet(conn, pin)
            if n in ("VCC", "GND") or n in self.nets:
                self.nets.setdefault(n, []).append((conn, pin))
            else:
                self.nc.add((conn, pin))
        # an output with no load off its own chip (e.g. a GAL register used only
        # through internal feedback) is not a net: declare the pin unused
        for net, pins in list(self.nets.items()):
            if len(pins) == 1 and net not in ("VCC", "GND"):
                (ref, pin), = pins
                if DEV[self.parts[ref][0]]["pins"][pin][1] in ("out", "tri"):
                    del self.nets[net]
                    self.nc.add((ref, pin))
        ics = [r for r, (d, f, v) in self.parts.items() if r.startswith("U")]
        def rail(u):
            return "+3V3" if any(t == "pwr3" for _, t in DEV[self.parts[u][0]]["pins"].values()) else "VCC"
        for i, u in enumerate(sorted(ics, key=lambda r: int(r[1:])), 1):
            c = "C%d" % i
            self.part(c, "C100N", "decoupling " + u)
            self.net(rail(u), (c, "1")); self.net("GND", (c, "2"))
        self.part("C%d" % (len(ics) + 1), "C10U", "bulk")
        self.net("VCC", ("C%d" % (len(ics) + 1), "+")); self.net("GND", ("C%d" % (len(ics) + 1), "-"))
        for u in ics:                       # power pins of every IC
            pins = DEV[self.parts[u][0]]["pins"]
            for p, (num, t) in pins.items():
                if t == "pwr":
                    self.net("VCC", (u, p))
                elif t == "pwr3":
                    self.net("+3V3", (u, p))
                elif t == "gnd":
                    self.net("GND", (u, p))
        return self

# ---------------------------------------------------------------- checks
DRIVERS = {"out", "tri", "bidir", "oc", "psrc"}

def check(card):
    errs = []
    where = {}
    for net, pins in card.nets.items():
        for rp in pins:
            if rp in where:
                errs.append("%s %s on nets %s and %s" % (rp[0], rp[1], where[rp], net))
            where[rp] = net
    for ref, (dev, f, v) in card.parts.items():         # every pin connected or declared NC
        for p in DEV[dev]["pins"]:
            if (ref, p) not in where and (ref, p) not in card.nc:
                errs.append("%s pin %s (%s) is neither connected nor declared unused" % (ref, p, dev))
            if (ref, p) in where and (ref, p) in card.nc:
                errs.append("%s pin %s is connected but declared unused" % (ref, p))
    for (ref, p) in card.nc:                            # unused inputs must not float
        if ref in card.parts and DEV[card.parts[ref][0]]["pins"][p][1] == "in":
            errs.append("%s input %s left floating" % (ref, p))
    for net, pins in card.nets.items():
        types = [DEV[card.parts[r][0]]["pins"][p][1] for r, p in pins]
        if len(pins) < 2 and types != ["out"]:                # an output may drive nothing off its chip
            errs.append("net %s has only one connection" % net)
        if types.count("out") > 1 or ("out" in types and ({"tri", "oc"} & set(types))):
            errs.append("net %s: more than one totem-pole driver" % net)
        is_bus = any(card.parts[r][0] in ("DIN96", "IDC50", "IDC40", "IDC20", "IDC10", "HDR1X6", "SOCK2X8", "DB9M_RA", "HDR2X20_RA", "SW_SPDT_RA", "MINIDIN6F_RA") for r, p in pins)   # driven from off the card
        is_bus = is_bus or any(card.parts[r][0] in ("R", "RNET9") for r, p in pins)               # a pull-up / pull-down
        if "in" in types and not (DRIVERS & set(types)) and net not in ("VCC", "GND", "+3V3") and not is_bus:
            errs.append("net %s: inputs with no driver" % net)
        if ("pwr" in types and net != "VCC") or ("gnd" in types and net != "GND") or \
           ("pwr3" in types and net != "+3V3"):
            errs.append("net %s: a power pin on a signal net" % net)
        for r, p in pins:                               # bus pins match busnet()
            if r in CONNECTORS and busnet(r, p) != net:
                errs.append("net %s on %s %s, but busnet says %s" % (net, r, p, busnet(r, p)))
    # a buffer channel whose output is on its own input's net drives itself
    # (and something else drives that input): a naming slip, e.g. a local net
    # called like the bus signal the buffer drives. Checked per channel.
    where_net = {rp: net for net, pins in card.nets.items() for rp in pins}
    PAIRS = {"74HCT541": [("A%d" % i, "Y%d" % i) for i in range(8)],
             "74HCT540": [("A%d" % i, "Y%d" % i) for i in range(8)],
             "74HCT244": [("%dA%d" % (h, i), "%dY%d" % (h, i)) for h in (1, 2) for i in range(1, 5)],
             "74LVC244": [("%dA%d" % (h, i), "%dY%d" % (h, i)) for h in (1, 2) for i in range(1, 5)],
             "74HCT125": [("%dA" % k, "%dY" % k) for k in range(1, 5)],
             "CD74HC4050": [("%dA" % k, "%dY" % k) for k in range(1, 7)]}
    for ref, (dev, f, v) in card.parts.items():
        for a, y in PAIRS.get(dev, []):
            na, ny = where_net.get((ref, a)), where_net.get((ref, y))
            if na is not None and na == ny:
                errs.append("net %s: %s channel %s -> %s drives its own input" % (na, ref, a, y))
    ics = [r for r in card.parts if r.startswith("U")]
    caps = [f for r, (d, f, v) in card.parts.items() if f.startswith("decoupling ")]
    if len(caps) != len(ics):
        errs.append("%d ICs but %d decoupling caps" % (len(ics), len(caps)))
    return errs

def bom(card):
    rows = {}
    for ref, (dev, f, v) in card.parts.items():
        rows.setdefault((v, DEV[dev]["pkg"]), []).append(ref)
    return sorted(rows.items(), key=lambda kv: kv[1][0])
