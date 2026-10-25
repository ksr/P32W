/* rtc.c -- the real-time clock (D23): a DS12C887 on the I/O card, MC146818
 * registers through FFFF_FF40 (the register number) and FFFF_FF41 (the
 * register). The kernel keeps it in binary, 24-hour mode, UTC; it reads it
 * once at boot (the system time is then the clock tick count on top) and
 * writes it when root sets the time (stime). A card without the chip is
 * noticed (a RAM byte does not read back) and the time starts at 1970. */
#include "defs.h"

#define IDX IOB(0xFFFFFF40)
#define DAT IOB(0xFFFFFF41)
#define REG_A 0x0A
#define REG_B 0x0B
#define REG_D 0x0D
#define CENTURY 0x32
#define PROBE 0x7F                      /* a battery-backed RAM byte, for the presence test */

static int present;

static int rd(int r) { IDX = r; return DAT; }
static void wr(int r, int v) { IDX = r; DAT = v; }

/* days since 1970-01-01 of a date, and back (H. Hinnant's algorithms) */
static int days_from_civil(int y, int m, int d)
{
    int era, yoe, doy, doe;
    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
static void civil_from_days(int z, int *y, int *m, int *d)
{
    int era, doe, yoe, doy, mp;
    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = z - era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    *y = yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp + (mp < 10 ? 3 : -9);
    *y += *m <= 2;
}

int rtc_init(void)
{
    int v;
    wr(PROBE, 0xA5);
    v = rd(PROBE);
    wr(PROBE, 0x5A);
    present = v == 0xA5 && rd(PROBE) == 0x5A;
    if (!present) return -1;
    if (!(rd(REG_D) & 0x80)) kprintf("warning: the clock's battery is flat\n");
    if ((rd(REG_A) & 0x70) != 0x20) wr(REG_A, 0x20);    /* the oscillator on (it ships off) */
    wr(REG_B, (rd(REG_B) & 0x80) | 0x06);               /* binary, 24-hour, no interrupts */
    return 0;
}

uint rtc_get(void)
{
    int s, mi, h, d, mo, y;
    if (!present) return 0;
    while (rd(REG_A) & 0x80) ;          /* an update in progress: under 2 ms */
    s = rd(0); mi = rd(2); h = rd(4); d = rd(7); mo = rd(8);
    y = rd(9) + 100 * (rd(CENTURY) ? rd(CENTURY) : 20);
    return (uint)days_from_civil(y, mo, d) * 86400u + h * 3600 + mi * 60 + s;
}

void rtc_set(uint t)
{
    int y, mo, d, days = t / 86400, sec = t % 86400;
    if (!present) return;
    civil_from_days(days, &y, &mo, &d);
    wr(REG_B, 0x86);                    /* SET: the clock stops while it is written */
    wr(0, sec % 60); wr(2, sec / 60 % 60); wr(4, sec / 3600);
    wr(6, (days + 4) % 7 + 1);          /* 1970-01-01 was a Thursday; 1 = Sunday */
    wr(7, d); wr(8, mo); wr(9, y % 100); wr(CENTURY, y / 100);
    wr(REG_B, 0x06);
}
