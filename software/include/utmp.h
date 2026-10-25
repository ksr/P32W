/* utmp.h -- who is logged in where: /etc/utmp, one record per terminal (by number) */
#ifndef _UTMP_H
#define _UTMP_H
struct utmp {
    char ut_line[8];            /* tty0 */
    char ut_name[8];            /* empty: nobody */
    int ut_time;
};
#define UTMP "/etc/utmp"
/* a terminal's record: tty0-tty3 (the UARTs) and tty4 (the video card's
   screen) are 0 to 4, the pseudo-terminals ttyp0-ttyp3 5 to 8 (D46, D50);
   -1 for any other name */
#define utmp_slot(l) ((l)[0] == 't' && (l)[1] == 't' && (l)[2] == 'y' ? \
    ((l)[3] >= '0' && (l)[3] <= '9' && !(l)[4] ? (l)[3] - '0' : \
     (l)[3] == 'p' && (l)[4] >= '0' && (l)[4] <= '9' && !(l)[5] ? 5 + (l)[4] - '0' : -1) : -1)
#endif
