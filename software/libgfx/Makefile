# Makefile -- libgfx, the graphics library (-lgfx; gfx(3)), rebuilt on P32W itself (/usr/src/libgfx).
# (The host builds it with ../Makefile; this one is for make on P32W.)
#
#   make              rebuild libgfx.a from whatever changed
#   make install      as root: put it in /usr/lib (cc -lgfx uses it)
#   make clean        remove the objects
#
# Seven members, so a program that draws no circles, or no text, fills and copies nothing or has no mouse, does not
# carry them.

OBJS = gfx.o gell.o gtext.o gsin.o gfill.o gcopy.o gmouse.o

all: libgfx.a

libgfx.a: $(OBJS)
	ar libgfx.a $(OBJS)

gfx.o gell.o gtext.o gfill.o gcopy.o: gfxint.h

install: all
	cp libgfx.a /usr/lib/libgfx.a.new && mv /usr/lib/libgfx.a.new /usr/lib/libgfx.a

clean:
	rm -f libgfx.a $(OBJS)

.PHONY: all install clean
