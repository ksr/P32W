/* sys/time.h -- P32W Unix: struct timeval (D45: select's timeout) */
#ifndef _SYS_TIME_H
#define _SYS_TIME_H
#include <sys/types.h>
struct timeval { time_t tv_sec; int tv_usec; };
#endif
