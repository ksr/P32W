/* ifconfig -- the network interface (D45): the network coprocessor's
 * address, netmask, gateway and DNS server, and its link.
 *
 *   ifconfig                                  show them
 *   ifconfig dhcp                             (root) get them by DHCP
 *   ifconfig ADDR [netmask M] [gateway G] [dns D]   (root) set them
 *
 * A static setting keeps whatever is not given: netmask 255.255.255.0 if
 * there was none, and the gateway and DNS server as they were. */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/netctl.h>
#include <arpa/inet.h>

static char *dot(unsigned a)
{
    struct in_addr x;
    x.s_addr = a;
    return inet_ntoa(x);
}

static int show(void)
{
    struct netinfo ni;
    if (netctl(NETCTL_INFO, &ni) < 0) {
        fprintf(stderr, "ifconfig: %s%s\n", strerror(errno), errno == ENETDOWN ? " (no network coprocessor)" : "");
        return 1;
    }
    printf("net0: %s%s%s  mac %02x:%02x:%02x:%02x:%02x:%02x\n", ni.flags & NETF_UP ? "UP" : "DOWN",
           ni.flags & NETF_ADDR ? "" : ", no address", ni.flags & NETF_DHCP ? ", DHCP" : "",
           ni.mac[0], ni.mac[1], ni.mac[2], ni.mac[3], ni.mac[4], ni.mac[5]);
    printf("      inet %s", dot(ni.addr));
    printf(" netmask %s", dot(ni.mask));
    printf(" gateway %s", dot(ni.gateway));
    printf(" dns %s\n", dot(ni.dns));
    return 0;
}

static unsigned addr(const char *s)
{
    struct in_addr a;
    if (!inet_aton(s, &a)) { fprintf(stderr, "ifconfig: %s: not an address (a.b.c.d)\n", s); return 0xFFFFFFFF; }
    return a.s_addr;
}

int main(int argc, char **argv)
{
    struct netconfig nc;
    struct netinfo ni;
    int i;
    if (argc == 1) return show();
    memset(&nc, 0, sizeof nc);
    if (argc == 2 && !strcmp(argv[1], "dhcp")) nc.dhcp = 1;
    else {
        if (netctl(NETCTL_INFO, &ni) == 0) { nc.mask = ni.mask; nc.gateway = ni.gateway; nc.dns = ni.dns; }
        if (!nc.mask) nc.mask = htonl(0xFFFFFF00);
        if ((nc.addr = addr(argv[1])) == 0xFFFFFFFF) return 1;
        for (i = 2; i + 1 < argc; i += 2) {
            unsigned *p = !strcmp(argv[i], "netmask") ? &nc.mask : !strcmp(argv[i], "gateway") ? &nc.gateway :
                          !strcmp(argv[i], "dns") ? &nc.dns : 0;
            if (!p) break;
            if ((*p = addr(argv[i + 1])) == 0xFFFFFFFF) return 1;
        }
        if (i != argc) {
            fprintf(stderr, "usage: ifconfig [dhcp | ADDR [netmask M] [gateway G] [dns D]]\n");
            return 2;
        }
    }
    if (netctl(NETCTL_CONFIG, &nc) < 0) {
        fprintf(stderr, "ifconfig: %s\n", strerror(errno));
        return 1;
    }
    return show();
}
