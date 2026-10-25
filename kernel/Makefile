# kernel/Makefile -- P32W Unix (M3, D22): kernel/unix.bin, a flat image the
# monitor's `b` loads at 0x0010_0000 (tools/p32fs.py --kernel puts it on the disk).
PY   = python3
CC   = $(PY) ../cc/p32cc.py -w -I .
AS   = $(PY) ../assembler/p32asm.py -c
LD   = $(PY) ../assembler/p32ld.py
B    = build
SRCS = main.c trap.c proc.c vm.c syscall.c exec.c fs.c bio.c cf.c ramdisk.c usb.c net.c file.c tty.c video.c rtc.c kprintf.c klib.c
OBJS = $(B)/start.o $(B)/mem.o $(SRCS:%.c=$(B)/%.o)

all: unix.bin

unix.bin: $(OBJS)
	$(LD) --format bin --text 0x00100000 -e _start -M unix.map $(OBJS) -o $@

$(B)/%.o: %.c *.h ../software/include/*.h ../software/include/sys/*.h ../cc/*.py | $(B)
	$(CC) -c $< -o $@

$(B)/start.o: start.s | $(B)
	$(AS) $< -o $@

$(B)/mem.o: ../software/libc/mem.s | $(B)     # memcpy, memset (shared with libc)
	$(AS) $< -o $@

$(B):
	mkdir -p $(B)

clean:
	rm -rf $(B) unix.bin unix.map

.PHONY: all clean
