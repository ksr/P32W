/* arpa/inet.h -- P32W Unix: dotted-decimal addresses (D45) */
#ifndef _ARPA_INET_H
#define _ARPA_INET_H
#include <netinet/in.h>
in_addr_t inet_addr(const char *s);             /* "10.0.2.2" in network order; INADDR_NONE if it is not one */
int inet_aton(const char *s, struct in_addr *a);   /* 1, or 0 if s is not an address */
char *inet_ntoa(struct in_addr a);              /* (a static buffer) */
#endif
