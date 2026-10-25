/* inet.c -- P32W C library: dotted-decimal addresses and host names (D45).
 * gethostbyname asks the coprocessor (netctl NETCTL_RESOLVE), which asks
 * the DNS server; a dotted address is taken as it is. */
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/netctl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

int inet_aton(const char *s, struct in_addr *a)
{
    unsigned v = 0, part;
    int k, digits;
    for (k = 0; k < 4; k++) {
        part = 0;
        for (digits = 0; *s >= '0' && *s <= '9'; s++, digits++) {
            part = part * 10 + (*s - '0');
            if (part > 255) return 0;
        }
        if (!digits) return 0;
        v = v << 8 | part;
        if (k < 3 && *s++ != '.') return 0;
    }
    if (*s) return 0;
    if (a) a->s_addr = htonl(v);
    return 1;
}

in_addr_t inet_addr(const char *s)
{
    struct in_addr a;
    return inet_aton(s, &a) ? a.s_addr : INADDR_NONE;
}

char *inet_ntoa(struct in_addr a)
{
    static char buf[16];
    unsigned v = ntohl(a.s_addr);
    sprintf(buf, "%u.%u.%u.%u", v >> 24, v >> 16 & 255, v >> 8 & 255, v & 255);
    return buf;
}

int h_errno;

struct hostent *gethostbyname(const char *name)
{
    static struct hostent h;
    static char hname[256];
    static unsigned addr;
    static char *list[2], *aliases[1];
    struct netresolve r;
    struct in_addr a;
    if (inet_aton(name, &a)) addr = a.s_addr;
    else {
        r.name = name;
        r.addr = 0;
        if (netctl(NETCTL_RESOLVE, &r) < 0) {
            h_errno = errno == ENOENT ? HOST_NOT_FOUND : errno == EAGAIN || errno == ETIMEDOUT ? TRY_AGAIN : NO_RECOVERY;
            return 0;
        }
        addr = r.addr;
    }
    strncpy(hname, name, sizeof hname - 1);
    list[0] = (char *)&addr;
    list[1] = 0;
    aliases[0] = 0;
    h.h_name = hname;
    h.h_aliases = aliases;
    h.h_addrtype = AF_INET;
    h.h_length = 4;
    h.h_addr_list = list;
    return &h;
}

const char *hstrerror(int err)
{
    return err == HOST_NOT_FOUND ? "host not found" : err == TRY_AGAIN ? "try again later" :
           err == NO_RECOVERY ? "the name server failed" : "resolver error";
}
