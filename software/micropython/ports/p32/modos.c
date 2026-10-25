// modos.c -- the os module's P32W half (included by extmod/modos.c through
// MICROPY_PY_OS_INCLUDEFILE): the environment and system(), as MicroPython's
// Unix port has them.
#include <errno.h>
#include <stdlib.h>
#include "py/runtime.h"

// getenv(key, default=None): the variable's value, else default
static mp_obj_t mp_os_getenv(size_t n_args, const mp_obj_t *args) {
    const char *s = getenv(mp_obj_str_get_str(args[0]));
    if (s == NULL) {
        return n_args > 1 ? args[1] : mp_const_none;
    }
    return mp_obj_new_str_from_cstr(s);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mp_os_getenv_obj, 1, 2, mp_os_getenv);

static mp_obj_t mp_os_putenv(mp_obj_t key, mp_obj_t value) {
    if (setenv(mp_obj_str_get_str(key), mp_obj_str_get_str(value), 1) != 0) {
        mp_raise_OSError(errno);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(mp_os_putenv_obj, mp_os_putenv);

static mp_obj_t mp_os_unsetenv(mp_obj_t key) {
    unsetenv(mp_obj_str_get_str(key));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mp_os_unsetenv_obj, mp_os_unsetenv);

// system(command): sh -c command; its wait status
static mp_obj_t mp_os_system(mp_obj_t cmd) {
    int r;
    MP_THREAD_GIL_EXIT();
    r = system(mp_obj_str_get_str(cmd));
    MP_THREAD_GIL_ENTER();
    return MP_OBJ_NEW_SMALL_INT(r);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mp_os_system_obj, mp_os_system);
