// mpconfigport.h -- MicroPython's configuration for P32W Unix (README.P32W).
// A 32-bit machine with no FPU: ints are 32 bits, floats are IEEE doubles
// in software (softfp, D27/D30), big integers are arbitrary precision.
#include <stdint.h>

#define MICROPY_CONFIG_ROM_LEVEL            (MICROPY_CONFIG_ROM_LEVEL_EXTRA_FEATURES)   // (as the Unix port)
#define MICROPY_PY_SELECT                   (0)     // (P32W has no poll)
#define MICROPY_PY_ASYNCIO                  (0)     // (needs its frozen Python library: later)
#define MICROPY_PY_FRAMEBUF                 (0)     // (no display yet)

#define MICROPY_ENABLE_COMPILER             (1)
#define MICROPY_ENABLE_GC                   (1)
#define MICROPY_GCREGS_SETJMP               (1)     // the callee-saved registers, by setjmp
#define MICROPY_NLR_SETJMP                  (1)     // exceptions: setjmp / longjmp
#define MICROPY_ENABLE_PYSTACK              (1)     // Python frames on a stack of their own: no alloca
#define MICROPY_STACKLESS                   (1)     // a Python call to Python does not recurse in C: its
                                                    // depth is the Python stack's, not C's 1 MiB (main.c)
#define MICROPY_STACK_CHECK                 (1)
#define MICROPY_HELPER_REPL                 (1)
#define MICROPY_REPL_AUTO_INDENT            (1)
#define MICROPY_REPL_EMACS_KEYS             (1)
#define MICROPY_KBD_EXCEPTION               (1)     // ^C raises KeyboardInterrupt
#define MICROPY_ENABLE_EXTERNAL_IMPORT      (1)
#define MICROPY_VFS                         (1)     // files: the VFS with P32W's own filesystem
#define MICROPY_VFS_POSIX                   (1)     //  mounted at / (main.c), as the Unix port
#define MICROPY_READER_VFS                  (1)
#define MICROPY_READER_POSIX                (1)     // source files through open / read
#define MICROPY_HELPER_LEXER_UNIX           (1)     // (and a program from stdin: mp_lexer_new_from_fd)
#define MICROPY_ALLOC_PATH_MAX              (256)
#define MICROPY_ENABLE_SOURCE_LINE          (1)
#define MICROPY_ERROR_REPORTING             (MICROPY_ERROR_REPORTING_NORMAL)
#define MICROPY_LONGINT_IMPL                (MICROPY_LONGINT_IMPL_MPZ)
#define MICROPY_FLOAT_IMPL                  (MICROPY_FLOAT_IMPL_DOUBLE)
#define MICROPY_PY_BUILTINS_FLOAT           (1)
#define MICROPY_PY_MATH                     (1)
#define MICROPY_PY_CMATH                    (1)
#define MICROPY_PY_SYS                      (1)
#define MICROPY_PY_SYS_ARGV                 (1)
#define MICROPY_PY_SYS_EXIT                 (1)
#define MICROPY_PY_SYS_PATH                 (1)
#define MICROPY_PY_SYS_PLATFORM             "p32"
#define MICROPY_PY_IO                       (1)     // open(), files, io.StringIO
#define MICROPY_PY_OS                       (1)     // os: listdir, stat, remove, rename, mkdir, getcwd, ...
#define MICROPY_PY_OS_STATVFS               (0)     // (P32W has no statvfs)
#define MICROPY_PY_OS_INCLUDEFILE           "ports/p32/modos.c"
#define MICROPY_PY_OS_GETENV_PUTENV_UNSETENV (1)    // os.getenv, putenv, unsetenv (modos.c)
#define MICROPY_PY_OS_SYSTEM                (1)     // os.system (modos.c)
#define MICROPY_PY_SYS_STDFILES             (1)     // sys.stdin, sys.stdout, sys.stderr
#define MICROPY_EPOCH_IS_1970               (1)     // os.stat's times: Unix seconds
#define MICROPY_PY_TIME                     (1)     // time: time, sleep, localtime, mktime, ticks_ms, ...
#define MICROPY_PY_TIME_TIME_TIME_NS        (1)
#define MICROPY_PY_TIME_INCLUDEFILE         "ports/p32/modtime.c"
#define MICROPY_PY_JSON                     (1)
#define MICROPY_PY_RE                       (1)     // (lib/re1.5)
#define MICROPY_PY_RE_MATCH_GROUPS          (1)     // match.groups()
#define MICROPY_PY_RE_MATCH_SPAN_START_END  (1)     // match.start(), end(), span()
#define MICROPY_PY_RANDOM                   (1)
#define MICROPY_PY_RANDOM_EXTRA_FUNCS       (1)     // randint, choice, uniform, ...
#define MICROPY_PY_RANDOM_SEED_INIT_FUNC    (p32_random_seed())     // (mphalport.c: the time and clock)
unsigned int p32_random_seed(void);
#define MICROPY_PY_BUILTINS_INPUT           (1)
#define MICROPY_USE_INTERNAL_PRINTF         (0)     // libc's printf
#define MICROPY_ENABLE_FINALISER            (1)     // (the GC closes files left open: VFS_POSIX needs it)

#define MICROPY_HW_BOARD_NAME               "P32W"
#define MICROPY_HW_MCU_NAME                 "P32W"

#define MP_ENDIANNESS_LITTLE                (1)     // P32W is little-endian

// GCC's attributes and builtins: P32W's compilers have none, so plain C
#define MP_NORETURN
#define MP_WEAK
#define MP_NOINLINE
#define MP_ALWAYSINLINE
#define MP_LIKELY(x)                        (x)
#define MP_UNLIKELY(x)                      (x)

// misc.h's mp_clz / mp_ctz are GCC's builtins: on P32W, functions (mphalport.c)
#ifdef __P32__
int __builtin_clz(unsigned int x);
int __builtin_clzl(unsigned long x);
int __builtin_clzll(unsigned long long x);
int __builtin_ctz(unsigned int x);
#endif

// type definitions for the machine
typedef intptr_t mp_int_t;                  // as wide as a pointer (int on P32W; the host
typedef uintptr_t mp_uint_t;                //  reference build, HOST=1, is 64-bit)
typedef long mp_off_t;

#define MP_STATE_PORT MP_STATE_VM

// P32W has sync() but no fsync(): a file's flush syncs everything
#ifdef __P32__
#define fsync(fd) ((void)(fd), sync(), 0)
#endif
