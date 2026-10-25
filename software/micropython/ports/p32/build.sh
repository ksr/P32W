#!/bin/sh
# build.sh -- MicroPython for P32W Unix, built on the host (README.P32W):
#   sh ports/p32/build.sh [BUILD-DIR]       (from software/micropython)
# 1. the generated headers (genhdr/): the version, then the qstr, module and
#    root-pointer tables, from MicroPython's own scripts. The sources are
#    preprocessed for that scan by the host's clang (it writes the # line
#    markers the scripts need), with -undef -nostdinc so that it sees P32W's
#    headers and P32W's predefined macro only, as p32cc does;
# 2. every source compiled by p32cc; 3. linked into BUILD/micropython.
set -e
cd "$(dirname "$0")/../.."
TOP=$(pwd)
SW=$TOP/..
B=${1:-$SW/build/micropython}
CC="python3 $SW/../cc/p32cc.py -w -DNDEBUG"   # (NDEBUG: no assertions, as MicroPython's release builds)
INC="-Iports/p32 -I. -I$B -I$SW/include"    # (relative: __FILE__ is the same wherever it is built,
                                            # and as the Makefile for P32W gives it)
CPPFLAGS="-undef -nostdinc -D__P32__=1"
if [ -n "$HOST" ]; then                 # HOST=1: a reference build for the host (clang, its libc):
    CC="clang -std=c99 -w -O1 -ffp-contract=off -DNDEBUG"  # MicroPython itself, the same version and
                                    # configuration (no fused multiply-adds: P32W rounds each step),
    INC="-Iports/p32 -I. -I$B"          # to compare P32W's output with
    CPPFLAGS=""
fi
export MICROPY_GIT_TAG=v1.29.0 SOURCE_DATE_EPOCH=1787529600   # (the release, for a reproducible banner)

PY="mpstate nlr nlrsetjmp malloc gc pystack qstr vstr mpprint unicode mpz reader lexer parse scope compile
    emitcommon emitbc asmbase emitglue persistentcode formatfloat parsenumbase parsenum runtime runtime_utils
    scheduler nativeglue pairheap ringbuf cstack stackctrl argcheck warning profile map obj objarray
    objattrtuple objbool objboundmeth objcell objclosure objcode objcomplex objdeque objdict objenumerate
    objexcept objfilter objfloat objfun objgenerator objgetitemiter objint objint_longlong objint_mpz objlist
    objmap objmodule objobject objpolyiter objproperty objnone objnamedtuple objrange objreversed objringio
    objset objsingleton objslice objstr objstrunicode objstringio objtemplate objtuple objtype objzip
    opmethods sequence stream binary builtinimport builtinevex builtinhelp modarray modbuiltins
    modcollections modgc modio modmath modcmath modmicropython modstruct modsys moderrno modthread
    modweakref vm bc showbc repl smallint frozenmod"
SRC=""
for f in $PY; do SRC="$SRC py/$f.c"; done
SRC="$SRC extmod/vfs.c extmod/vfs_posix.c extmod/vfs_posix_file.c extmod/vfs_reader.c extmod/modos.c
     extmod/modtime.c extmod/modjson.c extmod/modre.c extmod/modrandom.c shared/timeutils/timeutils.c
     extmod/moductypes.c extmod/moddeflate.c extmod/modheapq.c extmod/modhashlib.c extmod/modbinascii.c
     extmod/modplatform.c
     shared/readline/readline.c shared/runtime/pyexec.c
     shared/runtime/gchelper_generic.c shared/runtime/interrupt_char.c
     ports/p32/main.c ports/p32/mphalport.c"

mkdir -p $B/genhdr $B/obj
G=$B/genhdr
CPP="clang -E $CPPFLAGS $INC"
echo "GEN $G/mpversion.h"
python3 py/makeversionhdr.py $G/mpversion.h
echo "GEN the qstr, module and root-pointer tables"
python3 py/makeqstrdefs.py pp $CPP output $G/qstr.i.last cflags -DNO_QSTR cxxflags sources $SRC \
    dependencies changed_sources $SRC
for k in qstr module root_pointer; do
    python3 py/makeqstrdefs.py split $k $G/qstr.i.last $G/$k _
    python3 py/makeqstrdefs.py cat $k _ $G/$k $G/$k.collected
done
cat py/qstrdefs.h ports/p32/qstrdefsport.h $G/qstr.collected | sed 's/^Q(.*)/"&"/' \
    | $CPP - | sed 's/^\"\(Q(.*)\)\"/\1/' > $G/qstrdefs.preprocessed.h
python3 py/makeqstrdata.py $G/qstrdefs.preprocessed.h > $G/qstrdefs.generated.h
python3 py/makemoduledefs.py $G/module.collected > $G/moduledefs.h
python3 py/make_root_pointers.py $G/root_pointer.collected > $G/root_pointers.h
[ -n "$GEN_ONLY" ] && exit 0

echo "CC (p32cc) $(echo $SRC | wc -w) files"
OBJ=""; bad=0
for f in $SRC; do                       # (KEEP_GOING=1: report every file that fails)
    o=$B/obj/$(echo $f | tr / _ | sed 's/\.c$/.o/')
    if ! $CC $INC -c $f -o $o; then
        echo "FAILED: $f"; bad=1
        [ -n "$KEEP_GOING" ] || exit 1
    fi
    OBJ="$OBJ $o"
done
[ $bad = 0 ] || exit 1
echo "LINK $B/micropython"
$CC $OBJ -o $B/micropython $([ -n "$HOST" ] && echo -lm)
ls -l $B/micropython
