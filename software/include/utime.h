#ifndef _UTIME_H
#define _UTIME_H
#include <time.h>
struct utimbuf { time_t actime, modtime; };     /* (P32W keeps modtime only) */
int utime(const char *path, const struct utimbuf *times);
#endif
