# Makefile -- libl.a, the library for lex programs (cc ... -ll), built on
# P32W Unix itself: main (calls yylex until it returns 0) and yywrap
# (returns 1), each a member of its own so a program may replace either.
#
#   make              libl.a
#   make install      as root: libl.a to /usr/lib
#   make clean        remove what was built here

CC = cc
CFLAGS =
OBJS = main.o yywrap.o

all: libl.a

libl.a: $(OBJS)
	ar libl.a $(OBJS)

install: libl.a
	cp libl.a /usr/lib/libl.a.new && mv /usr/lib/libl.a.new /usr/lib/libl.a

clean:
	rm -f $(OBJS) libl.a

.PHONY: all install clean
