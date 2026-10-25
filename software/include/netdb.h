/* netdb.h -- P32W Unix: host names (D45). The coprocessor asks the DNS
 * server (netctl NETCTL_RESOLVE); there is no /etc/hosts yet. */
#ifndef _NETDB_H
#define _NETDB_H
#include <netinet/in.h>
struct hostent {
    char *h_name;
    char **h_aliases;
    int h_addrtype;                     /* AF_INET */
    int h_length;                       /* 4 */
    char **h_addr_list;
};
#define h_addr h_addr_list[0]
extern int h_errno;
#define HOST_NOT_FOUND 1
#define TRY_AGAIN      2
#define NO_RECOVERY    3
struct hostent *gethostbyname(const char *name);
const char *hstrerror(int err);
#endif
