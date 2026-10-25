# assembler/

`p32asm.py` — the P32W two-pass assembler (ISA v0, [../docs/isa-v0.md](../docs/isa-v0.md)).

Mnemonics, opcodes and formats come from
[`../microcode/p32isa.py`](../microcode/p32isa.py), so the assembler and the
emulator cannot disagree about an encoding. Edit the instruction set there, not
here.

```sh
python3 p32asm.py src.asm [-o out.bin] [-l listing.txt]
```

Output is a flat little-endian image starting at address 0 (the ROM). The
syntax, directives and pseudo-instructions (`LI`, `LA`, `MOV`, `NOT`,
`PUSH`/`POP`, `BGT`/`BLE`, `BEQZ`/`BNEZ`, …) are listed in the module
docstring. `.include "file"` splices in another source file (path relative to
the including file); `MFCR`/`MTCR` take control-register names (`STATUS`,
`EPC`, `CAUSE`, `BADADDR`, `VBR`, `USP`).
