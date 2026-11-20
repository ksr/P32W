# software/Makefile -- the P32W C library and start-up files (M2, D22).
#   make            build/libc.a, build/crt0.o (P32W Unix), build/sa/crt0_sa.o (stand-alone)
# Built with the repository's own tools: cc/p32cc.py, assembler/p32asm.py, p32ar.py.
PY    = python3
CC    = $(PY) ../cc/p32cc.py -w
AS    = $(PY) ../assembler/p32asm.py -c
AR    = $(PY) ../assembler/p32ar.py
B     = build

LIBC_C = $(wildcard libc/*.c)
LIBC_S = libc/syscall.s libc/setjmp.s libc/sigtramp.s libc/mem.s libc/str.s
OBJS   = $(LIBC_C:libc/%.c=$(B)/%.o) $(LIBC_S:libc/%.s=$(B)/%.o)

CMDS   = init sh ls cat echo halt sync true false mkdir rmdir rm ln mv cp pwd wc grep kill ps \
         chmod chown date sleep head tail tee touch sort uniq stty uname \
         getty login su passwd who id ed od cmp more find du basename dirname whoami fsck test expr time cycles uptime shutdown reboot tar \
         gzip zip unzip dos ifconfig ping telnet fetch ftp telnetd ntpdate kermit as ld ar vi top man make basic tr cut paste join comm xargs sed diff bc chgrp mknod mesg write wall mail yacc awk lex wm win clock files finder edit paint view adduser db mount mkfs df cron at
BINS   = $(CMDS:%=$(B)/bin/%)

all: $(B)/libc.a $(B)/crt0.o $(B)/sa/crt0_sa.o $(BINS) $(B)/bin/cc $(B)/bin/sql $(B)/bin/sqlui $(B)/libsql.a $(B)/liby.a $(B)/libl.a $(B)/libgfx.a $(LUA_BINS) $(B)/bin/python

# the commands: P32W Unix a.out files
$(B)/bin/%: cmd/%.c $(B)/libc.a $(B)/crt0.o
	mkdir -p $(B)/bin
	$(CC) $< -o $@

# the SQL database: libsql.a (six files, each within the native cc's memory)
# and the sql command (sql/README.md)
SQL_SRC = sql/sqlstore.c sql/sqlbtree.c sql/sqlparse.c sql/sqleval.c sql/sqlselect.c sql/sqldb.c
SQL_OBJ = $(SQL_SRC:sql/%.c=$(B)/sql/%.o)
$(B)/sql/%.o: sql/%.c sql/sqlint.h sql/sqldb.h $(wildcard include/*.h include/sys/*.h) ../cc/*.py
	mkdir -p $(B)/sql
	$(CC) -I sql -c $< -o $@
$(B)/libsql.a: $(SQL_OBJ)
	$(AR) $@ $(SQL_OBJ)

# liby.a (-ly, for yacc's parsers): main and yyerror, one member each
LIBY_OBJ = $(B)/liby/main.o $(B)/liby/yyerror.o
$(B)/liby/%.o: liby/%.c $(wildcard include/*.h include/sys/*.h) ../cc/*.py
	mkdir -p $(B)/liby
	$(CC) -c $< -o $@
$(B)/liby.a: $(LIBY_OBJ)
	$(AR) $@ $(LIBY_OBJ)

# libgfx.a (-lgfx; gfx(3)): drawing on the video card's screen, through /dev/video
LIBGFX_OBJ = $(B)/libgfx/gfx.o $(B)/libgfx/gell.o $(B)/libgfx/gtext.o $(B)/libgfx/gsin.o $(B)/libgfx/gfill.o $(B)/libgfx/gcopy.o $(B)/libgfx/gmouse.o $(B)/libgfx/gwin.o
$(B)/libgfx/%.o: libgfx/%.c libgfx/gfxint.h $(wildcard include/*.h include/sys/*.h) ../cc/*.py
	mkdir -p $(B)/libgfx
	$(CC) -c $< -o $@
$(B)/libgfx.a: $(LIBGFX_OBJ)
	$(AR) $@ $(LIBGFX_OBJ)

# awk: eight files (awk.h lists them)
AWK_SRC = cmd/awk.c cmd/awklex.c cmd/awkparse.c cmd/awkrun.c cmd/awkvar.c cmd/awkfn.c cmd/awknum.c cmd/awkre.c
$(B)/bin/awk: $(AWK_SRC) cmd/awk.h $(B)/libc.a $(B)/crt0.o
	mkdir -p $(B)/bin
	$(CC) $(AWK_SRC) -o $@

# lex: four files (the specification, the patterns and NFA, the DFA, the output)
$(B)/bin/lex: cmd/lex.c cmd/lexre.c cmd/lexdfa.c cmd/lexout.c cmd/lex.h $(B)/libc.a $(B)/crt0.o
	mkdir -p $(B)/bin
	$(CC) cmd/lex.c cmd/lexre.c cmd/lexdfa.c cmd/lexout.c -o $@

# libl.a (-ll, for lex's scanners): main and yywrap, one member each
LIBL_OBJ = $(B)/libl/main.o $(B)/libl/yywrap.o
$(B)/libl/%.o: libl/%.c $(wildcard include/*.h include/sys/*.h) ../cc/*.py
	mkdir -p $(B)/libl
	$(CC) -c $< -o $@
$(B)/libl.a: $(LIBL_OBJ)
	$(AR) $@ $(LIBL_OBJ)

# yacc: three files (the grammar, the LALR(1) automaton, the output)
$(B)/bin/yacc: cmd/yacc.c cmd/ylr.c cmd/yout.c cmd/yacc.h $(B)/libc.a $(B)/crt0.o
	mkdir -p $(B)/bin
	$(CC) cmd/yacc.c cmd/ylr.c cmd/yout.c -o $@
$(B)/bin/sql: sql/sql.c sql/sqldb.h $(B)/libsql.a $(B)/libc.a $(B)/crt0.o
	mkdir -p $(B)/bin
	$(CC) -I sql sql/sql.c $(B)/libsql.a -o $@
$(B)/bin/sqlui: sql/sqlui.c sql/sqluiscr.c sql/sqlui.h sql/sqldb.h $(B)/libsql.a $(B)/libc.a $(B)/crt0.o
	mkdir -p $(B)/bin
	$(CC) -I sql sql/sqlui.c sql/sqluiscr.c $(B)/libsql.a -o $@

# sed: two files (its regular expressions in sedre.c)
$(B)/bin/sed: cmd/sed.c cmd/sedre.c cmd/sed.h $(B)/libc.a $(B)/crt0.o
	mkdir -p $(B)/bin
	$(CC) cmd/sed.c cmd/sedre.c -o $@

# bc: three files (the parser and library, the interpreter, the arithmetic)
$(B)/bin/bc: cmd/bc.c cmd/bcexec.c cmd/bcnum.c cmd/bc.h $(B)/libc.a $(B)/crt0.o
	mkdir -p $(B)/bin
	$(CC) cmd/bc.c cmd/bcexec.c cmd/bcnum.c -o $@

# wm, the window manager (D122): four files and the graphics library
$(B)/bin/wm: cmd/wm.c cmd/wmwin.c cmd/wmterm.c cmd/wmgfx.c cmd/wmwin.h cmd/wmterm.h cmd/wmgfx.h $(B)/libgfx.a $(B)/libc.a $(B)/crt0.o
	mkdir -p $(B)/bin
	$(CC) cmd/wm.c cmd/wmwin.c cmd/wmterm.c cmd/wmgfx.c $(B)/libgfx.a -o $@

# the desk's programs (D125): each one file and the graphics library
$(B)/bin/clock $(B)/bin/files $(B)/bin/finder $(B)/bin/edit $(B)/bin/paint $(B)/bin/view: $(B)/bin/%: cmd/%.c $(B)/libgfx.a $(B)/libc.a $(B)/crt0.o
	mkdir -p $(B)/bin
	$(CC) $< $(B)/libgfx.a -o $@

# BASIC (D27): four files (each must fit the native cc's memory on P32W); its graphics are libgfx's (D112)
$(B)/bin/basic: cmd/basic.c cmd/bexpr.c cmd/bio.c cmd/bgfx.c cmd/basic.h sql/sqldb.h $(B)/libsql.a $(B)/libgfx.a $(B)/libc.a $(B)/crt0.o
	mkdir -p $(B)/bin
	$(CC) -I sql cmd/basic.c cmd/bexpr.c cmd/bio.c cmd/bgfx.c $(B)/libsql.a $(B)/libgfx.a -o $@

# the C compiler (M7, D24): several files, linked like any program
CC_SRC = $(wildcard cc/*.c)
CC_OBJ = $(CC_SRC:cc/%.c=$(B)/cc/%.o)
$(B)/cc/%.o: cc/%.c cc/cc.h $(wildcard include/*.h include/sys/*.h) ../cc/*.py
	mkdir -p $(B)/cc
	$(CC) -c $< -o $@
$(B)/bin/cc: $(CC_OBJ) $(B)/libc.a $(B)/crt0.o
	mkdir -p $(B)/bin
	$(PY) ../assembler/p32ld.py $(B)/crt0.o $(CC_OBJ) $(B)/libc.a -o $@

# Lua 5.4 (lua/README.P32W): the core and standard libraries as liblua.a,
# then lua (the interpreter) and luac (the compiler to bytecode). Built
# here only: the native cc runs out of memory on the five largest files.
LUA_SRC  = $(filter-out lua/lua.c lua/luac.c,$(wildcard lua/*.c))
LUA_OBJ  = $(LUA_SRC:lua/%.c=$(B)/lua/%.o)
LUA_BINS = $(B)/bin/lua $(B)/bin/luac
LUA_CC   = $(CC) -I lua
$(B)/lua/%.o: lua/%.c $(wildcard lua/*.h include/*.h include/sys/*.h) ../cc/*.py
	mkdir -p $(B)/lua
	$(LUA_CC) -c $< -o $@
$(B)/liblua.a: $(LUA_OBJ)
	$(AR) $@ $(LUA_OBJ)
$(B)/bin/lua $(B)/bin/luac: $(B)/bin/%: $(B)/lua/%.o $(B)/liblua.a $(B)/libc.a $(B)/crt0.o
	mkdir -p $(B)/bin
	$(CC) $< $(B)/liblua.a -o $@

# MicroPython 1.29 (micropython/README.P32W): build.sh makes its generated
# tables with MicroPython's own scripts, then compiles all of it with p32cc
# (about a minute), so any change rebuilds the lot. The same sources give
# the same binary.
MP_SRC = $(shell find micropython/py micropython/extmod micropython/shared micropython/lib micropython/ports/p32 -type f)
$(B)/bin/python: $(MP_SRC) $(wildcard include/*.h include/sys/*.h) ../cc/*.py $(B)/libc.a $(B)/crt0.o
	sh micropython/ports/p32/build.sh $(abspath $(B))/micropython
	mkdir -p $(B)/bin
	cp $(B)/micropython/micropython $@

# the disk's size: 512 MB, with an inode per 32 KB (D40)
DISK_MB     = 512
DISK_INODES = 16384

# a bootable disk: the kernel in the boot area, the root filesystem (root.manifest)
disk: $(B)/disk.img
$(B)/disk.img: $(BINS) $(B)/bin/cc $(B)/libc.a $(B)/crt0.o root.manifest etc/* examples/* include/*.h include/sys/*.h $(B)/src.stamp $(B)/systest.stamp $(B)/bin/sql $(B)/bin/sqlui $(B)/libsql.a $(B)/liby.a $(B)/libl.a $(B)/libgfx.a $(LUA_BINS) $(B)/bin/python man/whatis $(wildcard man/*/*) ../kernel/unix.bin
	$(PY) ../tools/p32fs.py build $@ $(DISK_MB) root.manifest --kernel ../kernel/unix.bin --inodes $(DISK_INODES)

# /usr/src on the disk: the sources of the commands, the compiler and the
# library, each with its Makefile for make on P32W, and what they build
# (so a first make there rebuilds only what has been edited). The kernel's
# Makefile is its own file, as kernel/Makefile is the host's (D86).
SRCTREE = $(B)/src
$(B)/src.stamp: $(BINS) $(B)/bin/cc $(B)/libc.a $(B)/crt0.o $(B)/bin/sql $(B)/bin/sqlui $(B)/libsql.a $(B)/liby.a $(B)/libl.a $(B)/libgfx.a $(wildcard cmd/* cc/* libc/* sql/* liby/* libl/* libgfx/* ../kernel/*.c ../kernel/*.h ../kernel/*.s) ../kernel/README.md ../kernel/Makefile.p32w lua/README.P32W $(B)/bin/python ../tools/mkmpsrc.py Makefile
	rm -rf $(SRCTREE)
	mkdir -p $(SRCTREE)/cmd $(SRCTREE)/cc $(SRCTREE)/libc
	cp cmd/*.c cmd/*.h cmd/Makefile $(SRCTREE)/cmd/
	cp $(BINS) $(SRCTREE)/cmd/ && cd $(SRCTREE)/cmd && chmod 755 $(CMDS)
	cp cc/*.c cc/*.h cc/Makefile $(CC_OBJ) $(SRCTREE)/cc/
	cp $(B)/bin/cc $(SRCTREE)/cc/cc && chmod 755 $(SRCTREE)/cc/cc
	cp libc/*.c libc/*.s libc/Makefile $(OBJS) $(B)/libc.a $(B)/crt0.o $(SRCTREE)/libc/
	mkdir -p $(SRCTREE)/sql
	cp sql/*.c sql/*.h sql/Makefile sql/README.md $(SQL_OBJ) $(B)/libsql.a $(SRCTREE)/sql/
	cp $(B)/bin/sql $(SRCTREE)/sql/sql && chmod 755 $(SRCTREE)/sql/sql
	cp $(B)/bin/sqlui $(SRCTREE)/sql/sqlui && chmod 755 $(SRCTREE)/sql/sqlui
	mkdir -p $(SRCTREE)/liby
	cp liby/*.c liby/Makefile $(LIBY_OBJ) $(B)/liby.a $(SRCTREE)/liby/
	mkdir -p $(SRCTREE)/libl
	cp libl/*.c libl/Makefile $(LIBL_OBJ) $(B)/libl.a $(SRCTREE)/libl/
	mkdir -p $(SRCTREE)/libgfx
	cp libgfx/*.c libgfx/*.h libgfx/Makefile $(LIBGFX_OBJ) $(B)/libgfx.a $(SRCTREE)/libgfx/
	mkdir -p $(SRCTREE)/lua
	cp lua/README.P32W $(SRCTREE)/lua/
	mkdir -p $(SRCTREE)/kernel
	cp ../kernel/*.c ../kernel/*.h ../kernel/*.s ../kernel/README.md $(SRCTREE)/kernel/
	cp ../kernel/Makefile.p32w $(SRCTREE)/kernel/Makefile
	$(PY) ../tools/mkmpsrc.py $(SRCTREE)/micropython
	touch $@

# /usr/tests: the system's tests to run on P32W (sh /usr/tests/run), their
# expected results made by the host's builds of the same programs
$(B)/systest.stamp: systest/* systest/*/* $(wildcard systest/cmd/*) ../cc/tests/*.c cmd/basic.c cmd/bexpr.c cmd/bio.c cmd/bgfx.c cmd/basic.h include/gfx.h sql/*.c sql/*.h libc/softfp.c ../tools/mksystest.py
	$(PY) ../tools/mksystest.py $(B)/systest
	touch $@

# the manual's index (man -k) and its extra names, from the pages' NAME lines
man/whatis: $(wildcard man/man*/*) ../tools/mkmanindex.py
	$(PY) ../tools/mkmanindex.py man

# the test suite's own copy (make test), so it never rewrites disk.img
# while someone is running the system from it
testdisk: $(B)/test.img
$(B)/test.img: $(BINS) $(B)/bin/cc $(B)/libc.a $(B)/crt0.o root.manifest etc/* examples/* include/*.h include/sys/*.h $(B)/src.stamp $(B)/systest.stamp $(B)/bin/sql $(B)/bin/sqlui $(B)/libsql.a $(B)/liby.a $(B)/libl.a $(B)/libgfx.a $(LUA_BINS) $(B)/bin/python man/whatis $(wildcard man/*/*) ../kernel/unix.bin
	$(PY) ../tools/p32fs.py build $@ $(DISK_MB) root.manifest --kernel ../kernel/unix.bin --inodes $(DISK_INODES)

../kernel/unix.bin: FORCE
	$(MAKE) -s -C ../kernel

FORCE:

$(B)/libc.a: $(OBJS)
	$(AR) $@ $(OBJS)

$(B)/%.o: libc/%.c $(wildcard include/*.h include/sys/*.h) ../cc/*.py | $(B)
	$(CC) -c $< -o $@

$(B)/%.o: libc/%.s | $(B)
	$(AS) $< -o $@

$(B)/sa/crt0_sa.o: sa/crt0_sa.s | $(B)
	mkdir -p $(B)/sa
	$(AS) $< -o $@

$(B):
	mkdir -p $(B)

clean:
	rm -rf $(B)

.PHONY: all clean disk testdisk FORCE
