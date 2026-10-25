/* netinet/in.h -- P32W Unix: Internet addresses (D45). Addresses and ports
 * are kept in network order (big-endian); P32W is little-endian, so htons
 * and htonl swap. */
#ifndef _NETINET_IN_H
#define _NETINET_IN_H
#include <sys/socket.h>

typedef unsigned in_addr_t;
typedef unsigned short in_port_t;
struct in_addr { in_addr_t s_addr; };

struct sockaddr_in {
    sa_family_t sin_family;             /* AF_INET */
    in_port_t sin_port;                 /* network order */
    struct in_addr sin_addr;            /* network order */
    char sin_zero[8];
};

#define IPPROTO_IP   0
#define IPPROTO_ICMP 1
#define IPPROTO_TCP  6
#define IPPROTO_UDP  17
#define IPPROTO_TLS  253                /* P32W: TCP with TLS, done by the coprocessor (RFC 3692's experimental number) */
#define TLS_HOSTNAME 1                  /* setsockopt(fd, IPPROTO_TLS, TLS_HOSTNAME, name, strlen(name)): before connect */

#define INADDR_ANY       0x00000000u
#define INADDR_LOOPBACK  0x7F000001u
#define INADDR_BROADCAST 0xFFFFFFFFu
#define INADDR_NONE      0xFFFFFFFFu

unsigned short htons(unsigned short v);
unsigned short ntohs(unsigned short v);
unsigned htonl(unsigned v);
unsigned ntohl(unsigned v);
#endif
