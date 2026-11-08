# P32W instruction-level reference emulator (D7).
CC     ?= cc
CFLAGS ?= -O2 -std=c99 -Wall -Wextra -Wpedantic
ISA    := ../microcode/p32isa.py
GEN    := ../generators/gen_p32opc.py

UGEN   := ../microcode/p32ucode.py
UCODE  := ../microcode/p32ucode.bin

all: p32emu p32uemu

p32emu: p32emu.c p32opc.h p32io.h p32dbg.h
	$(CC) $(CFLAGS) -o $@ p32emu.c

# microcode-level emulator (D14) and the control store it runs
p32uemu: p32uemu.c p32opc.h p32uc.h p32io.h p32dbg.h $(UCODE)
	$(CC) $(CFLAGS) -o $@ p32uemu.c

p32uc.h $(UCODE): $(ISA) $(UGEN)
	python3 $(UGEN) -o ../microcode --header p32uc.h

# generated from the ISA single source of truth -- never edit by hand
p32opc.h: $(ISA) $(GEN)
	python3 $(GEN) -o $@

test: p32emu p32uemu
	sh test/run_tests.sh

clean:
	rm -f p32emu p32uemu p32opc.h p32uc.h ../microcode/*.bin test/*.bin test/*.lst test/*.out test/*.err test/*.trace

.PHONY: all test clean
