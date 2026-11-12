#!/usr/bin/env python3
"""The timing analyses p32timing.py runs (D70): which cards, which
oscillator, its period, the phase generators' quiet inputs, clocks made
off the boards' logic, and the exceptions, each with its reason."""
from p32timing import Exc

OSC_3MHZ = 1e3 / 24.0          # the CPU card's 24 MHz: a 3 MHz microcycle
OSC_4MHZ = 1e3 / 32.0          # its 32 MHz, with the clock jumper fitted (D74): 4 MHz

MACHINE_EXC = [
    # the debug card's capture SRAMs are read back, and its trigger RAMs set
    # up, only while it captures nothing (D64)
    Exc(None, "the debug card's capture SRAMs are read back, and its trigger RAMs set up, only while stopped",
        thrufn=["*onto the local bus", "trigger RAM*write data*"]),
    # the opcode mux takes R only in the step after FETCH (a read): the
    # instruction arriving from memory (cpu-card-theory 5)
    Exc(None, "the opcode mux takes R only after a FETCH (a read cycle)", mode="write",
        thru=["LR[0-9]*"], thrufn=["opcode mux*"]),
    Exc(None, "the opcode mux takes R only after a FETCH (a read cycle)", mode="internal",
        thru=["LR[0-9]*"], thrufn=["opcode mux*"]),
    # the I/O card's byte devices take two clocks, -WAIT holding the first
    # (D72): their data, on the card's byte bus, and their strobes have both
    Exc(1, "the I/O card's byte devices: two clocks, -WAIT holding the first (D72)",
        thru=["io/ID[0-9]", "io/IORDN", "io/IOWRN", "io/REQCLK", "io/POCLK", "io/RTCAS",
              "io/REPOEN", "io/STATOEN", "io/PORDN", "io/PINRDN"]),
    Exc(1, "the I/O card's byte devices: two clocks, -WAIT holding the first (D72)",
        clock=["io/IOWRN", "io/REQCLK", "io/POCLK", "io/RTCAS"]),
    # the register file is written in phases 0-2, its address multiplexers then
    # selecting the write address (WPH high): the read address does not reach it
    Exc(None, "a register-file write uses the write address (WPH selects it in phases 0-2)",
        thru=["cpu/RA[0-9]*", "cpu/RB[0-9]*"], tofn=["register file*"]),
    # interrupt requests come when they come: IPEND samples them at a commit
    # (a synchronizer); a crossing, not a timed path
    Exc(None, "the -IRQ lines are asynchronous to the CPU (IPEND samples them)", thru=["-IRQ*"]),
    # instructions are fetched from memory cards only: a device's read data never
    # reaches the instruction register or the opcode decode
    Exc(None, "instructions come only from memory: no fetch from a device card", mode="read",
        thru=["io/*", "disk/*"], frm=["cpu/*", "mmu/*", "io/*", "disk/*"],
        tofn=["pipeline register*", "IR bits*", "opcode*", "control store*"]),
    # the ALU's result latches: clocked by the FPGA after an ALUGO commit, read
    # (RSRC = ALU) in a later microword, never the one that started it
    Exc(1, "the ALU's result: read in a later microcycle than ALUGO's (D21)", frmfn=["ALU result*"]),
]

DISK_DMA_EXC = [
    # the I/O card's byte devices take two clocks, -WAIT holding the first (D72)
    MACHINE_EXC[3], MACHINE_EXC[4],
]

DEBUG_EXC = [
    # in its timing mode the sample latches are a logic analyser's: they take
    # every line at their own 12 MHz, whenever it changes (D73)
    Exc(None, "the timing mode samples the lines asynchronously, as a logic analyser does", tofn=["*sample latch*"]),
    # the host reads the capture SRAMs, and writes the trigger RAMs, over the
    # card's local bus only while nothing is captured (D64)
    Exc(None, "the capture SRAMs are read back, and the trigger RAMs set up, only while stopped",
        thrufn=["*onto the local bus", "trigger RAM*write data*"]),
    # it listens or drives, never both (D64)
    Exc(None, "the card listens or drives, never both", thrufn=["*drive register*"],
        tofn=["*sample latch*", "*trigger*", "the capture", "*capture SRAM*"]),
]

# The video card is not in these runs. Revision 0 (D67), whose bus side and dot clock were timed here, gave way
# to revision 2, the HD63484 card (D103, D106). Revision 2's memory side and dots are YACC1-D's video card V3,
# timed by that card's own check with each part's data-sheet delays at eight corners (tests/video/v30_netsim.py
# there: docs/video-card-acrtc.md 2); its bus side joins the machine's runs with step 4 of that note's plan.
RUNS = [
    dict(name="the machine at 3 MHz", cards=["cpu", "mmu", "mem", "io", "disk", "debug"],
         root="cpu/OSC", period=OSC_3MHZ, phases=8,
         gen={"CLKGEN": {"STALL": 0, "HOLD": 0, "ALUGOK": 1}},
         # the FPGA clocks the result latches (CKLO / CKHI) at most 200 ns after ACLK (D21: a division, 18 of its clocks)
         virtual={"cpu/CKLO": ("cpu/ACLK", 200.0), "cpu/CKHI": ("cpu/ACLK", 200.0)},
         exceptions=MACHINE_EXC,
         modes=["read", "write", "internal"],
         # GAL PANEL (reset, run / step, halt): its registers change at a commit (the
         # edge that starts phase 0) or with a button, never mid-cycle while running
         launch_only={"cpu/U3": ([0], "GAL PANEL: the front panel's state changes at a commit, or with a button")}),
    # the clock jumper fitted (D74): 4 MHz is promised at typical delays only
    # (worst case is 3 MHz, above), so only the typical corner is timed
    dict(name="the machine at 4 MHz (the clock jumper)", cards=["cpu", "mmu", "mem", "io", "disk", "debug"],
         root="cpu/OSC", period=OSC_4MHZ, phases=8, corners=["typ"],
         gen={"CLKGEN": {"STALL": 0, "HOLD": 0, "ALUGOK": 1}},
         virtual={"cpu/CKLO": ("cpu/ACLK", 200.0), "cpu/CKHI": ("cpu/ACLK", 200.0)},
         exceptions=MACHINE_EXC,
         modes=["read", "write", "internal"],
         launch_only={"cpu/U3": ([0], "GAL PANEL: the front panel's state changes at a commit, or with a button")}),
    # the disk card's DMA engine as master (D75): its read and write cycles,
    # the slave cards answering (the CPU and the MMU / data card are held and
    # drive nothing); CLK the bus clock, the cards registering on its rise
    dict(name="the disk card's DMA cycles (3 MHz)", cards=["disk", "mem", "io"], root="CLK",
         period=1e3 / 3.0, phases=1, master=("disk",), modes=["read", "write"], exceptions=DISK_DMA_EXC),
    dict(name="the debug card alone (12 MHz timing mode)", cards=["debug"], root="debug/OSC", period=OSC_3MHZ, phases=8,
         gen={"DBCLK": {"CLKRUN": 1, "STEPQ": 0, "CLKOE": 1}}, exceptions=DEBUG_EXC),
]
