# microcode/

| File | Role |
|---|---|
| `p32isa.py` | the ISA's single source of truth: opcodes, formats, privileged set, control registers, exception causes |
| `p32ucode.py` | the microcode generator (docs/microarch.md, D14): microword fields, the microcode of every instruction and of reset / trap entry / interrupt entry, the commit-after-access check, the control-store images and `emulator/p32uc.h` |

```sh
python3 p32ucode.py --stats              # check, build the images, print cycles per instruction
```

Outputs (build artifacts, not committed; `make` in `emulator/` builds them):
`p32ucode.bin` (16384 words x 8 bytes, the emulator's image) and
`u0.bin`-`u6.bin` (one image per flash chip).

The generator refuses to build if any instruction writes programmer-visible
state before its last possibly-faulting memory access (precise exceptions,
microarch.md section 5).
