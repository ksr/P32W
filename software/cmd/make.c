/* make -- keep programs up to date: rebuild what is older than what it is
 * made from, by the rules in a makefile.
 *
 *   make [-f file] [-C dir] [-n] [-s] [-k] [-i] [-p] [NAME=value ...] [target ...]
 *
 * The makefile (Makefile or makefile, or -f's) holds
 *     NAME = value                  a variable ($(NAME) or ${NAME}; $X for one letter;
 *                                   := expands at once, += appends, ?= only if unset)
 *     targets: prerequisites        a rule, then its commands on lines starting
 *     <tab>command                  with a tab (or after a ; on the rule's line)
 *     %.o: %.c                      a pattern rule (% matches the same stem)
 *     .c.o: / .c:                   suffix rules (old style)
 *     .PHONY: names                 targets that are not files
 *     include file                  read another makefile
 * with \ continuing a line and # starting a comment. In commands, $@ is the
 * target, $< the first prerequisite, $^ all of them, $? those newer than the
 * target, $* the stem; a leading @ runs a command without showing it, - goes
 * on after its failure. Each command runs in sh -c. Built in: CC=cc AS=as
 * AR=ar CFLAGS= LDFLAGS= LDLIBS=, and the rules .c to .o, .s to .o, .c to a
 * program. The first target in the makefile is made when none is named.
 *
 *   -n  show the commands, run none     -s  run them without showing them
 *   -k  go on with other targets after a failure
 *   -i  ignore every command's failure  -p  print the rules and variables
 *   -C dir  change to dir first */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>

/* ---------------------------------------------------------------- data */
struct var { char *name, *value; int cmdline; struct var *next; };
struct name { char *s; struct name *next; };
struct cmds { char **v; int n, cap; };
struct rule {                           /* a target and how to make it */
    char *target;
    struct name *deps;
    struct cmds cmds;
    int has_cmds, phony;
    int state;                          /* 0 not looked at, 1 being made, 2 made */
    long mtime;                         /* after making: its time (0: missing) */
    int failed, remade;
    struct rule *next;
};
struct pat {                            /* a pattern rule: %.o: %.c */
    char *target;                       /* with one % */
    struct name *deps;
    struct cmds cmds;
    struct pat *next;
};

static struct var *vars;
static struct rule *rules, *first;
static struct pat *pats;
static struct name *suffixes;
static int nflag, sflag, kflag, iflag, pflag, errors, remade_any;   /* (remade_any: some command ran) */
static const char *mfname;
static int lineno;

static void fatal(const char *fmt, const char *a)
{
    fprintf(stderr, "make: ");
    if (mfname && lineno) fprintf(stderr, "%s:%d: ", mfname, lineno);
    fprintf(stderr, fmt, a);
    fprintf(stderr, "\n");
    exit(2);
}

static void *xmalloc(int n)
{
    void *p = malloc(n);
    if (!p) fatal("out of memory%s", "");
    return p;
}

static char *xstrdup(const char *s) { char *p = xmalloc(strlen(s) + 1); strcpy(p, s); return p; }

static char *xstrndup(const char *s, int n)
{
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

/* a growing string */
struct buf { char *s; int n, cap; };
static void bput(struct buf *b, const char *s, int n)
{
    if (b->n + n + 1 > b->cap) {
        char *ns;
        b->cap = (b->n + n + 1) * 2 + 64;
        ns = xmalloc(b->cap);
        if (b->s) { memcpy(ns, b->s, b->n); free(b->s); }
        b->s = ns;
    }
    memcpy(b->s + b->n, s, n);
    b->n += n;
    b->s[b->n] = 0;
}
static void bputs(struct buf *b, const char *s) { bput(b, s, strlen(s)); }

/* ---------------------------------------------------------------- variables */
static struct var *findvar(const char *name)
{
    struct var *v;
    for (v = vars; v; v = v->next)
        if (!strcmp(v->name, name)) return v;
    return 0;
}

static void setvar(const char *name, const char *value, int cmdline)
{
    struct var *v = findvar(name);
    if (v && v->cmdline && !cmdline) return;        /* (the command line wins) */
    if (!v) {
        v = xmalloc(sizeof *v);
        v->name = xstrdup(name);
        v->next = vars;
        vars = v;
    } else
        free(v->value);
    v->value = xstrdup(value);
    v->cmdline = cmdline;
}

static const char *getvar(const char *name)
{
    struct var *v = findvar(name);
    char *e;
    if (v) return v->value;
    if ((e = getenv(name)) != 0) return e;
    return "";
}

/* the automatic variables of the command being run */
static const char *auto_at, *auto_lt, *auto_star;
static struct buf auto_all, auto_newer;

static void expand_into(struct buf *out, const char *s, int depth);

static void expand_var(struct buf *out, const char *name, int depth)
{
    if (!strcmp(name, "@") && auto_at) { bputs(out, auto_at); return; }
    if (!strcmp(name, "<") && auto_lt) { bputs(out, auto_lt); return; }
    if (!strcmp(name, "*") && auto_star) { bputs(out, auto_star); return; }
    if (!strcmp(name, "^") && auto_at) { bputs(out, auto_all.s ? auto_all.s : ""); return; }
    if (!strcmp(name, "?") && auto_at) { bputs(out, auto_newer.s ? auto_newer.s : ""); return; }
    if (depth > 30) fatal("variable %s refers to itself", name);
    expand_into(out, getvar(name), depth + 1);
}

/* $(NAME) ${NAME} $X $$, recursively; $(NAME:.c=.o) replaces suffixes of words */
static void expand_into(struct buf *out, const char *s, int depth)
{
    while (*s) {
        const char *d = strchr(s, '$');
        if (!d) { bputs(out, s); return; }
        bput(out, s, d - s);
        s = d + 1;
        if (*s == '$') { bput(out, "$", 1); s++; continue; }
        if (*s == '(' || *s == '{') {
            char close = *s == '(' ? ')' : '}', open = *s;
            const char *e = s + 1;
            int level = 1;
            char *inner, *colon;
            struct buf name = { 0, 0, 0 };
            while (*e && level) {
                if (*e == open) level++;
                else if (*e == close) level--;
                if (level) e++;
            }
            if (!*e) fatal("unclosed variable reference in %s", s - 1);
            inner = xstrndup(s + 1, e - s - 1);
            expand_into(&name, inner, depth + 1);         /* ($($(X)) works) */
            free(inner);
            colon = name.s ? strchr(name.s, ':') : 0;
            if (colon && strchr(colon, '=')) {            /* $(NAME:from=to) */
                char *eq = strchr(colon, '='), *w;
                struct buf val = { 0, 0, 0 };
                int fl, first_word = 1;
                *colon = 0;
                *eq = 0;
                fl = strlen(colon + 1);
                expand_var(&val, name.s, depth);
                for (w = val.s ? strtok(val.s, " \t") : 0; w; w = strtok(0, " \t")) {
                    int wl = strlen(w);
                    if (!first_word) bput(out, " ", 1);
                    first_word = 0;
                    if (wl >= fl && !strcmp(w + wl - fl, colon + 1)) { bput(out, w, wl - fl); bputs(out, eq + 1); }
                    else bputs(out, w);
                }
                free(val.s);
            } else
                expand_var(out, name.s ? name.s : "", depth);
            free(name.s);
            s = e + 1;
            continue;
        }
        if (*s) {
            char one[2];
            one[0] = *s++;
            one[1] = 0;
            expand_var(out, one, depth);
        }
    }
}

static char *expand(const char *s)
{
    struct buf b = { 0, 0, 0 };
    expand_into(&b, s, 0);
    return b.s ? b.s : xstrdup("");
}

/* ---------------------------------------------------------------- words, rules */
static struct name *words(const char *s, struct name *tail)        /* split on spaces, appended */
{
    struct name *head = tail, **end = &head;
    while (*end) end = &(*end)->next;
    while (*s) {
        const char *e;
        while (*s == ' ' || *s == '\t') s++;
        if (!*s) break;
        for (e = s; *e && *e != ' ' && *e != '\t'; e++) ;
        *end = xmalloc(sizeof **end);
        (*end)->s = xstrndup(s, e - s);
        (*end)->next = 0;
        end = &(*end)->next;
        s = e;
    }
    return head;
}

static struct rule *findrule(const char *t)
{
    struct rule *r;
    for (r = rules; r; r = r->next)
        if (!strcmp(r->target, t)) return r;
    return 0;
}

static struct rule *getrule(const char *t)
{
    struct rule *r = findrule(t), **end;
    if (r) return r;
    r = xmalloc(sizeof *r);
    memset(r, 0, sizeof *r);
    r->target = xstrdup(t);
    for (end = &rules; *end; end = &(*end)->next) ;
    *end = r;                           /* (in makefile order) */
    return r;
}

static void addcmd(struct cmds *c, const char *s)
{
    if (c->n == c->cap) {
        char **nv = xmalloc((c->cap = c->cap * 2 + 4) * sizeof *nv);
        if (c->v) { memcpy(nv, c->v, c->n * sizeof *nv); free(c->v); }
        c->v = nv;
    }
    c->v[c->n++] = xstrdup(s);
}

static int is_suffix(const char *s)
{
    struct name *n;
    for (n = suffixes; n; n = n->next)
        if (!strcmp(n->s, s)) return 1;
    return 0;
}

/* .c.o or .c: a suffix rule? (from, to) */
static int suffix_rule(const char *t, char *from, char *to)
{
    const char *dot;
    if (t[0] != '.' || strchr(t, '/')) return 0;
    if (is_suffix(t)) { strcpy(from, t); to[0] = 0; return 1; }
    for (dot = t + 1; *dot && *dot != '.'; dot++) ;
    if (!*dot || dot - t > 15 || strlen(dot) > 15) return 0;
    memcpy(from, t, dot - t);
    from[dot - t] = 0;
    strcpy(to, dot);
    return is_suffix(from) && is_suffix(to);
}

/* ---------------------------------------------------------------- reading the makefile */
static struct rule *cur_rules[32];
static int ncur;
static struct pat *cur_pat;

static void addcmd_current(const char *s)
{
    int k;
    if (cur_pat) { addcmd(&cur_pat->cmds, s); return; }
    for (k = 0; k < ncur; k++) { addcmd(&cur_rules[k]->cmds, s); cur_rules[k]->has_cmds = 1; }
}

static void readfile(const char *path);

static void ruleline(char *line, char *colon)
{
    char *targets, *deps, *semi, *t;
    struct name *tl, *n, *dl;
    *colon = 0;
    if (colon[1] == ':') colon++;       /* (:: is taken as :) */
    deps = colon + 1;
    semi = strchr(deps, ';');
    if (semi) *semi = 0;
    targets = expand(line);
    t = expand(deps);
    dl = words(t, 0);
    free(t);
    tl = words(targets, 0);
    free(targets);
    ncur = 0;
    cur_pat = 0;
    for (n = tl; n; n = n->next) {
        char from[16], to[16];
        if (!strcmp(n->s, ".PHONY")) {
            struct name *d;
            for (d = dl; d; d = d->next) getrule(d->s)->phony = 1;
            continue;
        }
        if (!strcmp(n->s, ".SUFFIXES")) {   /* (none: the list is cleared; else they are added) */
            struct name **e = &suffixes;
            if (!dl) { suffixes = 0; continue; }
            while (*e) e = &(*e)->next;
            *e = dl;
            continue;
        }
        if (strchr(n->s, '%') || suffix_rule(n->s, from, to)) {
            struct pat *p = xmalloc(sizeof *p), **e;
            memset(p, 0, sizeof *p);
            if (strchr(n->s, '%')) { p->target = xstrdup(n->s); p->deps = dl; }
            else {                      /* .c.o -> %.o: %.c;  .c: -> %: %.c */
                char tb[40], db[40];
                sprintf(tb, "%%%s", to);
                sprintf(db, "%%%s", from);
                p->target = xstrdup(tb);
                p->deps = words(db, 0);
            }
            for (e = &pats; *e; e = &(*e)->next) ;
            *e = p;
            cur_pat = p;
            continue;
        }
        {
            struct rule *r = getrule(n->s);
            struct name **e = &r->deps;
            struct name *d;
            while (*e) e = &(*e)->next;
            for (d = dl; d; d = d->next) {          /* (each rule gets its own copy) */
                *e = xmalloc(sizeof **e);
                (*e)->s = d->s;
                (*e)->next = 0;
                e = &(*e)->next;
            }
            if (r->has_cmds && ncur == 0) { r->cmds.n = 0; r->has_cmds = 0; }     /* (a second set of commands replaces the first) */
            if (!first && n->s[0] != '.') first = r;
            if (ncur < 32) cur_rules[ncur++] = r;
        }
    }
    if (semi) addcmd_current(semi + 1);
}

static void readfile(const char *path)
{
    FILE *f = strcmp(path, "-") ? fopen(path, "r") : stdin;
    char raw[1024];
    struct buf line = { 0, 0, 0 };
    const char *saved_name = mfname;
    int saved_line = lineno;
    if (!f) fatal("cannot open %s", path);
    mfname = path;
    lineno = 0;
    while (fgets(raw, sizeof raw, f)) {
        int n = strlen(raw);
        lineno++;
        if (n && raw[n - 1] == '\n') raw[--n] = 0;
        {                               /* (a continuation line's indent becomes one space) */
            char *r = raw;
            if (line.n && raw[0] != '\t') while (*r == ' ' || *r == '\t') r++;
            if (n && raw[n - 1] == '\\') {    /* continued: joined with one space */
                n--;
                while (n > 0 && (raw[n - 1] == ' ' || raw[n - 1] == '\t')) n--;
                raw[n] = ' ';
                raw[n + 1] = 0;
                bputs(&line, r);
                continue;
            }
            bputs(&line, r);
        }
        if (line.s[0] == '\t') {        /* a command */
            if (!ncur && !cur_pat) fatal("a command before any rule%s", "");
            addcmd_current(line.s + 1);
        } else {
            char *s = line.s, *hash = strchr(s, '#'), *eq, *colon, *p;
            if (hash) *hash = 0;
            while (*s == ' ') s++;
            for (p = s + strlen(s); p > s && (p[-1] == ' ' || p[-1] == '\t'); p--) ;
            *p = 0;
            if (!*s) { line.n = 0; continue; }
            eq = strchr(s, '=');
            colon = strchr(s, ':');
            if (!strncmp(s, "include ", 8)) {
                char *file = expand(s + 8), *w;
                for (w = strtok(file, " \t"); w; w = strtok(0, " \t")) readfile(w);
                free(file);
                ncur = 0;
                cur_pat = 0;
            } else if (eq && (!colon || colon > eq || colon + 1 == eq)) {     /* NAME = value */
                char *name = s, *value = eq + 1, *e = eq, kind = 0;
                if (e > s && (e[-1] == ':' || e[-1] == '+' || e[-1] == '?')) kind = *--e;
                while (e > name && (e[-1] == ' ' || e[-1] == '\t')) e--;
                *e = 0;
                while (*value == ' ' || *value == '\t') value++;
                if (kind == '?' && (findvar(name) || getenv(name))) ;
                else if (kind == ':') { char *v = expand(value); setvar(name, v, 0); free(v); }
                else if (kind == '+') {
                    struct buf b = { 0, 0, 0 };
                    bputs(&b, getvar(name));
                    if (b.n) bputs(&b, " ");
                    bputs(&b, value);
                    setvar(name, b.s, 0);
                    free(b.s);
                } else setvar(name, value, 0);
                ncur = 0;
                cur_pat = 0;
            } else if (colon)
                ruleline(s, colon);
            else
                fatal("not a rule, a variable or a command: %s", s);
        }
        line.n = 0;
        if (line.s) line.s[0] = 0;
    }
    if (f != stdin) fclose(f);
    free(line.s);
    mfname = saved_name;
    lineno = saved_line;
}

/* ---------------------------------------------------------------- making */
static long mtime_of(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 ? (long)st.st_mtime : 0;
}

static int exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static int run(const char *cmd)        /* one command line through sh: 0 if it succeeded */
{
    int silent = sflag, ignore = iflag, pid, st;
    while (*cmd == '@' || *cmd == '-' || *cmd == '+' || *cmd == ' ' || *cmd == '\t') {
        if (*cmd == '@') silent = 1;
        if (*cmd == '-') ignore = 1;
        cmd++;
    }
    if (!*cmd) return 0;
    if (!silent || nflag) { printf("%s\n", cmd); fflush(stdout); }
    if (nflag) return 0;
    fflush(stdout);
    if ((pid = fork()) < 0) { perror("make: fork"); return -1; }
    if (pid == 0) {
        signal(SIGINT, SIG_DFL);
        execl("/bin/sh", "sh", "-c", cmd, (char *)0);
        perror("/bin/sh");
        _exit(127);
    }
    while (wait(&st) != pid) ;
    if (st == 0) return 0;
    if (WIFSIGNALED(st) && WTERMSIG(st) == SIGINT) { fprintf(stderr, "make: interrupted\n"); exit(2); }
    fprintf(stderr, "make: *** [%s] Error %d%s\n", auto_at ? auto_at : "?", WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st),
            ignore ? " (ignored)" : "");
    return ignore ? 0 : -1;
}

/* a pattern rule that can make t: its stem in *stem and its first
   prerequisite's name in dep (which exists or can be made) */
static int make_target(const char *t, int top);

static struct pat *find_pattern(const char *t, char *stem, char *dep)
{
    struct pat *p;
    for (p = pats; p; p = p->next) {
        char *pc = strchr(p->target, '%');
        int pre = pc - p->target, suf = strlen(pc + 1), tl = strlen(t), sl;
        if (tl < pre + suf || strncmp(t, p->target, pre) || strcmp(t + tl - suf, pc + 1)) continue;
        sl = tl - pre - suf;
        if (sl <= 0 || sl > 200) continue;
        memcpy(stem, t + pre, sl);
        stem[sl] = 0;
        if (!p->deps) { dep[0] = 0; return p; }
        {
            char *d = p->deps->s, *dc = strchr(d, '%');
            if (dc) sprintf(dep, "%.*s%s%s", (int)(dc - d), d, stem, dc + 1);
            else strcpy(dep, d);
        }
        if (exists(dep) || findrule(dep)) return p;
    }
    return 0;
}

static void set_auto(struct rule *r, const char *lt, const char *stem, long tm)
{
    struct name *d;
    auto_at = r->target;
    auto_lt = lt;
    auto_star = stem;
    auto_all.n = auto_newer.n = 0;
    if (auto_all.s) auto_all.s[0] = 0;
    if (auto_newer.s) auto_newer.s[0] = 0;
    for (d = r->deps; d; d = d->next) {
        struct rule *dr = findrule(d->s);
        long dt = dr && dr->state == 2 ? dr->mtime : mtime_of(d->s);
        if (auto_all.n) bputs(&auto_all, " ");
        bputs(&auto_all, d->s);
        if (!tm || dt > tm || (dr && dr->remade)) {
            if (auto_newer.n) bputs(&auto_newer, " ");
            bputs(&auto_newer, d->s);
        }
    }
}

/* make t (and what it needs): 0 if it is up to date afterwards */
static int make_target(const char *t, int top)
{
    struct rule *r = findrule(t);
    struct name *d;
    struct pat *p = 0;
    char stem[208], dep[256];
    long tm, newest = 0;
    int bad = 0, need = 0, k;
    struct cmds *cmds = 0;
    if (!r) r = getrule(t);
    if (r->state == 2) return r->failed ? -1 : 0;
    if (r->state == 1) { fprintf(stderr, "make: %s depends on itself; dependency dropped\n", t); return 0; }
    r->state = 1;
    stem[0] = dep[0] = 0;
    if (!r->has_cmds && !r->phony) p = find_pattern(t, stem, dep);
    if (p && dep[0]) {                  /* the implied prerequisite first */
        if (make_target(dep, 0) < 0) bad = 1;
        else {
            struct rule *dr = findrule(dep);
            long dt = dr && dr->state == 2 ? dr->mtime : mtime_of(dep);
            if (dt > newest) newest = dt;
            if (dr && dr->remade) need = 1;
        }
    }
    for (d = r->deps; d; d = d->next) {
        struct rule *dr;
        long dt;
        if (make_target(d->s, 0) < 0) { bad = 1; if (!kflag) break; continue; }
        dr = findrule(d->s);
        dt = dr && dr->state == 2 ? dr->mtime : mtime_of(d->s);
        if (dt > newest) newest = dt;
        if (dr && dr->remade) need = 1;
    }
    if (bad) {
        r->state = 2;
        r->failed = 1;
        if (top || !kflag) fprintf(stderr, "make: %s not remade because of errors\n", t);
        return -1;
    }
    tm = r->phony ? 0 : mtime_of(t);
    if (r->phony || !tm || newest > tm) need = 1;
    if (r->has_cmds) cmds = &r->cmds;
    else if (p) cmds = &p->cmds;
    if (!cmds || !cmds->n) {
        if (!tm && !r->phony && !r->deps && !(p && dep[0])) {
            r->state = 2;
            r->failed = 1;
            fprintf(stderr, "make: don't know how to make %s\n", t);
            return -1;
        }
        r->state = 2;
        r->mtime = tm ? tm : newest;
        if (top && !need) printf("make: '%s' is up to date.\n", t);
        else if (top && !remade_any) printf("make: Nothing to be done for '%s'.\n", t);
        return 0;
    }
    if (!need) {
        r->state = 2;
        r->mtime = tm;
        if (top) printf("make: '%s' is up to date.\n", t);
        return 0;
    }
    set_auto(r, dep[0] ? dep : r->deps ? r->deps->s : "", stem, tm);
    for (k = 0; k < cmds->n; k++) {
        char *c = expand(cmds->v[k]);
        int st = run(c);
        free(c);
        if (st < 0) {
            r->state = 2;
            r->failed = 1;
            errors = 1;
            if (!r->phony && exists(t) && mtime_of(t) != tm) {   /* (a half-made file would look up to date) */
                fprintf(stderr, "make: deleting %s\n", t);
                unlink(t);
            }
            return -1;
        }
    }
    auto_at = 0;
    r->state = 2;
    r->remade = 1;
    remade_any = 1;
    r->mtime = nflag ? newest + 1 : mtime_of(t);
    if (!r->mtime) r->mtime = newest + 1;   /* (a target that makes no file: still counts as new) */
    return 0;
}

static void print_all(void)
{
    struct var *v;
    struct rule *r;
    struct pat *p;
    struct name *n;
    int k;
    for (v = vars; v; v = v->next) printf("%s = %s\n", v->name, v->value);
    printf("\n");
    for (p = pats; p; p = p->next) {
        printf("%s:", p->target);
        for (n = p->deps; n; n = n->next) printf(" %s", n->s);
        printf("\n");
        for (k = 0; k < p->cmds.n; k++) printf("\t%s\n", p->cmds.v[k]);
    }
    for (r = rules; r; r = r->next) {
        printf("%s%s:", r->target, r->phony ? " (phony)" : "");
        for (n = r->deps; n; n = n->next) printf(" %s", n->s);
        printf("\n");
        for (k = 0; k < r->cmds.n; k++) printf("\t%s\n", r->cmds.v[k]);
    }
}

static void builtins(void)
{
    struct pat *p;
    setvar("CC", "cc", 0);
    setvar("AS", "as", 0);
    setvar("AR", "ar", 0);
    setvar("CFLAGS", "", 0);
    setvar("LDFLAGS", "", 0);
    setvar("LDLIBS", "", 0);
    setvar("MAKE", "make", 0);
    suffixes = words(".o .c .s .a", 0);
    pats = p = xmalloc(sizeof *p); memset(p, 0, sizeof *p);
    p->target = "%.o"; p->deps = words("%.c", 0); addcmd(&p->cmds, "$(CC) $(CFLAGS) -c $<");
    p->next = xmalloc(sizeof *p); p = p->next; memset(p, 0, sizeof *p);
    p->target = "%.o"; p->deps = words("%.s", 0); addcmd(&p->cmds, "$(AS) -o $@ $<");
    p->next = xmalloc(sizeof *p); p = p->next; memset(p, 0, sizeof *p);
    p->target = "%"; p->deps = words("%.c", 0); addcmd(&p->cmds, "$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)");
}

int main(int argc, char **argv)
{
    const char *file = 0;
    char *targets[64];
    int nt = 0, i, st = 0;
    struct pat *bi;
    builtins();
    bi = pats;                          /* (the makefile's own patterns go first) */
    pats = 0;
    for (i = 1; i < argc; i++) {
        char *a = argv[i], *eq = strchr(a, '=');
        if (a[0] == '-' && a[1]) {
            char *f;
            for (f = a + 1; *f; f++)
                switch (*f) {
                case 'n': nflag = 1; break;
                case 's': sflag = 1; break;
                case 'k': kflag = 1; break;
                case 'i': iflag = 1; break;
                case 'p': pflag = 1; break;
                case 'f':
                    if (f[1]) { file = f + 1; f += strlen(f) - 1; }
                    else if (i + 1 < argc) { file = argv[++i]; }
                    else fatal("-f needs a file%s", "");
                    goto next;
                case 'C':
                    {
                        const char *dir = f[1] ? f + 1 : i + 1 < argc ? argv[++i] : 0;
                        if (!dir || chdir(dir) < 0) fatal("cannot change to directory %s", dir ? dir : "");
                        printf("make: entering directory '%s'\n", dir);
                    }
                    goto next;
                default:
                    fprintf(stderr, "usage: make [-f file] [-C dir] [-n] [-s] [-k] [-i] [-p] [NAME=value ...] [target ...]\n");
                    return 2;
                }
        } else if (eq && eq != a) {
            *eq = 0;
            setvar(a, eq + 1, 1);
            *eq = '=';
        } else if (nt < 64)
            targets[nt++] = a;
    next:;
    }
    if (!file) file = exists("Makefile") ? "Makefile" : exists("makefile") ? "makefile" : 0;
    if (file) readfile(file);
    {
        struct pat **e = &pats;
        while (*e) e = &(*e)->next;
        *e = bi;
    }
    lineno = 0;
    if (pflag) { print_all(); return 0; }
    if (!nt) {
        if (!first) { fprintf(stderr, "make: no targets%s\n", file ? "" : " and no makefile (Makefile)"); return 2; }
        targets[nt++] = first->target;
    }
    for (i = 0; i < nt; i++)
        if (make_target(targets[i], 1) < 0) { st = 2; if (!kflag) break; }
    return st;
}
