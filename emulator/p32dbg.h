/*
 * p32dbg.h -- the debug server (-dbg PORT, D97) for both emulators. A front
 * end (tools/p32dbg: a page in the browser, or commands) drives the machine
 * over one TCP connection on 127.0.0.1, as YACC1-D's y1dbg drives its
 * microcode-level emulator. The first line on stdout says where:
 * "p32emu: debug server on 127.0.0.1 port N". The emulator ends when the
 * connection closes, however it closes. Without -dbg none of this runs.
 *
 * A command is one line of text; each is answered by one line holding one
 * JSON object with "ok" (true or false; "error" says why) and the machine's
 * status: "state" (stopped, running, halted), "reason" (why it last
 * stopped), "pc" (the next instruction's address; p32uemu between two
 * microcycles of one instruction: that instruction's), "mode" ("S" or "U"),
 * "instrs" and "cycles" (since the start: instructions, and p32uemu's
 * microcycles). Addresses, values and bytes in commands are hex (an optional
 * 0x or $), counts decimal; numbers in replies are decimal JSON numbers.
 *
 *   hello              "emulator" (p32emu or p32uemu), "protocol" 1, "video" 1
 *   status             the status alone (so is an empty line)
 *   regs               + "r" (r0..r15), "sp_other" (the other mode's r15),
 *                      "status", "imask", "epc", "cause", "badaddr", "vbr",
 *                      "asid", "tlbhi", "tlblo", "timer" [count, reload,
 *                      enabled, pending]; p32uemu also "ir"
 *   step [N]           N instructions (default 1), the reply when they are
 *                      done (commands wait till then); stops early at a
 *                      HALT, not at breakpoints
 *   ustep [N]          (p32uemu) N microcycles
 *   run                until a breakpoint, a HALT or pause; the reply comes
 *                      at once ("running"); later replies show the stop
 *   until ADDR         run, and stop at ADDR too
 *   pause              stop a run at the next instruction boundary
 *   break add ADDR | break del ADDR | break del all | break list
 *                      breakpoints: stop when the next instruction is at ADDR
 *                      ("breaks": the list, at most 64)
 *   mem ADDR LEN [phys]  "addr", "hex": LEN bytes (at most 65536) as a load
 *                      would see them (user mode: through the D-TLB, else the
 *                      I-TLB; supervisor mode is physical), or physical; "??"
 *                      where nothing answers or a device register is (the
 *                      server never reads one: that could take its data)
 *   poke ADDR BB... [phys]  bytes into the ROM or RAM (no bus cycle)
 *   set NAME VALUE     a register: r0..r15, pc, sp_other, status, imask, epc,
 *                      cause, badaddr, vbr, asid; replies as regs
 *   key TEXT           queue TEXT for UART 0's receiver: "queued" (escapes
 *                      \n \r \t \e \\ \xHH and \s for a space)
 *   console            "text": what P32W sent on UART 0 since the last
 *                      console, "lost": bytes dropped (over 1 MB not fetched)
 *   kbd TEXT           the PS/2 keyboard types TEXT (as -K; the same escapes)
 *   scan HH...         set-2 scan codes into the keyboard as they are (an
 *                      arrow up: E0 75 E0 F0 75)
 *   video [SUM]        the video card: "on" (its chip is started), "sum" (a
 *                      checksum of the picture), "pix" (hex: 480 lines x 80
 *                      bytes, bit 7 the left-hand dot), "text" (the console's
 *                      25 rows of 80 characters, a line each, read from the
 *                      dots: each cell matched against the font); given the
 *                      last SUM and nothing changed, "same" and no data
 *   trace [N]          "trace": the addresses of the last N instructions run
 *                      (default 20, at most 4096), the oldest first
 *   ustate             (p32uemu) the microcycle last run: "uaddr" [SEQ, OPCODE,
 *                      STEP, COND] as it began, "word" (the microword, hex),
 *                      "a", "b", "r" (the three buses), "alua", "alub",
 *                      "aluop", "mar", "mdr", "ir", "rf" (the register file's
 *                      32), "next" [SEQ, OPCODE, STEP, COND] (the next)
 *   reset              the reset: the CPU (PC 0, supervisor mode, interrupts
 *                      off, the TLB and the timer cleared) and the video
 *                      card's registers; memory, disks and the counts kept
 *   quit               the reply, then the emulator exits
 *
 * While running, the server looks for commands every 65536 instructions
 * (p32uemu: every 65536 microcycles), and at once when P32W idles in WAIT
 * (which then follows real time, as at a terminal).
 */
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <unistd.h>

/* the server stays out of the emulators' hot loops: its functions are not
   inlined there, and the loops' tests of dbg_on are marked unlikely */
#if defined(__GNUC__)
#define DBG_COLD __attribute__((noinline, cold))
#define DBG_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define DBG_COLD
#define DBG_UNLIKELY(x) (x)
#endif

struct sbuf { char *p; size_t n, cap; };
static void sb_add(struct sbuf *s, const char *fmt, ...)
{
    for (;;) {
        va_list ap;
        int k;
        va_start(ap, fmt);
        k = vsnprintf(s->p ? s->p + s->n : NULL, s->p ? s->cap - s->n : 0, fmt, ap);
        va_end(ap);
        if (s->p && k >= 0 && (size_t)k < s->cap - s->n) { s->n += (size_t)k; return; }
        s->cap = s->cap * 2 + (k > 0 ? (size_t)k : 0) + 256;
        if (!(s->p = realloc(s->p, s->cap))) exit(3);
    }
}
static void sb_str(struct sbuf *s, const char *p, size_t n)     /* a JSON string */
{
    static const char hx[] = "0123456789abcdef";
    sb_add(s, "\"");
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)p[i];
        if (c == '"' || c == '\\') sb_add(s, "\\%c", c);
        else if (c >= 0x20 && c < 0x7F) sb_add(s, "%c", c);
        else sb_add(s, "\\u00%c%c", hx[c >> 4], hx[c & 15]);   /* (bytes as Latin-1: the front end decodes) */
    }
    sb_add(s, "\"");
}
static void sb_hex(struct sbuf *s, const uint8_t *p, size_t n)
{
    static const char hx[] = "0123456789abcdef";
    if (s->cap - s->n < 2 * n + 8) {
        s->cap = s->n + 2 * n + 256;
        if (!(s->p = realloc(s->p, s->cap))) exit(3);
    }
    s->p[s->n++] = '"';
    for (size_t i = 0; i < n; i++) { s->p[s->n++] = hx[p[i] >> 4]; s->p[s->n++] = hx[p[i] & 15]; }
    s->p[s->n++] = '"';
}

/* the emulator's side (p32emu.c, p32uemu.c) */
static const char *dbg_name;
static uint32_t dbg_status_pc(void);
static int dbg_supervisor(void);
static unsigned long long dbg_instrs(void), dbg_cycles(void);
static void dbg_regs(struct sbuf *s);                   /* JSON members, each with a comma after it */
static int dbg_set(const char *name, uint32_t v);       /* 0: no such register */
static int dbg_read(uint32_t a, int phys, uint8_t *v);  /* 0: nothing there, or a device */
static int dbg_write(uint32_t a, int phys, uint8_t v);
static void dbg_cpu_reset(void);
static int dbg_ustate(struct sbuf *s);                  /* the same; 0: not this emulator (p32emu) */

enum { DS_STOPPED, DS_RUNNING, DS_HALTED };
static int dbg_on, dbg_lfd = -1, dbg_state = DS_STOPPED;
static const char *dbg_reason = "start";
static unsigned long long dbg_steps, dbg_usteps;        /* left to run before stopping (0: none) */
static uint32_t dbg_until;
static int dbg_until_on;
static unsigned dbg_count;                              /* boundaries since the last look */
#define DBG_NBREAK 64
static uint32_t dbg_break[DBG_NBREAK];
static int dbg_nbreak;
static uint8_t dbg_bhash[1024];                         /* (pc >> 2) & 1023: a breakpoint may be there */
static uint32_t dbg_trace[4096];
static unsigned long long dbg_ntrace;
static char dbg_in[1 << 17];                            /* the commands read and not yet run */
static int dbg_replying;                                /* a step's reply is owed at its stop */
static int dbg_ran;                                     /* an instruction began (since a reset) */
static size_t dbg_inlen;

static int dbg_is_break(uint32_t a)
{
    if (!dbg_bhash[(a >> 2) & 1023]) return 0;
    for (int k = 0; k < dbg_nbreak; k++)
        if (dbg_break[k] == a) return 1;
    return 0;
}
static void dbg_rehash(void)
{
    memset(dbg_bhash, 0, sizeof dbg_bhash);
    for (int k = 0; k < dbg_nbreak; k++) dbg_bhash[(dbg_break[k] >> 2) & 1023] = 1;
}

static int dbg_num(const char *p, uint32_t *v)          /* hex, with an optional 0x or $ */
{
    char *e;
    if (!p || !*p) return 0;
    if (*p == '$') p++;
    else if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    unsigned long x = strtoul(p, &e, 16);
    if (*e || e == p) return 0;
    *v = (uint32_t)x;
    return 1;
}

/* TEXT with \n \r \t \e \\ \s \xHH into bytes; returns the count */
static size_t dbg_unescape(const char *p, uint8_t *out, size_t max)
{
    size_t n = 0;
    while (*p && n < max) {
        int c = (unsigned char)*p++;
        if (c == '\\' && *p) {
            int e = *p++;
            if (e == 'n') c = '\n';
            else if (e == 'r') c = '\r';
            else if (e == 't') c = '\t';
            else if (e == 'e') c = 27;
            else if (e == 's') c = ' ';
            else if (e == 'x' && isxdigit((unsigned char)p[0]) && isxdigit((unsigned char)p[1])) {
                char h[3] = { p[0], p[1], 0 };
                c = (int)strtol(h, NULL, 16);
                p += 2;
            } else c = e;
        }
        out[n++] = (uint8_t)c;
    }
    return n;
}

static void dbg_status(struct sbuf *s)
{
    static const char *const st[] = { "stopped", "running", "halted" };
    sb_add(s, "\"state\":\"%s\",\"reason\":\"%s\",\"pc\":%u,\"mode\":\"%s\",\"instrs\":%llu,\"cycles\":%llu",
           st[dbg_state], dbg_reason, dbg_status_pc(), dbg_supervisor() ? "S" : "U", dbg_instrs(), dbg_cycles());
}

static void dbg_send(struct sbuf *s)
{
    size_t off = 0;
    sb_add(s, "}\n");
    while (off < s->n) {
        ssize_t w = write(dbg_fd, s->p + off, s->n - off);
        if (w < 0 && (errno == EAGAIN || errno == EINTR)) {
            struct pollfd p = { dbg_fd, POLLOUT, 0 };
            poll(&p, 1, 1000);
            continue;
        }
        if (w <= 0) exit(0);                            /* the front end is gone */
        off += (size_t)w;
    }
}

static uint32_t dbg_video_sum(void)                     /* what the screen shows, in one number */
{
    uint32_t h = 2166136261u;                           /* (FNV-1a over the picture's 480 lines of 80 bytes) */
    uint8_t row[80];
    h = (h ^ (uint32_t)vid_shown()) * 16777619u;
    if (vid_shown())
        for (int n = 0; n < 480; n++) {
            vid_line(n, row);
            for (int i = 0; i < 80; i++) h = (h ^ row[i]) * 16777619u;
        }
    return h;
}

/* one command line; returns 1 if the machine is to run on */
static DBG_COLD int dbg_command(char *line)
{
    struct sbuf s = { 0, 0, 0 };
    char *argv[512], *p = line;
    int argc = 0, resume = 0, ok = 1, with_regs = 0;
    char err[200] = "";
    while (*p && argc < 511) {                          /* words; key and kbd keep the rest of the line */
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        argv[argc++] = p;
        if (argc == 2 && (!strcmp(argv[0], "key") || !strcmp(argv[0], "kbd"))) break;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = 0;
    }
    argv[argc] = NULL;
    const char *cmd = argc ? argv[0] : "";
    sb_add(&s, "{");
    if (!strcmp(cmd, "hello"))
        sb_add(&s, "\"emulator\":\"%s\",\"protocol\":1,\"video\":%d,", dbg_name, vid_fitted);
    else if (!strcmp(cmd, "status") || !cmd[0]) ;
    else if (!strcmp(cmd, "regs")) with_regs = 1;
    else if (!strcmp(cmd, "step") || !strcmp(cmd, "ustep")) {
        unsigned long long n = argc > 1 ? strtoull(argv[1], NULL, 10) : 1;
        int u = cmd[0] == 'u';
        if (u && !dbg_ustate(NULL)) { ok = 0; snprintf(err, sizeof err, "ustep: p32uemu only"); }
        else if (!n) { ok = 0; snprintf(err, sizeof err, "step: a count of 1 or more"); }
        else {
            if (u) dbg_usteps = n, dbg_steps = 0;
            else dbg_steps = n, dbg_usteps = 0;
            dbg_until_on = 0;
            resume = 1;
        }
    } else if (!strcmp(cmd, "run") || !strcmp(cmd, "until")) {
        dbg_steps = dbg_usteps = 0;
        dbg_until_on = 0;
        if (cmd[0] == 'u') {
            if (argc < 2 || !dbg_num(argv[1], &dbg_until)) { ok = 0; snprintf(err, sizeof err, "until ADDR"); }
            else dbg_until_on = 1;
        }
        if (ok) resume = 1;
    } else if (!strcmp(cmd, "pause")) {
        if (dbg_state == DS_RUNNING) dbg_state = DS_STOPPED, dbg_reason = "pause";
    } else if (!strcmp(cmd, "break")) {
        uint32_t a = 0;
        const char *w = argc > 1 ? argv[1] : "list";
        if (!strcmp(w, "add") && argc > 2 && dbg_num(argv[2], &a)) {
            if (!dbg_is_break(a)) {
                if (dbg_nbreak == DBG_NBREAK) { ok = 0; snprintf(err, sizeof err, "break: at most %d", DBG_NBREAK); }
                else dbg_break[dbg_nbreak++] = a;
            }
        } else if (!strcmp(w, "del") && argc > 2 && !strcmp(argv[2], "all"))
            dbg_nbreak = 0;
        else if (!strcmp(w, "del") && argc > 2 && dbg_num(argv[2], &a)) {
            for (int k = 0; k < dbg_nbreak; k++)
                if (dbg_break[k] == a) { dbg_break[k] = dbg_break[--dbg_nbreak]; break; }
        } else if (strcmp(w, "list")) { ok = 0; snprintf(err, sizeof err, "break add ADDR | del ADDR | del all | list"); }
        dbg_rehash();
        sb_add(&s, "\"breaks\":[");
        for (int k = 0; k < dbg_nbreak; k++) sb_add(&s, "%s%u", k ? "," : "", dbg_break[k]);
        sb_add(&s, "],");
    } else if (!strcmp(cmd, "mem")) {
        uint32_t a, n;
        int phys = argc > 3 && !strcmp(argv[3], "phys");
        if (argc < 3 || !dbg_num(argv[1], &a) || (n = (uint32_t)strtoul(argv[2], NULL, 10)) == 0 || n > 65536) {
            ok = 0; snprintf(err, sizeof err, "mem ADDR LEN [phys] (LEN 1 to 65536)");
        } else {
            static const char hx[] = "0123456789abcdef";
            sb_add(&s, "\"addr\":%u,\"hex\":\"", a);
            for (uint32_t i = 0; i < n; i++) {
                uint8_t v;
                if (dbg_read(a + i, phys, &v)) sb_add(&s, "%c%c", hx[v >> 4], hx[v & 15]);
                else sb_add(&s, "??");
            }
            sb_add(&s, "\",");
        }
    } else if (!strcmp(cmd, "poke")) {
        uint32_t a, v;
        int phys = argc > 3 && !strcmp(argv[argc - 1], "phys"), last = argc - phys;
        if (argc < 3 || !dbg_num(argv[1], &a)) { ok = 0; snprintf(err, sizeof err, "poke ADDR BB... [phys]"); }
        for (int k = 2; ok && k < last; k++)
            if (!dbg_num(argv[k], &v) || v > 255) { ok = 0; snprintf(err, sizeof err, "poke: %s is not a byte", argv[k]); }
            else if (!dbg_write(a + (uint32_t)(k - 2), phys, (uint8_t)v)) {
                ok = 0; snprintf(err, sizeof err, "poke: nothing to write at %08X", a + (uint32_t)(k - 2));
            }
    } else if (!strcmp(cmd, "set")) {
        uint32_t v;
        if (argc < 3 || !dbg_num(argv[2], &v)) { ok = 0; snprintf(err, sizeof err, "set NAME VALUE"); }
        else if (!dbg_set(argv[1], v)) { ok = 0; snprintf(err, sizeof err, "set: no register %s", argv[1]); }
        with_regs = 1;
    } else if (!strcmp(cmd, "key") || !strcmp(cmd, "kbd")) {
        static uint8_t b[65536];
        size_t n = argc > 1 ? dbg_unescape(argv[1], b, sizeof b) : 0;
        for (size_t i = 0; i < n; i++) {
            if (cmd[0] == 'k' && cmd[1] == 'e') {
                if (dbg_khead - dbg_ktail >= sizeof dbg_keys) { ok = 0; snprintf(err, sizeof err, "key: the queue is full"); break; }
                dbg_keys[dbg_khead++ & 0xFFFF] = b[i];
            } else ps2_type(b[i]);
        }
        sb_add(&s, "\"queued\":%u,", (unsigned)n);
    } else if (!strcmp(cmd, "scan")) {
        uint32_t v;
        for (int k = 1; ok && k < argc; k++)
            if (!dbg_num(argv[k], &v) || v > 255) { ok = 0; snprintf(err, sizeof err, "scan: %s is not a byte", argv[k]); }
            else ps2_scan(v);
    } else if (!strcmp(cmd, "console")) {
        sb_add(&s, "\"text\":");
        sb_str(&s, dbg_con ? dbg_con : "", dbg_con_len);
        sb_add(&s, ",\"lost\":%llu,", dbg_con_lost);
        dbg_con_len = 0;
        dbg_con_lost = 0;
    } else if (!strcmp(cmd, "video")) {
        uint32_t sum = dbg_video_sum(), was;               /* on: the chip is started; sum: the picture */
        sb_add(&s, "\"on\":%u,\"sum\":%u,", (unsigned)vid_shown(), sum);
        if (argc > 1 && (was = (uint32_t)strtoul(argv[1], NULL, 10), 1) && was == sum)
            sb_add(&s, "\"same\":true,");
        else {
            static uint8_t pix[480 * 80];               /* the 640 x 480 dots, bit 7 the left-hand one */
            char text[VID_ROWS * 81];                   /* the console's 25 rows, read from the dots */
            size_t n = 0;
            for (int k = 0; k < 480; k++) vid_line(k, pix + 80 * k);
            for (int r = 0; r < VID_ROWS; r++) {
                for (int c = 0; c < 80; c++) text[n++] = (char)vid_char(r, c);
                text[n++] = '\n';
            }
            sb_add(&s, "\"text\":");
            sb_str(&s, text, n);
            sb_add(&s, ",\"pix\":");
            sb_hex(&s, pix, sizeof pix);
            sb_add(&s, ",");
        }
    } else if (!strcmp(cmd, "trace")) {
        unsigned long long n = argc > 1 ? strtoull(argv[1], NULL, 10) : 20;
        if (n > 4096) n = 4096;
        if (n > dbg_ntrace) n = dbg_ntrace;
        sb_add(&s, "\"trace\":[");
        for (unsigned long long k = dbg_ntrace - n; k < dbg_ntrace; k++)
            sb_add(&s, "%s%u", k == dbg_ntrace - n ? "" : ",", dbg_trace[k & 4095]);
        sb_add(&s, "],");
    } else if (!strcmp(cmd, "ustate")) {
        if (!dbg_ustate(&s)) { ok = 0; snprintf(err, sizeof err, "ustate: p32uemu only"); }
    } else if (!strcmp(cmd, "reset")) {
        dbg_cpu_reset();
        dbg_ran = 0;                            /* (p32uemu: the RESET sequence is no instruction) */
        vid_reset();
        dbg_state = DS_STOPPED;
        dbg_reason = "reset";
    } else if (!strcmp(cmd, "quit")) {
        sb_add(&s, "\"ok\":true");
        dbg_send(&s);
        exit(0);
    } else { ok = 0; snprintf(err, sizeof err, "no command %s (hello, status, regs, step, ustep, run, until, pause, "
                              "break, mem, poke, set, key, console, kbd, scan, video, trace, ustate, reset, quit)", cmd); }
    if (resume && ok) {
        dbg_state = DS_RUNNING;
        dbg_reason = "";
        if (dbg_steps || dbg_usteps) {          /* a step: the reply comes when it stops */
            dbg_replying = 1;
            free(s.p);
            return 1;
        }
    }
    if (with_regs) dbg_regs(&s);
    if (!ok) { sb_add(&s, "\"error\":"); sb_str(&s, err, strlen(err)); sb_add(&s, ","); }
    sb_add(&s, "\"ok\":%s,", ok ? "true" : "false");
    dbg_status(&s);
    dbg_send(&s);
    free(s.p);
    return resume && ok;
}

/* read what has come; run each whole line. block: wait for one. Returns 1
   if a command resumed the machine. */
static DBG_COLD int dbg_read_commands(int block)
{
    int resumed = 0;
    for (;;) {
        char *nl;
        while ((nl = memchr(dbg_in, '\n', dbg_inlen)) != NULL) {
            size_t n = (size_t)(nl - dbg_in) + 1;
            *nl = 0;
            if (n > 1 && nl[-1] == '\r') nl[-1] = 0;
            char line[sizeof dbg_in];
            memcpy(line, dbg_in, n);
            memmove(dbg_in, dbg_in + n, dbg_inlen - n);
            dbg_inlen -= n;
            if (dbg_command(line)) resumed = 1;
            if (dbg_state == DS_RUNNING && block) return 1;     /* (stopped: the first resume ends the wait) */
        }
        if (!block && dbg_state != DS_RUNNING) return resumed;  /* (a pause: serve below) */
        struct pollfd p = { dbg_fd, POLLIN, 0 };
        if (poll(&p, 1, block ? -1 : 0) <= 0) return resumed;
        if (dbg_inlen == sizeof dbg_in) dbg_inlen = 0;          /* (a line over 128K: dropped) */
        ssize_t r = read(dbg_fd, dbg_in + dbg_inlen, sizeof dbg_in - dbg_inlen);
        if (r == 0 || (r < 0 && errno != EAGAIN && errno != EINTR)) exit(0);   /* the front end is gone */
        if (r > 0) dbg_inlen += (size_t)r;
        else if (!block) return resumed;
    }
}

/* stopped (or halted): the reply a step owes, then serve until a command
   runs the machine on */
static DBG_COLD void dbg_serve(void)
{
    if (dbg_replying) {
        struct sbuf s = { 0, 0, 0 };
        dbg_replying = 0;
        sb_add(&s, "{\"ok\":true,");
        dbg_status(&s);
        dbg_send(&s);
        free(s.p);
    }
    while (dbg_state != DS_RUNNING)
        dbg_read_commands(1);
}

static DBG_COLD void dbg_stop(const char *why)
{
    dbg_state = DS_STOPPED;
    dbg_reason = why;
    dbg_steps = dbg_usteps = 0;
    dbg_until_on = 0;
    dbg_serve();
}

/* at each instruction boundary (pc: the next instruction), with -dbg */
static DBG_COLD void dbg_boundary(uint32_t pc)
{
    if (dbg_state == DS_RUNNING) {
        if (dbg_steps && dbg_ran && --dbg_steps == 0) { dbg_stop("step"); goto ran; }
        if (!dbg_steps && !dbg_usteps && dbg_nbreak && dbg_is_break(pc)) { dbg_stop("break"); goto ran; }
        if (dbg_until_on && pc == dbg_until) { dbg_stop("until"); goto ran; }
        if ((++dbg_count >= 65536 || dbg_wake) && !dbg_replying) {
            dbg_count = 0;
            dbg_wake = 0;
            dbg_read_commands(0);
            if (dbg_state != DS_RUNNING) dbg_serve();   /* (a pause, or a reset) */
        }
    } else
        dbg_serve();
ran:
    dbg_ran = 1;                                        /* (the instruction at the boundary begins) */
    dbg_trace[dbg_ntrace++ & 4095] = dbg_status_pc();    /* (the instruction about to run; a reset moved it) */
}

#ifdef DBG_MICRO
/* after each microcycle (p32uemu), with -dbg */
static DBG_COLD void dbg_ucycle(void)
{
    if (dbg_state == DS_RUNNING && dbg_usteps && --dbg_usteps == 0)
        dbg_stop("ustep");
}
#endif

/* a HALT (or a WAIT nothing can end): halted, serving until run on (after
   a reset, from PC 0) */
static DBG_COLD void dbg_halt(const char *why)
{
    dbg_state = DS_HALTED;
    dbg_reason = why;
    dbg_steps = dbg_usteps = 0;
    dbg_until_on = 0;
    dbg_serve();
}

/* -dbg PORT: listen, say where on stdout, take the front end's connection */
static DBG_COLD void dbg_start(void)
{
    struct sockaddr_in a;
    socklen_t al = sizeof a;
    int one = 1;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)dbg_port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if ((dbg_lfd = socket(AF_INET, SOCK_STREAM, 0)) < 0
        || setsockopt(dbg_lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one) < 0
        || bind(dbg_lfd, (struct sockaddr *)&a, sizeof a) < 0 || listen(dbg_lfd, 1) < 0
        || getsockname(dbg_lfd, (struct sockaddr *)&a, &al) < 0) {
        perror("-dbg");
        exit(255);
    }
    printf("%s: debug server on 127.0.0.1 port %u\n", dbg_name, ntohs(a.sin_port));
    fflush(stdout);
    if ((dbg_fd = accept(dbg_lfd, NULL, NULL)) < 0) { perror("-dbg: accept"); exit(255); }
    close(dbg_lfd);
    setsockopt(dbg_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    fcntl(dbg_fd, F_SETFL, O_NONBLOCK);
    signal(SIGPIPE, SIG_IGN);
    dbg_on = 1;
}
