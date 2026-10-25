# Makefile -- liby, yacc's library (-ly), rebuilt on P32W itself (/usr/src/liby).
# (The host builds it with ../Makefile; this one is for make on P32W.)
#
#   make              rebuild liby.a from whatever changed
#   make install      as root: put it in /usr/lib (cc -ly uses it)
#   make clean        remove the objects
#
# Two members, so a program may have its own yyerror and still take main.

OBJS = main.o yyerror.o

all: liby.a

liby.a: $(OBJS)
	ar liby.a $(OBJS)

install: all
	cp liby.a /usr/lib/liby.a.new && mv /usr/lib/liby.a.new /usr/lib/liby.a

clean:
	rm -f liby.a $(OBJS)

.PHONY: all install clean
