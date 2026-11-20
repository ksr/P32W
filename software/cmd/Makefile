# Makefile -- the commands of P32W Unix, rebuilt on P32W itself (/usr/src/cmd).
# (The host builds them with ../Makefile; this one is for make on P32W.)
#
#   make              rebuild every command whose source changed (make all)
#   make ls           just ls
#   make install      as root: put the rebuilt ones in /bin and /etc
#   make clean        remove the programs built here
#
# The disk comes with every program already built here, so a first make
# only rebuilds what you have edited.

CC = cc
CFLAGS =

# in /bin (755), /bin setuid root (4755), /bin setgid tty or mail (2755),
# /etc (755), /etc root only (700)
BIN = sh ls cat echo halt sync true false mkdir rmdir rm ln mv cp pwd wc grep \
      kill ps chmod chown date sleep head tail tee touch sort uniq stty uname \
      login who id ed od cmp more find du basename dirname whoami test expr \
      time cycles uptime shutdown reboot tar gzip zip unzip dos ifconfig ping telnet fetch ftp kermit as ld ar vi top \
      man make basic db df at tr cut paste join comm xargs sed diff bc chgrp mesg yacc awk lex wm win \
      clock files finder edit paint sheet view
SUID = su passwd
SGID_TTY = write wall
SGID_MAIL = mail
ETC = init getty mount mkfs mknod ntpdate
ETCPRIV = fsck cron adduser telnetd
PROGS = $(BIN) $(SUID) $(SGID_TTY) $(SGID_MAIL) $(ETC) $(ETCPRIV)

all: $(PROGS)

# each program is one .c file (make's built-in rule: cc -o ls ls.c);
# these also read the object format's header
as ld ar: objfmt.h

# sed is two files
sed: sed.c sedre.c sed.h
	$(CC) $(CFLAGS) -o sed sed.c sedre.c

# awk is eight files (awk.h lists them)
awk: awk.c awklex.c awkparse.c awkrun.c awkvar.c awkfn.c awknum.c awkre.c awk.h
	$(CC) $(CFLAGS) -o awk awk.c awklex.c awkparse.c awkrun.c awkvar.c awkfn.c awknum.c awkre.c

# lex is four files
lex: lex.c lexre.c lexdfa.c lexout.c lex.h
	$(CC) $(CFLAGS) -o lex lex.c lexre.c lexdfa.c lexout.c

# yacc is three files
yacc: yacc.c ylr.c yout.c yacc.h
	$(CC) $(CFLAGS) -o yacc yacc.c ylr.c yout.c

# bc is three files
bc: bc.c bcexec.c bcnum.c bc.h
	$(CC) $(CFLAGS) -o bc bc.c bcexec.c bcnum.c

# wm is four files
wm: wm.c wmwin.c wmterm.c wmgfx.c wmwin.h wmterm.h wmgfx.h
	$(CC) $(CFLAGS) -o wm wm.c wmwin.c wmterm.c wmgfx.c -lgfx

# the desk's programs draw with the graphics library
clock: clock.c
	$(CC) $(CFLAGS) -o clock clock.c -lgfx
files: files.c
	$(CC) $(CFLAGS) -o files files.c -lgfx
finder: finder.c
	$(CC) $(CFLAGS) -o finder finder.c -lgfx
edit: edit.c
	$(CC) $(CFLAGS) -o edit edit.c -lgfx
paint: paint.c
	$(CC) $(CFLAGS) -o paint paint.c -lgfx
sheet: sheet.c
	$(CC) $(CFLAGS) -o sheet sheet.c -lgfx
view: view.c
	$(CC) $(CFLAGS) -o view view.c -lgfx

# BASIC is four files (cc compiles each in a process of its own)
basic: basic.c bexpr.c bio.c bgfx.c basic.h
	$(CC) $(CFLAGS) -o basic basic.c bexpr.c bio.c bgfx.c -lsql -lgfx

install: all
	@echo installing what differs from the running system:
	@for p in $(BIN); do if cmp $$p /bin/$$p >/dev/null; then :; else cp $$p /bin/$$p.new && chmod 755 /bin/$$p.new && mv /bin/$$p.new /bin/$$p && echo installed /bin/$$p; fi; done
	@for p in $(SUID); do if cmp $$p /bin/$$p >/dev/null; then :; else cp $$p /bin/$$p.new && chmod 4755 /bin/$$p.new && mv /bin/$$p.new /bin/$$p && echo installed /bin/$$p; fi; done
	@for p in $(SGID_TTY); do if cmp $$p /bin/$$p >/dev/null; then :; else cp $$p /bin/$$p.new && chgrp tty /bin/$$p.new && chmod 2755 /bin/$$p.new && mv /bin/$$p.new /bin/$$p && echo installed /bin/$$p; fi; done
	@for p in $(SGID_MAIL); do if cmp $$p /bin/$$p >/dev/null; then :; else cp $$p /bin/$$p.new && chgrp mail /bin/$$p.new && chmod 2755 /bin/$$p.new && mv /bin/$$p.new /bin/$$p && echo installed /bin/$$p; fi; done
	@for p in $(ETC); do if cmp $$p /etc/$$p >/dev/null; then :; else cp $$p /etc/$$p.new && chmod 755 /etc/$$p.new && mv /etc/$$p.new /etc/$$p && echo installed /etc/$$p; fi; done
	@for p in $(ETCPRIV); do if cmp $$p /etc/$$p >/dev/null; then :; else cp $$p /etc/$$p.new && chmod 700 /etc/$$p.new && mv /etc/$$p.new /etc/$$p && echo installed /etc/$$p; fi; done
	@rm -f /bin/gunzip /bin/zcat /bin/atq /bin/atrm /bin/[ /etc/umount /bin/dosdir /bin/doscp /bin/dosrm /bin/dosmkdir /bin/dosformat
	@ln /bin/gzip /bin/gunzip; ln /bin/gzip /bin/zcat; ln /bin/at /bin/atq; ln /bin/at /bin/atrm
	@ln /bin/test /bin/[; ln /etc/mount /etc/umount
	@for p in dosdir doscp dosrm dosmkdir dosformat; do ln /bin/dos /bin/$$p; done

clean:
	rm -f $(PROGS) *.o

.PHONY: all install clean
