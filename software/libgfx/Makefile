# Makefile -- libgfx, the graphics library (-lgfx; gfx(3)), rebuilt on P32W itself (/usr/src/libgfx).
# (The host builds it with ../Makefile; this one is for make on P32W.)
#
#   make              rebuild libgfx.a from whatever changed
#   make install      as root: put it in /usr/lib (cc -lgfx uses it)
#   make clean        remove the objects
#
# Four members, so a program that draws no circles or no text does not carry them.

OBJS = gfx.o gell.o gtext.o gsin.o

all: libgfx.a

libgfx.a: $(OBJS)
	ar libgfx.a $(OBJS)

gfx.o gell.o gtext.o: gfxint.h

install: all
	cp libgfx.a /usr/lib/libgfx.a.new && mv /usr/lib/libgfx.a.new /usr/lib/libgfx.a

clean:
	rm -f libgfx.a $(OBJS)

.PHONY: all install clean
