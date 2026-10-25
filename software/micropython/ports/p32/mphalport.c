// mphalport.c -- the terminal and the clock for MicroPython on P32W Unix
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/times.h>
#ifndef CLK_TCK
#define CLK_TCK 100                     // (the host reference build)
#endif

#include "py/mphal.h"
#include "py/runtime.h"
#include "py/stream.h"
#include "shared/readline/readline.h"

// a key, or ^C if the read was interrupted (the SIGINT handler has
// already scheduled the KeyboardInterrupt); ^D at the end of the input
int mp_hal_stdin_rx_chr(void) {
    unsigned char c;
    int n = read(0, &c, 1);
    if (n == 1) {
        return c;
    }
    if (n < 0 && errno == EINTR) {
        return CHAR_CTRL_C;
    }
    return CHAR_CTRL_D;
}

mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len) {
    size_t done = 0;
    while (done < len) {
        int n = write(1, str + done, len - done);
        if (n <= 0) {
            break;
        }
        done += n;
    }
    return done;
}

// is standard input / output ready? (P32W has no poll: always)
uintptr_t mp_hal_stdio_poll(uintptr_t poll_flags) {
    return poll_flags & (MP_STREAM_POLL_RD | MP_STREAM_POLL_WR);
}

// text goes out as it is: on a terminal the kernel turns \n into \r\n
// (CRMOD), and files and pipes get Unix lines (shared/runtime/
// stdout_helpers.c's "cooked" output, for a bare serial line, is not used)
void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len) {
    mp_hal_stdout_tx_strn(str, len);
}

void mp_hal_stdout_tx_str(const char *str) {
    mp_hal_stdout_tx_strn(str, strlen(str));
}

// the clock: times() counts CLK_TCK (50) ticks a second
mp_uint_t mp_hal_ticks_ms(void) {
    struct tms t;
    return (mp_uint_t)times(&t) * (1000 / CLK_TCK);
}

mp_uint_t mp_hal_ticks_us(void) {
    return mp_hal_ticks_ms() * 1000;
}

mp_uint_t mp_hal_ticks_cpu(void) {
    struct tms t;
    times(&t);
    return t.tms_utime + t.tms_stime;
}

void mp_hal_delay_ms(mp_uint_t ms) {
    mp_uint_t start = mp_hal_ticks_ms();
    if (ms >= 1000) {
        sleep(ms / 1000);
    }
    while (mp_hal_ticks_ms() - start < ms) {
        mp_handle_pending(true);        // (^C still works)
    }
}

void mp_hal_delay_us(mp_uint_t us) {
    mp_hal_delay_ms((us + 999) / 1000);
}

// random's first seed: the date and the clock ticks since boot
unsigned int p32_random_seed(void) {
    return (unsigned int)time(0) ^ (unsigned int)mp_hal_ticks_ms();
}

uint64_t mp_hal_time_ns(void) {
    return (uint64_t)time(0) * 1000000000ULL;
}

#ifdef __P32__
// GCC's bit-counting builtins (misc.h's mp_clz, mp_ctz): the leading and
// trailing zero bits; undefined for 0, as GCC's
int __builtin_clz(unsigned int x) {
    int n = 0;
    while (!(x & 0x80000000u)) {
        x <<= 1;
        n++;
    }
    return n;
}

int __builtin_clzl(unsigned long x) {
    return __builtin_clz(x);
}

int __builtin_clzll(unsigned long long x) {
    unsigned int hi = (unsigned int)(x >> 32);
    return hi ? __builtin_clz(hi) : 32 + __builtin_clz((unsigned int)x);
}

int __builtin_ctz(unsigned int x) {
    int n = 0;
    while (!(x & 1)) {
        x >>= 1;
        n++;
    }
    return n;
}
#endif
