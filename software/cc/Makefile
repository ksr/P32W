# Makefile -- the C compiler, rebuilt on P32W itself (/usr/src/cc).
# (The host builds it with ../Makefile; this one is for make on P32W.)
#
#   make              rebuild cc from whatever changed
#   make install      as root: put it in /bin/cc
#   make test         compile cc with the new cc and check it is the same
#   make clean        remove the objects and the program
#
# Compiling parse.c needs well under 2 MB of the 8 MiB a process gets (D33, D34).

OBJS = cpp.o gen.o lex.o main.o parse.o type.o

cc: $(OBJS)
	cc -o cc $(OBJS)

$(OBJS): cc.h

install: cc
	cp cc /bin/cc.new && chmod 755 /bin/cc.new && mv /bin/cc.new /bin/cc

# the compiler compiling itself: the second generation must be identical
test: cc
	rm -rf /tmp/cc2; mkdir /tmp/cc2
	for f in cpp gen lex main parse type; do ./cc -c -o /tmp/cc2/$$f.o $$f.c || exit 1; done
	cd /tmp/cc2 && cc -o cc2 cpp.o gen.o lex.o main.o parse.o type.o
	cmp cc /tmp/cc2/cc2 && echo the compiler reproduces itself
	rm -rf /tmp/cc2

clean:
	rm -f cc $(OBJS)

.PHONY: install test clean
