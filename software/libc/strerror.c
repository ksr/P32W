/* strerror.c -- P32W C library (M2, D22) */
#include <string.h>
#include <stdio.h>
static char *msgs[] = {
    "no error", "not owner", "no such file or directory", "no such process",
    "interrupted system call", "I/O error", "no such device or address",
    "argument list too long", "exec format error", "bad file number",
    "no child processes", "no more processes", "not enough memory",
    "permission denied", "bad address", "block device required",
    "device busy", "file exists", "cross-device link", "no such device",
    "not a directory", "is a directory", "invalid argument", "file table overflow",
    "too many open files", "not a terminal", "text file busy", "file too large",
    "no space left on device", "illegal seek", "read-only file system",
    "too many links", "broken pipe", "argument out of domain", "result too large",
    "error 35", "file name too long", "error 37", "function not implemented",
    "directory not empty",
};
static char *net[] = {                  /* 88-115: networking's (errno.h) */
    "not a socket", "destination address required", "message too long",
    "wrong protocol type", "protocol not available", "protocol not supported", 0,
    "operation not supported", 0, "address family not supported", "address in use", 0,
    "network is down", 0, 0, "connection aborted", "connection reset", "no buffer space",
    "already connected", "not connected", 0, 0, "timed out", "connection refused", 0,
    "host unreachable", "operation already in progress", "operation in progress",
};
char *strerror(int e)
{
    static char buf[24];
    if (e >= 0 && e < (int)(sizeof msgs / sizeof msgs[0])) return msgs[e];
    if (e >= 88 && e <= 115 && net[e - 88]) return net[e - 88];
    sprintf(buf, "error %d", e);
    return buf;
}
