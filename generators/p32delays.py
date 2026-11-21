#!/usr/bin/env python3
"""Every chip's switching times for the timing analysis (D70, p32timing.py).

Figures are the datasheets' MAXIMUM over temperature (-40..+85 °C for HC,
HCT, ACT and LVC; 0..70 °C for 74F; 25 °C where that is all the datasheet
gives), at VCC = 4.5 V (3.0 V for LVC) and CL = 50 pF; setup and hold are
the datasheets' minimum requirements over the same range. Each part's
source is in SOURCE. Units: ns.

Keys a part may have (p32timing.model reads them):
  tpd   data in -> out            sel   select / address -> out
  en    output enable -> out (tPZx; disable is never later here)
  tco   clock -> Q                tsu   data setup to the clock
  tsu_e / tsu_en / tsu_ld / tsu_clr   enable / count-enable / load / sync-clear setup
  clr   asynchronous clear / preset -> Q
  tco_rco, ent_rco   a counter's clock -> RCO and ENT -> RCO
  tco_sr, tsu_rclk   the 595: shift clock -> QH', shift-to-storage clock setup
  load  the 597: parallel load -> QH'
  casc  the 85's cascade inputs -> outputs
  c0    the 283's carry in -> sums and C4
  memories: tAA tACE tDOE (read), tSD tAW tSCE (write, at -WE's rise)
  GALs: tpd tco tsu tcf (clock to feedback) tea (input to output enable)
"""

T = {}
SOURCE = {}

def part(name, src, **k):
    T[name] = k
    SOURCE[name] = src

TI = "https://www.ti.com/lit/ds/symlink/%s.pdf"

# ---------------------------------------------------------------- 74F, 74ACT, 74LVC, LS (TI / onsemi)
part("74F283", TI % "sn74f283", tpd=10.5, c0=10.5)
part("74F151", TI % "sn74f151b", tpd=7.5, sel=12.0, en=10.5)
part("74F161", TI % "sn74f161a", tco=11.0, tsu=5.0, tsu_en=11.5, tsu_ld=11.5, tsu_clr=11.5, clr=13.0,
     tco_rco=15.0, ent_rco=8.5)
part("74F163", TI % "sn74f163a", tco=11.0, tsu=5.0, tsu_en=11.5, tsu_ld=11.5, tsu_clr=11.5, clr=13.0,
     tco_rco=15.0, ent_rco=8.5)
part("74ACT04", TI % "sn74act04", tpd=9.0)
part("74ACT541", "https://www.onsemi.com/download/data-sheet/pdf/74act541-d.pdf", tpd=7.5, en=9.5)
part("74ACT574", TI % "sn74act574", tco=12.0, tsu=2.5, en=10.0)
part("74LVC244", TI % "sn74lvc244a", tpd=5.9, en=7.6)
part("74LVC245", TI % "sn74lvc245a", tpd=6.3, en=8.5)
part("74LVC574", TI % "sn74lvc574a", tco=7.0, tsu=2.0, en=7.5)
part("74LS145", TI % "sn74ls145" + " (25 °C max only, 50; x1.2 for temperature)", tpd=60.0)
part("CD74HC4050", TI % "cd74hc4050", tpd=21.0)

# ---------------------------------------------------------------- the faster parts on the critical paths (D72)
part("74ACT157", TI % "cd74act157", tpd=8.6, sel=13.2, en=12.3)
part("74ACT08", TI % "sn74act08", tpd=10.0)
part("74ACT32", TI % "sn74act32", tpd=10.0)
part("74ACT00", TI % "sn74act00", tpd=9.5)
part("74ABT377", TI % "sn74abt377a" + " (BiCMOS, TTL-level outputs: no ACT / F 377 is made in DIP)",
     tco=7.3, tsu=2.5, tsu_e=3.0)
part("74F138", TI % "sn74f138", sel=9.0, en=9.0)
part("74F153", TI % "sn74f153", tpd=8.0, sel=12.0, en=10.5)
part("74F253", TI % "sn74f253", tpd=8.0, sel=13.0, en=9.0)
part("74F257", TI % "sn74f257", tpd=7.0, sel=15.0, en=8.5)
part("74ACT74", TI % "sn74act74", tco=13.0, tsu=3.5, clr=11.5)
part("74F521", TI % "sn74f521" + " (the '688's pinout)", tpd=11.0, en=10.0)


CD = lambda n: TI % ("cd74hct%s" % n)
SN = lambda n: TI % ("sn74hct%s" % n)
part("74HCT00", SN("00"), tpd=25.0)
part("74HCT08", SN("08"), tpd=30.0)
part("74HCT32", SN("32"), tpd=30.0)
part("74HCT14", SN("14"), tpd=40.0)
part("74HCT4002", "https://assets.nexperia.com/documents/data-sheet/74HC_HCT4002.pdf", tpd=28.0)
part("74HC05", TI % "sn74hc05" + " (open drain: pulling down, tPHL 21; its release rises through the pull-up)", tpd=21.0)
# (D128) open collector, TTL input thresholds: pulling down, tPHL 23 at 25 °C, 5 V, into 15 pF and 110 ohms
# (TI SDLS031A gives no figure over temperature: x1.2, as the 74LS145); its release rises through the pull-up
part("7406", TI % "sn7406" + " (open collector: pulling down, tPHL 23 at 25 °C and 15 pF; x1.2 for temperature)",
     tpd=27.6)
part("74HCT125", SN("125"), tpd=25.0, en=35.0)
part("74HCT138", SN("138"), sel=45.0, en=42.0)
part("74HCT148", TI % "sn74hc148" + " (no HCT datasheet found: the HC part's figures)", tpd=49.0)
part("74HCT153", CD("153"), tpd=43.0, sel=43.0, en=34.0)
part("74HCT154", CD("154"), sel=44.0, en=43.0)
part("74HCT157", SN("157"), tpd=35.0, sel=40.0, en=33.0)
part("74HCT161", CD("161"), tco=49.0, tsu=13.0, tsu_en=16.0, tsu_ld=15.0, tsu_clr=16.0, clr=63.0,
     tco_rco=53.0, ent_rco=40.0)
part("74HCT163", CD("163"), tco=49.0, tsu=13.0, tsu_en=16.0, tsu_ld=15.0, tsu_clr=16.0, clr=63.0,
     tco_rco=53.0, ent_rco=40.0)
part("74HCT175", CD("175"), tco=41.0, tsu=25.0, clr=44.0)
part("74HCT244", SN("244") + " (its -40..+125 °C column: no 85 °C one)", tpd=42.0, en=53.0)
part("74HCT245", SN("245"), tpd=28.0, en=58.0)
part("74HCT253", CD("253"), tpd=48.0, sel=50.0, en=38.0)
part("74HCT257", SN("257"), tpd=38.0, sel=38.0, en=38.0)
part("74HCT273", SN("273"), tco=42.0, tsu=25.0, clr=42.0)
part("74HCT374", SN("374"), tco=45.0, tsu=25.0, en=38.0)
part("74HCT377", CD("377"), tco=48.0, tsu=15.0, tsu_e=15.0)
part("74HCT393", CD("393") + " (CP to Q3, the ripple's last bit)", tco=78.0, clr=40.0)
part("74HCT540", SN("540"), tpd=25.0, en=38.0)
part("74HCT541", SN("541"), tpd=29.0, en=38.0)
part("74HCT574", SN("574"), tco=45.0, tsu=25.0, en=38.0)
part("74HCT595", SN("595") + " (its -40..+125 °C column)", tco=50.0, tco_sr=53.0, tsu=25.0, tsu_rclk=20.0,
     clr=50.0, en=44.0)
part("74HCT597", CD("597"), tco=48.0, tsu=13.0, load=70.0, clr=55.0)
part("74HCT688", CD("688"), tpd=42.0, en=30.0)
part("74HCT85", CD("85"), tpd=50.0, casc=39.0)
part("74HCT74", SN("74"), tco=35.0, tsu=15.0, clr=44.0)
part("74HC280", TI % "cd74hc280", tpd=50.0)

# ---------------------------------------------------------------- memories, GALs, peripherals
GAL = "https://ww1.microchip.com/downloads/en/DeviceDoc/doc0735.pdf"
part("GAL-10", GAL + " (ATF22V10C-10)", tpd=10.0, tco=6.5, tsu=4.5, tcf=2.5, tea=10.0)
part("GAL-7", GAL + " (ATF22V10C-7; tCO 5.5 in DIP, footnote 2)", tpd=7.5, tco=5.5, tsu=3.5, tcf=2.5, tea=7.5)
part("CY7C199", "Cypress 38-00239-E (CY7C199-15)", tAA=15.0, tACE=15.0, tDOE=7.0, tSD=9.0, tAW=10.0, tSCE=10.0)
part("AS6C4008", "Alliance Memory AS6C4008 v1.4 (-55)", tAA=55.0, tACE=55.0, tDOE=30.0, tSD=25.0, tAW=50.0, tSCE=50.0)
SST = "Microchip DS20005022C, table 11"
part("SST39SF010A", SST + " (-55: the control store, D18)", tAA=55.0, tCE=55.0, tOE=35.0)
part("SST39SF020A", SST + " (-70: the boot ROM until D142; no card has it now)", tAA=70.0, tCE=70.0, tOE=35.0)
# the memory card's 28-pin ROM site (D142). Fitted: an AT28C64B-15. Timed here as a 250 ns part with 100 ns from
# -OE, the slowest grade of the 28-pin EPROMs and of the AT28C256 short of its -35: what the site promises to take
part("ROM28", "Atmel 0270L (AT28C64B-15: 150 ns, 70 from -OE), timed as the slowest part the site takes: 250 ns, "
     "100 from -OE (Microchip DS11107M, 27C64-25; Atmel 0006M, AT28C256-25)", tAA=250.0, tCE=250.0, tOE=100.0)
part("TL16C550", TI % "tl16c550c" + " (RD to data 45; address and CS 7 before the strobe, data 15 before WR ends)",
     tRVD=45.0, tDS=15.0, tAS=7.0)
part("DS12C887", "ESTIMATE: its datasheet could not be fetched (Intel-mode read delay taken as 120)",
     tDDR=120.0, tDSW=50.0, tASL=20.0)
part("TIL311", "TI SODS001D (the latch: data 50 ns before the strobe rises and 40 ns after; the strobe low 40 ns)",
     tsu=50.0, th=40.0, tw=40.0)

# ---------------------------------------------------------------- the video card, revision 2 (D103, D107)
# The parts of YACC1-D's video card V3 that P32W's keeps, at the maximums of that card's own table
# (tests/video/v30_netsim.py there: TI SN74F74 SDFS046A, SN74F299 SDFS071B, SN74F21, SN74ACT10 / 11 / 86 / 245 /
# 573, CD74ACT02, CD74ACT273 SCHS249C; Alliance AS6C1008-55 v1.2; Atmel AT27C256R-70 doc 0014). That card's
# check, with minimums too and at eight corners, is what times them (generators/sim_video_timing.py); here they
# are only so that the machine's analysis knows every part of the card.
YV3 = "YACC1-D tests/video/v30_netsim.py: "
part("74F74", YV3 + "TI SDFS046A", tco=9.2, tsu=3.0, clr=10.5)
part("74F21", YV3 + "TI SN74F21", tpd=5.5)
part("74F299", YV3 + "TI SDFS071B", tco=10.0, tsu=8.5, clr=10.5)
part("74ACT02", YV3 + "TI CD74ACT02", tpd=11.1)
part("74ACT10", YV3 + "TI SN74ACT10", tpd=10.0)
part("74ACT11", YV3 + "TI SN74ACT11", tpd=10.5)
part("74ACT86", YV3 + "TI SN74ACT86", tpd=10.5)
part("74ACT273", YV3 + "TI SCHS249C", tco=12.3, tsu=2.0, clr=12.3)
part("74ACT573", YV3 + "TI SN74ACT573 (a transparent latch: D or LE to Q)", tpd=12.0, en=11.0)
part("74ACT245", YV3 + "TI SN74ACT245", tpd=9.0, en=12.0)
part("AS6C1008", YV3 + "Alliance AS6C1008-55 v1.2", tAA=55.0, tACE=55.0, tDOE=30.0, tSD=25.0, tAW=50.0, tSCE=50.0)
part("27C256", YV3 + "Atmel AT27C256R-70 doc 0014 (the font's 27C256 taken as that fast)", tAA=70.0, tCE=70.0, tOE=30.0)

# ---------------------------------------------------------------- lookups
def family(dev):
    return dev.replace("CD74", "74")

def timing(dev, value=""):
    if dev.startswith("GAL_"):
        return T["GAL-7" if "-7" in (value or "") else "GAL-10"]
    d = dev if dev in T else family(dev)
    if d not in T:
        raise KeyError("no switching times for %s" % dev)
    return T[d]

# Drivers into a backplane line: the datasheet's 50 pF against the line's,
# and the line itself (30 cm, its sockets: about 3 ns). ns per pF beyond
# 50 pF: HCT (6 mA outputs) about 0.06; ACT, 74F, LVC, the GALs (16-24 mA)
# about 0.02. The 7406 (D128; its datasheet's load is 15 pF): 0.1, from
# the current it is promised to sink, 40 mA less a 1k pull-up's 5: a line's
# 185 pF more through 3.5 V takes 19 ns. (TI gives no curve.)
def bus_derate(dev, value, c):
    if dev == "7406":
        return 3.0 + 0.10 * max(0.0, c - 15.0)
    extra = max(0.0, c - 50.0)
    k = 0.06 if "HC" in dev and "ACT" not in dev else 0.02
    return 3.0 + k * extra

# (D142: the 74ACT00, 74ACT02 and 74ACT10 were missing: the debug card's -WE, a NAND of the sample clock, was timed
# as rising with that clock, the edge it falls on.)
INVERTING = ("74HCT00", "74ACT00", "74ACT02", "74ACT10", "74HCT14", "74HCT04", "74ACT04", "74HC05", "7406", "74HCT4002",
             "74HCT540")

def inverts(dev):
    return dev in INVERTING

def bidir(dev):
    return dev.endswith("245")

def falling_clock(dev):
    """Parts that act on their clock's falling edge."""
    return dev.endswith("393")

# Typical against maximum: the datasheets' 25 °C, 5 V typical over their
# -40..+85 °C maximum, family by family (e.g. the 74HCT541 14 / 29, the
# 74F283 6.6 / 10.5, the 74ACT574 7 / 12, the 74LVC574 4.6 / 7.0, the GAL's
# typical about 60 % of its maximum); the memories' access times have no
# typical, taken as 85 %.
def typical(dev, value=""):
    if dev.startswith("GAL_"):
        return 0.6
    if dev in ("CY7C199", "AS6C4008", "AS6C1008", "27C256", "ROM28", "TL16C550", "DS12C887") or dev.startswith("SST39"):
        return 0.85
    d = family(dev)
    for prefix, f in (("74HCT", 0.5), ("74HC", 0.5), ("74ACT", 0.6), ("74LVC", 0.65), ("74F", 0.63), ("74LS", 0.5),
                      ("7406", 0.65)):                 # (the 7406: 15 / 23)
        if d.startswith(prefix):
            return f
    return 1.0
