// main.c -- MicroPython for P32W Unix: python [file [args] | -c code]
//
//   python              the REPL on a terminal; from a pipe or a file, the
//                       standard input is run as a program
//   python file args    run file, with sys.argv = [file, args...]
//   python -c code args run code
//
// The exit status is a SystemExit's code, 1 after any other uncaught
// exception, else 0. The Python heap is HEAP bytes from malloc; Python
// frames live on a stack of their own (MICROPY_ENABLE_PYSTACK: no alloca),
// PYSTACK bytes from malloc. Calls from Python to Python do not recurse in
// C (MICROPY_STACKLESS), so the Python stack sets how deep a recursion
// goes. When memory is short (a machine with less RAM fitted, D52) each is
// halved until it fits, down to HEAP_MIN and PYSTACK_MIN: the heap first,
// and a smaller one if the biggest leaves no room for the Python stack.
// Both cost only the pages they use (the kernel makes heap pages on
// first use, D37).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#ifdef __P32__
#include <sgtty.h>
#endif

#include "py/builtin.h"
#include "py/compile.h"
#include "py/runtime.h"
#include "py/repl.h"
#include "py/gc.h"
#include "py/lexer.h"
#include "py/stackctrl.h"
#include "py/mperrno.h"
#include "shared/runtime/gchelper.h"
#include "extmod/vfs.h"
#include "extmod/vfs_posix.h"
#include "shared/runtime/pyexec.h"

#define HEAP     (2 * 1024 * 1024)      // the Python heap (malloc)
#define PYSTACK  (512 * 1024)           // Python frames
#define HEAP_MIN (256 * 1024)
#define PYSTACK_MIN (64 * 1024)

// n bytes from malloc, or half as many, and so on down to min; *got is the size
static void *grab(size_t n, size_t min, size_t *got) {
    void *p;
    for (;;) {
        if ((p = malloc(n)) != NULL || n / 2 < min) {
            *got = n;
            return p;
        }
        n /= 2;
    }
}
#define CSTACK   (960 * 1024)           // how deep C may go (the kernel allows 1 MiB, D41)

// ^C: KeyboardInterrupt at the next bytecode; the terminal is in CBREAK
// mode, so the key is a signal even while the REPL reads a line
static void on_sigint(int sig) {
    (void)sig;
    signal(SIGINT, on_sigint);
    mp_sched_keyboard_interrupt();
}

#ifdef __P32__
static struct sgttyb saved_tty;
static int tty_changed;

static void tty_cbreak(void) {
    struct sgttyb t;
    if (ioctl(0, TIOCGETP, &saved_tty) < 0) {
        return;
    }
    t = saved_tty;
    t.sg_flags = (t.sg_flags & ~(ECHO | RAW)) | CBREAK;
    ioctl(0, TIOCSETP, &t);
    tty_changed = 1;
}

static void tty_restore(void) {
    if (tty_changed) {
        ioctl(0, TIOCSETP, &saved_tty);
        tty_changed = 0;
    }
}
#else                                   // (the host reference build: no terminal modes)
static void tty_cbreak(void) {
}
static void tty_restore(void) {
}
#endif

// an uncaught exception: SystemExit's code, else print it and 1
static int uncaught(mp_obj_t exc) {
    if (mp_obj_is_subclass_fast(MP_OBJ_FROM_PTR(mp_obj_get_type(exc)), MP_OBJ_FROM_PTR(&mp_type_SystemExit))) {
        mp_obj_t code = mp_obj_exception_get_value(exc);
        if (code == mp_const_none) {
            return 0;
        }
        if (mp_obj_is_int(code)) {
            return mp_obj_get_int(code) & 255;
        }
        mp_obj_print(code, PRINT_STR);
        mp_print_str(&mp_plat_print, "\n");
        return 1;
    }
    mp_obj_print_exception(&mp_plat_print, exc);
    return 1;
}

// compile and run a lexer's program as a module (a file, -c, stdin)
static int run(mp_lexer_t *lex) {
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        qstr source_name = lex->source_name;
        mp_parse_tree_t tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
        mp_obj_t fun = mp_compile(&tree, source_name, false);
        mp_call_function_0(fun);
        nlr_pop();
        return 0;
    }
    return uncaught(MP_OBJ_FROM_PTR(nlr.ret_val));
}

static mp_lexer_t *lexer_for(const char *file) {
    nlr_buf_t nlr;
    mp_lexer_t *lex = NULL;
    if (nlr_push(&nlr) == 0) {
        lex = mp_lexer_new_from_file(qstr_from_str(file));
        nlr_pop();
    } else {
        fprintf(stderr, "python: can't open file '%s'\n", file);
    }
    return lex;
}

int main(int argc, char **argv) {
    int status = 0, i, first = 1;
    size_t heapsz, pystacksz = 0;
    void *heap = NULL;
    mp_obj_t *pystack = NULL;
    for (heapsz = HEAP; heapsz >= HEAP_MIN; heapsz /= 2) {     // the biggest heap that leaves room for a Python stack
        if (!(heap = malloc(heapsz))) continue;
        if ((pystack = grab(PYSTACK, PYSTACK_MIN, &pystacksz)) != NULL) break;
        free(heap);
        heap = NULL;
    }
    mp_stack_ctrl_init();
    mp_stack_set_limit(CSTACK);
    if (!heap || !pystack) {
        fprintf(stderr, "python: no memory for the heap\n");
        return 1;
    }
    gc_init(heap, (char *)heap + heapsz);
    mp_pystack_init(pystack, pystack + pystacksz / sizeof(mp_obj_t));
    mp_init();
    {                                   // P32W's filesystem at the root of the VFS (as the Unix port)
        mp_obj_t args[2] = {
            MP_OBJ_TYPE_GET_SLOT(&mp_type_vfs_posix, make_new)(&mp_type_vfs_posix, 0, 0, NULL),
            MP_OBJ_NEW_QSTR(MP_QSTR__slash_),
        };
        mp_vfs_mount(2, args, (mp_map_t *)&mp_const_empty_map);
        MP_STATE_VM(vfs_cur) = MP_STATE_VM(vfs_mount_table);
        while (MP_STATE_VM(vfs_cur)->next != NULL) {
            MP_STATE_VM(vfs_cur) = MP_STATE_VM(vfs_cur)->next;
        }
    }
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(qstr_from_str("/usr/lib/micropython")));
    signal(SIGINT, on_sigint);

    if (argc > 2 && !strcmp(argv[1], "-c")) {
        first = 3;                      // sys.argv: ['-c', args...], as CPython's
        mp_obj_list_append(mp_sys_argv, MP_OBJ_NEW_QSTR(MP_QSTR__hyphen_c));
    }
    for (i = first; i < argc; i++) {    // (strings, not qstrs: those stop at 255 bytes)
        mp_obj_list_append(mp_sys_argv, mp_obj_new_str_from_cstr(argv[i]));
    }
    if (argc > 2 && !strcmp(argv[1], "-c")) {
        status = run(mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, argv[2], strlen(argv[2]), 0));
    } else if (argc > 1 && argv[1][0] == '-') {
        fprintf(stderr, "usage: python [file [arg ...] | -c code [arg ...]]\n");
        status = 2;
    } else if (argc > 1) {
        mp_lexer_t *lex = lexer_for(argv[1]);
        status = lex ? run(lex) : 2;
    } else if (isatty(0)) {
        tty_cbreak();
        pyexec_friendly_repl();
        tty_restore();
    } else {
        status = run(mp_lexer_new_from_fd(MP_QSTR__lt_stdin_gt_, 0, false));
    }
    tty_restore();
    mp_deinit();
    return status;
}

void gc_collect(void) {
    gc_collect_start();
    gc_helper_collect_regs_and_stack();
    gc_collect_end();
}

#if !MICROPY_VFS                        // (with the VFS, it answers this)
mp_import_stat_t mp_import_stat(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return (st.st_mode & S_IFMT) == S_IFDIR ? MP_IMPORT_STAT_DIR : MP_IMPORT_STAT_FILE;
    }
    return MP_IMPORT_STAT_NO_EXIST;
}
#endif

void nlr_jump_fail(void *val) {
    (void)val;
    tty_restore();
    fprintf(stderr, "python: an exception with nowhere to go\n");
    exit(1);
}
