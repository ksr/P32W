/* db -- a debugger for P32W Unix programs (backlog: the debugger, first
 * part: machine level, with the a.out's symbols).
 *
 *   db program [args...]
 *
 * The program starts stopped before its first instruction. Commands:
 *   c                    continue
 *   s [n]                step n instructions (into calls)
 *   n [n]                step n instructions, over calls
 *   f                    finish: run until the current function returns
 *   b where              a breakpoint (where: a symbol, symbol+off, or 0xADDR)
 *   d [n]                delete breakpoint n (all, without n)
 *   bl                   list the breakpoints
 *   r                    registers
 *   set REG VALUE        set r1..r15, sp, fp or pc
 *   x where [n]          n words of memory
 *   p sym                a symbol's address and the word there
 *   l [where] [n]        disassemble n instructions (pc, 8)
 *   bt                   backtrace (the frame-pointer chain)
 *   sym [prefix]         list symbols
 *   q                    quit (the program is killed)
 * An empty line repeats the last s, n or l. The program shares the
 * terminal. Without symbols (ld -s), use addresses.
 *
 * How: ptrace (kernel/syscall.c). A breakpoint is a BREAK instruction
 * written into the program's own copy of its text; it traps with the pc at
 * the BREAK. Breakpoints are in place only while the program runs; to step
 * one instruction, temporary breakpoints go where it can go next (the next
 * word, a branch or jump target, the register of JR / CALLR, the return
 * address of RET). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <p32isa.h>

#define BREAK_WORD 0x74000000u
#define MAXBP 32

static int pid, alive, pending;   /* pending: a signal to deliver when it goes on */
static unsigned regs[PT_NREGS];
static const struct p32op *byop[256];

/* ---------------------------------------------------------------- symbols */
struct sym { unsigned value; char type; char *name; };
static struct sym *syms;
static int nsyms;

static void load_symbols(const char *path)
{
    int fd = open(path, O_RDONLY), n, k;
    unsigned h[8];
    unsigned char *tab;
    if (fd < 0) return;
    if (read(fd, h, 32) != 32 || h[0] != 0x50333277u || !h[6]) { close(fd); return; }
    tab = malloc(h[6]);
    lseek(fd, 32 + h[1] + h[2], 0);
    if (!tab || read(fd, tab, h[6]) != (int)h[6]) { close(fd); return; }
    close(fd);
    for (k = 0, n = 0; k < (int)h[6]; n++) k += (6 + tab[k + 5] + 3) & ~3;
    syms = malloc(sizeof *syms * (n + 1));
    for (k = 0, nsyms = 0; k < (int)h[6]; nsyms++) {
        int len = tab[k + 5];
        syms[nsyms].value = tab[k] | tab[k + 1] << 8 | tab[k + 2] << 16 | (unsigned)tab[k + 3] << 24;
        syms[nsyms].type = tab[k + 4];
        syms[nsyms].name = malloc(len + 1);
        memcpy(syms[nsyms].name, tab + k + 6, len);
        syms[nsyms].name[len] = 0;
        k += (6 + len + 3) & ~3;
    }
}

static int find_sym(const char *name, unsigned *v)
{
    int k, best = -1;
    for (k = 0; k < nsyms; k++)
        if (!strcmp(syms[k].name, name) && (best < 0 || (syms[k].type >= 'A' && syms[k].type <= 'Z'))) best = k;
    if (best < 0) return 0;
    *v = syms[best].value;
    return 1;
}

/* "name+off" for an address in the text (the nearest symbol at or below it) */
static const char *where(unsigned a)
{
    static char b[80];
    int k, best = -1;
    for (k = 0; k < nsyms; k++)
        if ((syms[k].type == 'T' || syms[k].type == 't') && syms[k].value <= a &&
            (best < 0 || syms[k].value >= syms[best].value))
            best = k;
    if (best < 0) { b[0] = 0; return b; }
    if (a == syms[best].value) sprintf(b, "<%.60s>", syms[best].name);
    else sprintf(b, "<%.60s+%u>", syms[best].name, a - syms[best].value);
    return b;
}

static int parse_addr(const char *s, unsigned *v)   /* sym, sym+off, 0x..., decimal */
{
    char name[64];
    const char *plus;
    unsigned off = 0;
    int n;
    if (!s || !*s) return 0;
    if (s[0] >= '0' && s[0] <= '9') { *v = strtoul(s, 0, 0); return 1; }
    plus = strchr(s, '+');
    n = plus ? plus - s : (int)strlen(s);
    if (n > 63) return 0;
    memcpy(name, s, n);
    name[n] = 0;
    if (plus) off = strtoul(plus + 1, 0, 0);
    if (!find_sym(name, v)) {
        char under[66];
        sprintf(under, "_C_%s", name);          /* (p32cc's names for C names that are register names) */
        if (!find_sym(under, v)) return 0;
    }
    *v += off;
    return 1;
}

/* ---------------------------------------------------------------- the program */
static unsigned peek(unsigned a)
{
    unsigned w = 0;
    ptrace(PT_PEEK, pid, a & ~3u, (unsigned)&w);
    return w;
}

static void poke(unsigned a, unsigned w) { ptrace(PT_POKE, pid, a, w); }
static void getregs(void) { ptrace(PT_GETREGS, pid, 0, (unsigned)regs); }
static void setregs(void) { ptrace(PT_SETREGS, pid, 0, (unsigned)regs); }

/* ---------------------------------------------------------------- disassembly */
static const char *rn(int r) { static char b[4][8]; static int k; k = (k + 1) % 4; sprintf(b[k], r == 15 ? "sp" : "r%d", r); return b[k]; }

static void disasm(unsigned pc, unsigned w, char *out)
{
    const struct p32op *o = byop[w >> 24];
    int a = w >> 20 & 15, b = w >> 16 & 15, c = w >> 12 & 15;
    int imm = (short)(w & 0xFFFF);
    unsigned uimm = w & 0xFFFF;
    char m[16];
    int i;
    if (!o) { sprintf(out, ".word 0x%08x", w); return; }
    for (i = 0; o->name[i] && i < 15; i++) m[i] = o->name[i] | 32;
    m[i] = 0;
    switch (o->fmt) {
    case F_R: sprintf(out, "%-6s %s, %s, %s", m, rn(a), rn(b), rn(c)); break;
    case F_I: sprintf(out, "%-6s %s, %s, %d", m, rn(a), rn(b), imm); break;
    case F_IU: sprintf(out, "%-6s %s, %s, 0x%x", m, rn(a), rn(b), uimm); break;
    case F_SH: sprintf(out, "%-6s %s, %s, %u", m, rn(a), rn(b), uimm & 31); break;
    case F_LUI: sprintf(out, "%-6s %s, 0x%x", m, rn(a), uimm); break;
    case F_LD: case F_ST: sprintf(out, "%-6s %s, %d(%s)", m, rn(a), imm, rn(b)); break;
    case F_BR: { unsigned t = pc + 4 + imm * 4; sprintf(out, "%-6s %s, %s, 0x%x %s", m, rn(a), rn(b), t, where(t)); break; }
    case F_J: { int off = (int)(w << 8) >> 8; unsigned t = pc + 4 + off * 4; sprintf(out, "%-6s 0x%x %s", m, t, where(t)); break; }
    case F_JR: sprintf(out, "%-6s %s", m, rn(b)); break;
    case F_CR:
        if (!strcmp(m, "mfcr")) sprintf(out, "%-6s %s, %d", m, rn(a), b);
        else sprintf(out, "%-6s %d, %s", m, b, rn(a));
        break;
    default: sprintf(out, "%s", m);
    }
}

static void show_insn(unsigned pc)
{
    char d[128];
    disasm(pc, peek(pc), d);
    printf("0x%08x %-20s %s\n", pc, where(pc), d);
}

/* where the instruction at pc can go next: up to 2 addresses */
static int next_pcs(unsigned pc, unsigned *n)
{
    unsigned w = peek(pc);
    const struct p32op *o = byop[w >> 24];
    int b = w >> 16 & 15;
    if (!o) { n[0] = pc + 4; return 1; }
    switch (o->fmt) {
    case F_BR: n[0] = pc + 4; n[1] = pc + 4 + (short)(w & 0xFFFF) * 4; return n[1] == n[0] ? 1 : 2;
    case F_J: n[0] = pc + 4 + ((int)(w << 8) >> 8) * 4; return 1;
    case F_JR: n[0] = b == 15 ? regs[15] : regs[b]; return 1;
    case F_N:
        if (!strcmp(o->name, "RET")) { n[0] = peek(regs[15]); return 1; }
        n[0] = pc + 4;
        return 1;
    }
    n[0] = pc + 4;
    return 1;
}

static int is_call(unsigned w)
{
    const struct p32op *o = byop[w >> 24];
    return o && (!strcmp(o->name, "CALL") || !strcmp(o->name, "CALLR"));
}

/* ---------------------------------------------------------------- breakpoints */
struct bp { unsigned addr, saved; int used; };
static struct bp bps[MAXBP];

/* run until the program stops; 1 if it is still there. temps: temporary
   breakpoints (taken out again); skip: an address with a breakpoint the
   program sits on (left out this time) */
static int run(unsigned *temps, int ntemps, unsigned skip)
{
    unsigned saved_t[4];
    int k, st, sig;
    for (k = 0; k < MAXBP; k++)
        if (bps[k].used && bps[k].addr != skip) { bps[k].saved = peek(bps[k].addr); poke(bps[k].addr, BREAK_WORD); }
    for (k = 0; k < ntemps; k++) {
        saved_t[k] = peek(temps[k]);
        poke(temps[k], BREAK_WORD);
    }
    ptrace(PT_CONT, pid, 1, pending);
    pending = 0;
    while (wait(&st) != pid) ;
    if (!WIFSTOPPED(st)) {
        if (WIFEXITED(st)) printf("the program exited, status %d\n", WEXITSTATUS(st));
        else printf("the program was killed by signal %d\n", WTERMSIG(st));
        alive = 0;
        return 0;
    }
    for (k = ntemps - 1; k >= 0; k--) poke(temps[k], saved_t[k]);
    for (k = MAXBP - 1; k >= 0; k--)
        if (bps[k].used && bps[k].addr != skip) poke(bps[k].addr, bps[k].saved);
    getregs();
    sig = WSTOPSIG(st);
    if (sig != SIGTRAP) {               /* a real signal: say so; it is delivered when the program goes on */
        printf("signal %d\n", sig);
        pending = sig;
    }
    return 1;
}

static int bp_at(unsigned a)
{
    int k;
    for (k = 0; k < MAXBP; k++) if (bps[k].used && bps[k].addr == a) return 1;
    return 0;
}

static int step(void)                   /* one instruction */
{
    unsigned n[2];
    int k = next_pcs(regs[16], n);
    return run(n, k, bp_at(regs[16]) ? regs[16] : 0xFFFFFFFFu);
}

static int cont(void)
{
    unsigned pc = regs[16];
    if (bp_at(pc)) {                    /* off the breakpoint first */
        if (!step()) return 0;
        if (bp_at(regs[16])) return 1;
    }
    (void)pc;
    return run(0, 0, 0xFFFFFFFFu);
}

/* ---------------------------------------------------------------- commands */
static void show_regs(void)
{
    int k;
    for (k = 1; k < 16; k++) printf("%-3s %08x%s", rn(k), regs[k], k % 4 == 3 ? "\n" : "  ");
    printf("pc  %08x %s\n", regs[16], where(regs[16]));
}

/* the current function's start (its symbol), or 0 */
static unsigned func_start(unsigned a)
{
    int k, best = -1;
    for (k = 0; k < nsyms; k++)
        if ((syms[k].type == 'T' || syms[k].type == 't') && syms[k].value <= a &&
            (best < 0 || syms[k].value >= syms[best].value))
            best = k;
    return best < 0 ? 0 : syms[best].value;
}

/* where the current function returns to. Its frame (addi sp,-4; sw r14;
   mov r14,sp) is not made yet in its first three instructions: then the
   return address is at sp (or sp + 4), and fp is still the caller's. */
static unsigned return_address(int *made)
{
    unsigned off = regs[16] - func_start(regs[16]);
    *made = off >= 12;
    if (off == 0) return peek(regs[15]);
    if (off == 4 || off == 8) return peek(regs[15] + 4);
    return peek(regs[14] + 4);
}

static void backtrace(void)
{
    unsigned pc = regs[16], fp = regs[14];
    int k, made;
    unsigned ra = return_address(&made);
    printf("#0  0x%08x %s\n", pc, where(pc));
    if (!made) {                        /* (the caller's frame is fp's) */
        printf("#1  0x%08x %s\n", ra, where(ra));
        k = 2;
    } else
        k = 1;
    for (; k < 32 && fp && fp < 0x00800000u && !(fp & 3); k++) {   /* (USERTOP: 8 MiB) */
        unsigned ra = peek(fp + 4), up = peek(fp);
        if (!ra) break;
        printf("#%-2d 0x%08x %s\n", k, ra, where(ra));
        if (up <= fp) break;
        fp = up;
    }
}

/* up to three words of line into a, b, c (sizes na, nb, nc); how many */
static int words(const char *line, char *a, int na, char *b, int nb, char *c, int nc)
{
    char *out[3];
    int size[3], n = 0;
    out[0] = a; out[1] = b; out[2] = c;
    size[0] = na; size[1] = nb; size[2] = nc;
    while (n < 3) {
        int k = 0;
        while (*line == ' ' || *line == '\t' || *line == '\n') line++;
        if (!*line) break;
        while (*line && *line != ' ' && *line != '\t' && *line != '\n') {
            if (k < size[n] - 1) out[n][k++] = *line;
            line++;
        }
        out[n][k] = 0;
        n++;
    }
    return n;
}

static int reg_index(const char *s)
{
    if (!strcmp(s, "pc")) return 16;
    if (!strcmp(s, "sp")) return 15;
    if (!strcmp(s, "fp")) return 14;
    if (s[0] == 'r' && s[1] >= '0' && s[1] <= '9') { int r = atoi(s + 1); if (r >= 1 && r <= 15) return r; }
    return -1;
}

int main(int argc, char **argv)
{
    char line[256], last[256];
    int st, k;
    if (argc < 2) { fprintf(stderr, "usage: db program [args...]\n"); return 2; }
    for (k = 0; k < P32_NOPS; k++) byop[p32_ops[k].op] = &p32_ops[k];
    load_symbols(argv[1]);
    if ((pid = fork()) < 0) { perror("db: fork"); return 1; }
    if (pid == 0) {
        ptrace(PT_TRACEME, 0, 0, 0);
        execv(argv[1], argv + 1);
        perror(argv[1]);
        _exit(127);
    }
    while (wait(&st) != pid) ;
    if (!WIFSTOPPED(st)) { fprintf(stderr, "db: %s did not start\n", argv[1]); return 1; }
    alive = 1;
    getregs();
    printf("db: %s, %d symbols; stopped at the start\n", argv[1], nsyms);
    show_insn(regs[16]);
    last[0] = 0;
    for (;;) {
        char cmd[32], a1[64], a2[64];
        int n;
        printf("db> ");
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin)) break;
        if (line[0] == '\n') strcpy(line, last);
        cmd[0] = a1[0] = a2[0] = 0;
        n = words(line, cmd, 32, a1, 64, a2, 64);
        if (n <= 0) continue;
        if (!strcmp(cmd, "q")) break;
        if (!alive && strcmp(cmd, "sym") && strcmp(cmd, "bl")) { printf("the program is not running\n"); continue; }
        if (!strcmp(cmd, "c")) {
            if (cont()) { printf("stopped: "); show_insn(regs[16]); }
        } else if (!strcmp(cmd, "s") || !strcmp(cmd, "n")) {
            int count = a1[0] ? atoi(a1) : 1, i;
            strcpy(last, line);
            for (i = 0; i < count && alive; i++) {
                unsigned pc = regs[16], w = peek(pc);
                if (cmd[0] == 'n' && is_call(w)) {
                    unsigned ret = pc + 4;
                    if (!run(&ret, 1, bp_at(pc) ? pc : 0xFFFFFFFFu)) break;
                } else if (!step())
                    break;
            }
            if (alive) show_insn(regs[16]);
        } else if (!strcmp(cmd, "f")) {
            int made;
            unsigned ret = return_address(&made);
            if (run(&ret, 1, bp_at(regs[16]) ? regs[16] : 0xFFFFFFFFu)) { printf("returned to: "); show_insn(regs[16]); }
        } else if (!strcmp(cmd, "b")) {
            unsigned a;
            if (!parse_addr(a1, &a)) { printf("no such place: %s\n", a1); continue; }
            for (k = 0; k < MAXBP && bps[k].used; k++) ;
            if (k == MAXBP) { printf("too many breakpoints\n"); continue; }
            bps[k].used = 1;
            bps[k].addr = a & ~3u;
            printf("breakpoint %d at 0x%08x %s\n", k + 1, bps[k].addr, where(bps[k].addr));
        } else if (!strcmp(cmd, "d")) {
            if (a1[0]) { k = atoi(a1) - 1; if (k >= 0 && k < MAXBP) bps[k].used = 0; }
            else for (k = 0; k < MAXBP; k++) bps[k].used = 0;
        } else if (!strcmp(cmd, "bl")) {
            for (k = 0; k < MAXBP; k++) if (bps[k].used) printf("%2d  0x%08x %s\n", k + 1, bps[k].addr, where(bps[k].addr));
        } else if (!strcmp(cmd, "r")) {
            show_regs();
        } else if (!strcmp(cmd, "set")) {
            int r = reg_index(a1);
            unsigned v;
            if (r < 0 || !parse_addr(a2, &v)) { printf("usage: set REG VALUE\n"); continue; }
            regs[r] = v;
            setregs();
        } else if (!strcmp(cmd, "x")) {
            unsigned a;
            int cnt = a2[0] ? atoi(a2) : 4, i;
            if (!parse_addr(a1, &a)) { printf("no such place: %s\n", a1); continue; }
            a &= ~3u;
            for (i = 0; i < cnt; i++) {
                if (i % 4 == 0) printf("%s0x%08x:", i ? "\n" : "", a + 4 * i);
                printf(" %08x", peek(a + 4 * i));
            }
            printf("\n");
        } else if (!strcmp(cmd, "p")) {
            unsigned a;
            if (!parse_addr(a1, &a)) { printf("no such symbol: %s\n", a1); continue; }
            printf("%s = 0x%08x: %08x (%d)\n", a1, a, peek(a), (int)peek(a));
        } else if (!strcmp(cmd, "l")) {
            unsigned a = regs[16];
            int cnt = 8, i;
            strcpy(last, "l\n");
            if (a1[0] && !parse_addr(a1, &a)) { printf("no such place: %s\n", a1); continue; }
            if (a2[0]) cnt = atoi(a2);
            for (i = 0; i < cnt; i++) {
                printf("%s", a + 4 * i == regs[16] ? "=> " : bp_at(a + 4 * i) ? " * " : "   ");
                show_insn(a + 4 * i);
            }
            sprintf(last, "l 0x%x %d\n", a + 4 * cnt, cnt);
        } else if (!strcmp(cmd, "bt")) {
            backtrace();
        } else if (!strcmp(cmd, "sym")) {
            for (k = 0; k < nsyms; k++)
                if (!a1[0] || !strncmp(syms[k].name, a1, strlen(a1)))
                    printf("0x%08x %c %s\n", syms[k].value, syms[k].type, syms[k].name);
        } else
            printf("commands: c s n f b d bl r set x p l bt sym q\n");
    }
    if (alive) {
        ptrace(PT_KILL, pid, 0, 0);
        while (wait(&st) != pid) ;
    }
    return 0;
}
