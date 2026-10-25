// mphalport.h -- MicroPython's hardware layer for P32W Unix: the terminal
// and the clock come from the kernel's system calls (mphalport.c).
#include <unistd.h>

mp_uint_t mp_hal_ticks_ms(void);
mp_uint_t mp_hal_ticks_us(void);
mp_uint_t mp_hal_ticks_cpu(void);
void mp_hal_delay_ms(mp_uint_t ms);
void mp_hal_delay_us(mp_uint_t us);
#include "shared/runtime/interrupt_char.h"  // (mp_hal_set_interrupt_char; ^C is SIGINT here: main.c)
static inline void mp_hal_delay_us_fast(mp_uint_t us) { mp_hal_delay_us(us); }

// a system call, again after EINTR (a signal: ^C raises KeyboardInterrupt
// here); any other failure runs raise, with err set (as the Unix port)
#include <errno.h>
#define MP_HAL_RETRY_SYSCALL(ret, syscall, raise) { \
        for (;;) { \
            ret = syscall; \
            if (ret == -1) { \
                int err = errno; \
                if (err == EINTR) { \
                    mp_handle_pending(MP_HANDLE_PENDING_CALLBACKS_AND_EXCEPTIONS); \
                    continue; \
                } \
                raise; \
            } \
            break; \
        } \
}
