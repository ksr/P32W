# Makefile -- the C library, rebuilt on P32W itself (/usr/src/libc).
# (The host builds it with ../Makefile; this one is for make on P32W.)
#
#   make              rebuild libc.a and crt0.o from whatever changed
#   make install      as root: put them in /usr/lib (new programs use them)
#   make clean        remove the objects
#
# The headers are the system's own, in /usr/include.

OBJS = crc32.o ctype.o dbl.o deflate.o erf.o errno.o f32.o fpconv.o fpdefault.o fpfmt.o fphook.o fpmath.o gamma.o help.o inflate.o \
       llong.o locale.o malloc.o math.o printf.o process.o \
       pwd.o scanf.o softfp.o stdio.o stdlib.o strerror.o strftime.o string.o sys.o \
       sys2.o syscall.o setjmp.o sigtramp.o mem.o str.o

all: libc.a crt0.o

libc.a: $(OBJS)
	ar libc.a $(OBJS)

install: all
	cp libc.a /usr/lib/libc.a.new && mv /usr/lib/libc.a.new /usr/lib/libc.a
	cp crt0.o /usr/lib/crt0.o.new && mv /usr/lib/crt0.o.new /usr/lib/crt0.o

clean:
	rm -f libc.a crt0.o $(OBJS)

.PHONY: all install clean
