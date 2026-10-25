# Makefile -- the SQL library (libsql.a), the sql command and sqlui (its
# full-screen interface), built on P32W Unix itself. (On the host, test.sh
# and sqlui_test.py build and test them.)
#
#   make              libsql.a, sql and sqlui
#   make apitest      the API's test program (./apitest newdir runs it)
#   make install      as root: libsql.a to /usr/lib, sqldb.h to
#                     /usr/include, sql and sqlui to /bin
#   make clean        remove what was built here
#
# The library is six files so that cc, which keeps a whole file in memory,
# compiles each in the 8 MiB of a process. A program using it:
#     cc -o prog prog.c -lsql

CC = cc
CFLAGS =
OBJS = sqlstore.o sqlbtree.o sqlparse.o sqleval.o sqlselect.o sqldb.o

all: libsql.a sql sqlui

libsql.a: $(OBJS)
	ar libsql.a $(OBJS)

$(OBJS): sqlint.h sqldb.h

sql: sql.c sqldb.h libsql.a
	$(CC) $(CFLAGS) -o sql sql.c libsql.a

sqlui: sqlui.c sqluiscr.c sqlui.h sqldb.h libsql.a
	$(CC) $(CFLAGS) -o sqlui sqlui.c sqluiscr.c libsql.a

apitest: apitest.c sqldb.h libsql.a
	$(CC) $(CFLAGS) -o apitest apitest.c libsql.a

install: libsql.a sql sqlui
	cp libsql.a /usr/lib/libsql.a.new && mv /usr/lib/libsql.a.new /usr/lib/libsql.a
	cp sqldb.h /usr/include/sqldb.h
	cp sql /bin/sql.new && chmod 755 /bin/sql.new && mv /bin/sql.new /bin/sql
	cp sqlui /bin/sqlui.new && chmod 755 /bin/sqlui.new && mv /bin/sqlui.new /bin/sqlui

clean:
	rm -f $(OBJS) libsql.a sql sqlui apitest

.PHONY: all install clean
