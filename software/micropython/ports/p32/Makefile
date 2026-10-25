# Makefile -- MicroPython, rebuilt on P32W itself (/usr/src/micropython; D39).
# (The host builds it with ports/p32/build.sh, which also makes the tables in
# genhdr/ with MicroPython's Python scripts; they come here ready made.)
#
#   make              rebuild python from whatever changed
#   make install      as root: put it in /usr/bin (python and micropython)
#   make clean        remove the objects
#
# The objects are in build.sh's order, so python comes out byte for byte
# the host's /usr/bin/python.

CFLAGS = -w -DNDEBUG -I ports/p32 -I .

# (each object beside its source: the built-in rule would put it here)

OBJS = py/mpstate.o py/nlr.o py/nlrsetjmp.o py/malloc.o py/gc.o py/pystack.o \
       py/qstr.o py/vstr.o py/mpprint.o py/unicode.o py/mpz.o py/reader.o \
       py/lexer.o py/parse.o py/scope.o py/compile.o py/emitcommon.o py/emitbc.o \
       py/asmbase.o py/emitglue.o py/persistentcode.o py/formatfloat.o \
       py/parsenumbase.o py/parsenum.o py/runtime.o py/runtime_utils.o \
       py/scheduler.o py/nativeglue.o py/pairheap.o py/ringbuf.o py/cstack.o \
       py/stackctrl.o py/argcheck.o py/warning.o py/profile.o py/map.o py/obj.o \
       py/objarray.o py/objattrtuple.o py/objbool.o py/objboundmeth.o py/objcell.o \
       py/objclosure.o py/objcode.o py/objcomplex.o py/objdeque.o py/objdict.o \
       py/objenumerate.o py/objexcept.o py/objfilter.o py/objfloat.o py/objfun.o \
       py/objgenerator.o py/objgetitemiter.o py/objint.o py/objint_longlong.o \
       py/objint_mpz.o py/objlist.o py/objmap.o py/objmodule.o py/objobject.o \
       py/objpolyiter.o py/objproperty.o py/objnone.o py/objnamedtuple.o \
       py/objrange.o py/objreversed.o py/objringio.o py/objset.o py/objsingleton.o \
       py/objslice.o py/objstr.o py/objstrunicode.o py/objstringio.o \
       py/objtemplate.o py/objtuple.o py/objtype.o py/objzip.o py/opmethods.o \
       py/sequence.o py/stream.o py/binary.o py/builtinimport.o py/builtinevex.o \
       py/builtinhelp.o py/modarray.o py/modbuiltins.o py/modcollections.o \
       py/modgc.o py/modio.o py/modmath.o py/modcmath.o py/modmicropython.o \
       py/modstruct.o py/modsys.o py/moderrno.o py/modthread.o py/modweakref.o \
       py/vm.o py/bc.o py/showbc.o py/repl.o py/smallint.o py/frozenmod.o \
       extmod/vfs.o extmod/vfs_posix.o extmod/vfs_posix_file.o extmod/vfs_reader.o \
       extmod/modos.o extmod/modtime.o extmod/modjson.o extmod/modre.o \
       extmod/modrandom.o shared/timeutils/timeutils.o extmod/moductypes.o \
       extmod/moddeflate.o extmod/modheapq.o extmod/modhashlib.o \
       extmod/modbinascii.o extmod/modplatform.o shared/readline/readline.o \
       shared/runtime/pyexec.o shared/runtime/gchelper_generic.o \
       shared/runtime/interrupt_char.o ports/p32/main.o ports/p32/mphalport.o

python: $(OBJS)
	cc -o python $(OBJS)

%.o: %.c
	cc $(CFLAGS) -c $< -o $@

install: python
	cp python /usr/bin/python.new && mv /usr/bin/python.new /usr/bin/python
	rm -f /usr/bin/micropython && ln /usr/bin/python /usr/bin/micropython

clean:
	rm -f python $(OBJS)

.PHONY: install clean
