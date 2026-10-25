// modtime.c -- the time module's port part for P32W Unix (included by
// extmod/modtime.c, as the Unix port's): time() in whole seconds (P32W has
// no gettimeofday), sleep() by the kernel's sleep and the 50 Hz clock for
// the fraction, gmtime / localtime / mktime from libc (TZ), clock().
#include <unistd.h>
#include <time.h>
#include <string.h>
#include "py/mphal.h"
#include "py/runtime.h"
#include "shared/timeutils/timeutils.h"

static mp_obj_t mp_time_time_get(void) {
    return mp_obj_new_float((mp_float_t)time(NULL));
}

// (deprecated since CPython 3.3, but pystone uses it): CPU seconds
static mp_obj_t mod_time_clock(void) {
    return mp_obj_new_float((mp_float_t)clock() / CLOCKS_PER_SEC);
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_time_clock_obj, mod_time_clock);

static mp_obj_t mp_time_sleep(mp_obj_t arg) {
    mp_float_t val = mp_obj_get_float(arg);
    mp_float_t whole;
    mp_float_t frac = MICROPY_FLOAT_C_FUN(modf)(val, &whole);
    unsigned int left = whole > 0 ? (unsigned int)whole : 0;
    while (left) {                      // (a signal, ^C, ends a sleep early: then it raises)
        mp_handle_pending(MP_HANDLE_PENDING_CALLBACKS_AND_EXCEPTIONS);
        left = sleep(left);
    }
    if (frac > 0) {
        mp_hal_delay_ms((mp_uint_t)(frac * 1000));
    }
    mp_handle_pending(MP_HANDLE_PENDING_CALLBACKS_AND_EXCEPTIONS);
    return mp_const_none;
}

static mp_obj_t mod_time_gm_local_time(size_t n_args, const mp_obj_t *args, struct tm *(*time_func)(const time_t *timep)) {
    time_t t;
    struct tm *tm;
    mp_obj_t ret;
    mp_obj_tuple_t *tuple;
    int wday;
    if (n_args == 0) {
        t = time(NULL);
    } else {
        t = (time_t)timeutils_obj_get_timestamp(args[0]);
    }
    tm = time_func(&t);
    ret = mp_obj_new_tuple(9, NULL);
    tuple = MP_OBJ_TO_PTR(ret);
    tuple->items[0] = MP_OBJ_NEW_SMALL_INT(tm->tm_year + 1900);
    tuple->items[1] = MP_OBJ_NEW_SMALL_INT(tm->tm_mon + 1);
    tuple->items[2] = MP_OBJ_NEW_SMALL_INT(tm->tm_mday);
    tuple->items[3] = MP_OBJ_NEW_SMALL_INT(tm->tm_hour);
    tuple->items[4] = MP_OBJ_NEW_SMALL_INT(tm->tm_min);
    tuple->items[5] = MP_OBJ_NEW_SMALL_INT(tm->tm_sec);
    wday = tm->tm_wday - 1;             // (Monday 0, as MicroPython)
    if (wday < 0) {
        wday = 6;
    }
    tuple->items[6] = MP_OBJ_NEW_SMALL_INT(wday);
    tuple->items[7] = MP_OBJ_NEW_SMALL_INT(tm->tm_yday + 1);
    tuple->items[8] = MP_OBJ_NEW_SMALL_INT(tm->tm_isdst);
    return ret;
}

static mp_obj_t mod_time_gmtime(size_t n_args, const mp_obj_t *args) {
    return mod_time_gm_local_time(n_args, args, gmtime);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_time_gmtime_obj, 0, 1, mod_time_gmtime);

static mp_obj_t mod_time_localtime(size_t n_args, const mp_obj_t *args) {
    return mod_time_gm_local_time(n_args, args, localtime);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_time_localtime_obj, 0, 1, mod_time_localtime);

static mp_obj_t mod_time_mktime(mp_obj_t tuple) {
    size_t len;
    mp_obj_t *elem;
    struct tm tm;
    time_t ret;
    mp_obj_get_array(tuple, &len, &elem);
    if (len < 8 || len > 9) {
        mp_raise_TypeError(MP_ERROR_TEXT("mktime needs a tuple of length 8 or 9"));
    }
    memset(&tm, 0, sizeof tm);
    tm.tm_year = mp_obj_get_int(elem[0]) - 1900;
    tm.tm_mon = mp_obj_get_int(elem[1]) - 1;
    tm.tm_mday = mp_obj_get_int(elem[2]);
    tm.tm_hour = mp_obj_get_int(elem[3]);
    tm.tm_min = mp_obj_get_int(elem[4]);
    tm.tm_sec = mp_obj_get_int(elem[5]);
    tm.tm_isdst = len == 9 ? mp_obj_get_int(elem[8]) : -1;
    ret = mktime(&tm);
    if (ret == (time_t)-1) {
        mp_raise_msg(&mp_type_OverflowError, MP_ERROR_TEXT("invalid mktime usage"));
    }
    return timeutils_obj_from_timestamp(ret);
}
MP_DEFINE_CONST_FUN_OBJ_1(mod_time_mktime_obj, mod_time_mktime);

#define MICROPY_PY_TIME_EXTRA_GLOBALS \
    { MP_ROM_QSTR(MP_QSTR_clock), MP_ROM_PTR(&mod_time_clock_obj) }, \
    { MP_ROM_QSTR(MP_QSTR_gmtime), MP_ROM_PTR(&mod_time_gmtime_obj) }, \
    { MP_ROM_QSTR(MP_QSTR_localtime), MP_ROM_PTR(&mod_time_localtime_obj) }, \
    { MP_ROM_QSTR(MP_QSTR_mktime), MP_ROM_PTR(&mod_time_mktime_obj) },
