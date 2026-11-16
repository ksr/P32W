---
hide:
  - toc
---

<!-- Author: Claude (Anthropic) for Ken Rother, 2026. The visitor home page of the P32W website (stage.py copies it
     to index.md). Every fact here comes from the P32W repository: README.md, docs/architecture.md,
     docs/programmers-guide.md. -->

# P32W — a 32-bit homebrew computer and its Unix

**P32W is a 32-bit computer built from discrete 74-series logic, GALs, SRAM and flash**, on 9U × 340 mm cards with
three 96-pin bus connectors each, in a 10-slot backplane. The ALU is an FPGA module (a Tang Nano 20K, in sockets: every
part is through-hole). It runs a custom load/store instruction set with traps, interrupts and virtual memory, and
**P32W Unix**, a multi-user V6-style Unix written in C, with a native C compiler, assembler and linker, MicroPython,
Lua, SQL, BASIC and networking.

The cards are designed and laid out in KiCad; the software runs today on two emulators, one of them at the level of
the microcode.

I'm Ken Rother. I designed P32W. Claude, Anthropic's AI, has been directly involved in its design, coding and
documentation.

## At a glance

| | |
|---|---|
| **Word** | 32-bit, load/store, fixed 32-bit instructions, byte addressing, little-endian; no condition flags (branches compare two registers) |
| **Modes** | supervisor and user; each user program has a 2 GiB virtual address space through a software-managed TLB |
| **Memory** | a 1 MiB boot ROM at address 0, RAM above it on one to four 16 MiB memory cards (up to 64 MiB), the I/O page at the top |
| **Speed** | a 3 MHz microcycle (4 MHz with the clock jumper), about 6–7 microcycles per instruction: roughly 0.5 MIPS |
| **Bus** | synchronous, one clock, stretched by `-WAIT`; physical memory cycles, interrupts and arbitration only. The CPU and MMU / data cards have a private link of their own |
| **Disk** | 2 CompactFlash sockets and 2 IDE ports |
| **I/O** | 4 serial ports, timer, real-time clock, 2 USB, Ethernet and Wi-Fi |
| **Video** | VGA 640 × 480, drawn by an HD63484 ACRTC; an 80 × 25 console; PS/2 keyboard and mouse |

## The cards

<div class="grid cards" markdown>

-   ![CPU card, KiCad render](hardware/cpu-card/kicad/cpu-card-render-3d.png)

    **[CPU](docs/cpu-card.md)** — the sequencer and microcode, the register file, the ALU in its FPGA module, the clock.

-   ![MMU / data card, KiCad render](hardware/mmu-card/kicad/mmu-card-render-3d.png)

    **[MMU / data](docs/mmu-card.md)** — address translation, the bus master interface and the arbiter.

-   ![Memory card, KiCad render](hardware/mem-card/kicad/mem-card-render-3d.png)

    **[Memory](docs/mem-card.md)** — 16 MiB of SRAM and the boot ROM; one to four of them.

-   ![Disk card, KiCad render](hardware/disk-card/kicad/disk-card-render-3d.png)

    **[Disk](docs/disk-card.md)** — two CompactFlash sockets and two IDE ports.

-   ![I/O card, KiCad render](hardware/io-card/kicad/io-card-render-3d.png)

    **[I/O](docs/io-card.md)** — serial ports, timer, clock, USB, Ethernet and Wi-Fi.

-   ![Video card, KiCad render](hardware/video-card/kicad/video-card-render-3d.png)

    **[Video](docs/video-card-acrtc.md)** — VGA from an HD63484 ACRTC, and the PS/2 keyboard and mouse.

-   ![Debug card, KiCad render](hardware/debug-card/kicad/debug-card-render-3d.png)

    **[Debug](docs/debug-card.md)** — a logic analyzer on every bus line, or the bus master that tests each card alone.

-   ![Backplane, KiCad render](hardware/backplane/kicad/backplane-render-3d.png)

    **[Backplane](docs/backplane.md)** — the ten slots the cards plug into.

</div>

The pictures are KiCad renders of the boards as laid out, turned so the bus connectors are at the bottom.

## Where to start

- **[System architecture](docs/architecture.md)** — the machine, the cards, the address map.
- **[Programmer's guide](docs/programmers-guide.md)** — how to program it.
- **[Using P32W Unix](docs/unix.md)** — the operating system as a user sees it.
- **[Design decisions](docs/decisions.md)** — every choice, numbered, with its reason.
- **[The books (PDF)](books.md)** — the hardware, the bus, the ISA, the programmer's guide, Unix and the emulators, as
  printable books.
