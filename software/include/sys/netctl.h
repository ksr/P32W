/* sys/netctl.h -- P32W Unix: the network interface and names (D45):
 * netctl(cmd, arg), for ifconfig, the resolver and the boot scripts. */
#ifndef _SYS_NETCTL_H
#define _SYS_NETCTL_H
#define NETCTL_INFO    1        /* arg: struct netinfo *, filled */
#define NETCTL_CONFIG  2        /* arg: struct netconfig * (root only) */
#define NETCTL_RESOLVE 3        /* arg: struct netresolve *: name in, address out */

struct netinfo {                /* addresses in network order */
    unsigned char flags;        /* NETF_UP | NETF_ADDR | NETF_DHCP */
    unsigned char pad;
    unsigned char mac[6];
    unsigned addr, mask, gateway, dns;
};
#define NETF_UP   1             /* the link is up */
#define NETF_ADDR 2             /* an address is configured */
#define NETF_DHCP 4             /* ... by DHCP */

struct netconfig {
    int dhcp;                   /* 1: by DHCP (the rest ignored); 0: these */
    unsigned addr, mask, gateway, dns;
};

struct netresolve {
    const char *name;           /* a host name */
    unsigned addr;              /* its address (network order) */
};

int netctl(int cmd, void *arg);
#endif
