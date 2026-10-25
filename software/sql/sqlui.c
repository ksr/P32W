/* sqlui -- a full-screen way into a database of sqldb(3), in the spirit of
 * dBASE's ASSIST and BROWSE (see sqlui(1)).
 *
 *   sqlui [-c] dbdir
 *
 * Screens: the tables (a list with row counts), a table's rows as a grid
 * (read a screenful at a time with LIMIT/OFFSET: a table is never all in
 * memory), a form for one record, a line to type SQL in (its results in
 * the same grid, read-only), and help. A row is found again for UPDATE and
 * DELETE by its PRIMARY KEY or, in a table without one, by all its values.
 *
 * This file has the screens; sqluiscr.c the terminal, the screen and the
 * line editor (the two are separate files so that cc, which keeps a whole
 * file in memory, compiles each in a process's 8 MiB).
 */
#include "sqlui.h"

/* ---------------------------------------------------------------- a table's columns */

struct tinfo {
    char name[24];
    int nc, pk;                         /* columns; the PRIMARY KEY's (or -1) */
    char col[MAXC][24], type[MAXC][16];
    int isint[MAXC], notnull[MAXC];
};
static struct tinfo T;

static int describe(const char *table)
{
    char q[64];
    sqlstmt *st;
    sprintf(q, "DESCRIBE %.40s", table);
    if (!(st = sql_query(db, q))) {
        error(sql_errmsg(db));
        return 0;
    }
    strcpy(T.name, table);
    T.nc = 0;
    T.pk = -1;
    while (T.nc < MAXC && sql_next(st) > 0) {
        snprintf(T.col[T.nc], 24, "%s", sql_text(st, 0));
        snprintf(T.type[T.nc], 16, "%s", sql_text(st, 1));
        T.isint[T.nc] = !strcmp(T.type[T.nc], "INTEGER");
        T.notnull[T.nc] = !strcmp(sql_text(st, 2), "NO");
        if (!strcmp(sql_text(st, 3), "PRI")) T.pk = T.nc;
        T.nc++;
    }
    sql_finish(st);
    return 1;
}

/* ---------------------------------------------------------------- the grid */

struct grid {
    int table;                          /* 1: T's rows; 0: a query's (read-only) */
    char *sql;                          /* the query */
    char where[256], order[128];
    int nc, total, top, cur, lcol, nrow;
    char *name[MAXC];
    int w[MAXC];
    char **cell;                        /* nrow * nc: type (N I T, t cut), text */
};

#define NVIS (R - 3)                    /* rows of the grid on the screen */

static void gsql(struct grid *g, struct buf *b, const char *what)
{
    add(b, "SELECT ", -1);
    add(b, what, -1);
    add(b, " FROM ", -1);
    add(b, T.name, -1);
    if (g->where[0]) add(b, " WHERE ", -1), add(b, g->where, -1);
}

static void gfree(struct grid *g)
{
    int i;
    for (i = 0; i < g->nrow * g->nc; i++) free(g->cell[i]);
    g->nrow = 0;
}

/* read the rows from top (and with all, count them all: a query's) */
static int gfetch(struct grid *g, int all)
{
    struct buf b;
    sqlstmt *st;
    int r = 1, n = 0, i, k, len;
    char *s, num[16];
    gfree(g);
    memset(&b, 0, sizeof b);
    if (g->table) {
        gsql(g, &b, "*");
        if (g->order[0]) add(&b, " ORDER BY ", -1), add(&b, g->order, -1);
        sprintf(num, " LIMIT %d", NVIS);
        add(&b, num, -1);
        sprintf(num, " OFFSET %d", g->top);
        add(&b, num, -1);
    } else
        add(&b, g->sql, -1);
    st = sql_query(db, b.s);
    free(b.s);
    if (!st) {
        error(sql_errmsg(db));
        return 0;
    }
    if (!g->nc) {
        g->nc = sql_ncols(st) < MAXC ? sql_ncols(st) : MAXC;
        g->cell = xmalloc(NVIS * g->nc * sizeof(char *));
        for (i = 0; i < g->nc; i++) {
            g->name[i] = xstrdup(sql_colname(st, i));
            g->w[i] = strlen(g->name[i]) < MAXW ? strlen(g->name[i]) : MAXW;
        }
    }
    if (!g->table)
        for (i = 0; i < g->top && (r = sql_next(st)) > 0; i++) ;
    while (r > 0 && n < NVIS && (r = sql_next(st)) > 0) {
        for (i = 0; i < g->nc; i++) {
            s = sql_isnull(st, i) ? "NULL" : (char *)sql_text(st, i);
            len = strlen(s);
            k = sql_isnull(st, i) ? 'N' : len > MAXW ? 't' : sql_coltype(st, i) == SQL_INTEGER ? 'I' : 'T';
            if (len > MAXW) len = MAXW;
            g->cell[n * g->nc + i] = xmalloc(len + 2);
            g->cell[n * g->nc + i][0] = k;
            memcpy(g->cell[n * g->nc + i] + 1, s, len);
            g->cell[n * g->nc + i][len + 1] = 0;
            if (k == 't') len = MAXW;
            if (len > g->w[i]) g->w[i] = len;
        }
        g->nrow = ++n;
    }
    if (all && r > 0) {
        while ((r = sql_next(st)) > 0) n++;
        g->total = g->top + n;
    } else if (all)
        g->total = g->top + n;
    if (r < 0) error(sql_errmsg(db));
    sql_finish(st);
    return r >= 0;
}

/* the number of rows, then the rows from top; 0 on an error */
static int gload(struct grid *g)
{
    struct buf b;
    int n;
    if (!g->table) return gfetch(g, 1);
    memset(&b, 0, sizeof b);
    gsql(g, &b, "COUNT(*)");
    n = count(b.s);
    free(b.s);
    if (n < 0) return 0;
    g->total = n;
    if (g->cur >= n) g->cur = n - 1;
    if (g->cur < 0) g->cur = 0;
    if (g->top > g->cur) g->top = g->cur;
    if (g->top < g->cur - NVIS + 1) g->top = g->cur - NVIS + 1;
    return gfetch(g, 0);
}

/* the last column that fits wholly from lcol */
static int glast(struct grid *g)
{
    int c, x = 0;
    for (c = g->lcol; c < g->nc; c++) {
        x += g->w[c] + (c > g->lcol);
        if (x > C - 1) break;
    }
    return c - 1 < g->lcol ? g->lcol : c - 1;
}

static void growline(struct grid *g, int r, char **cell, int how)  /* how: 0, A_REV, A_BOLD */
{
    int c, n, x0, cut;
    char *s;
    lbeg();
    if (how) lattr(how);
    for (c = g->lcol; c < g->nc && lv < C - 1; c++) {
        if (c > g->lcol) lstr("|", 1);
        s = cell ? cell[c] + 1 : g->name[c];
        n = strlen(s);
        cut = n > g->w[c] || (cell && cell[c][0] == 't');
        x0 = lv;
        if (cell && cell[c][0] == 'I') lpad(x0 + g->w[c] - n);
        if (cut) lstr(s, g->w[c] - 1), lstr(">", 1);
        else lstr(s, n);
        lpad(x0 + g->w[c]);
    }
    if (how == A_REV) lpad(C - 1);
    lend(r);
}

static void gdraw(struct grid *g, const char *scr)
{
    int i;
    char info[100], where[300];
    growline(g, 0, 0, A_BOLD);
    for (i = 0; i < NVIS; i++)
        if (i < g->nrow) growline(g, i + 1, g->cell + i * g->nc, g->top + i == g->cur ? A_REV : 0);
        else {
            lbeg();
            if (i == 0 && !g->total) lstr("(no rows)", -1);
            lend(i + 1);
        }
    if (!g->total) sprintf(info, "%s | no rows", scr);
    else sprintf(info, "%s | row %d of %d", scr, g->cur + 1, g->total);
    where[0] = 0;
    if (g->lcol > 0 || glast(g) < g->nc - 1)
        sprintf(where, "cols %d-%d of %d ", g->lcol + 1, glast(g) + 1, g->nc);
    if (g->where[0]) snprintf(where + strlen(where), 120, "WHERE %s ", g->where);
    if (g->order[0]) snprintf(where + strlen(where), 100, "ORDER BY %s", g->order);
    status(info, where);
    keys(g->table ? "Enter edit  a add  D delete  / filter  o order  r refresh  ? help  q back"
                  : "arrows move  PgUp PgDn page  Left Right columns  ? help  q back");
    gorc(g->cur - g->top + 1 < 1 ? 1 : g->cur - g->top + 1, 0);
}

/* ---------------------------------------------------------------- the record form */

/* WHERE that finds the row of the original values ov/on: by the key, or
   by all the values */
static void addkey(struct buf *b, char **ov, int *onul)
{
    int i, first = 1;
    add(b, " WHERE ", -1);
    for (i = 0; i < T.nc; i++) {
        if (T.pk >= 0 && i != T.pk) continue;
        if (!first) add(b, " AND ", -1);
        first = 0;
        addname(b, T.col[i]);
        if (onul[i]) add(b, " IS NULL", -1);
        else add(b, " = ", -1), addval(b, ov[i], 0, T.isint[i]);
    }
}

/* how many rows the key finds (more than 1 only without a PRIMARY KEY) */
static int keycount(char **ov, int *onul)
{
    struct buf b;
    int n;
    memset(&b, 0, sizeof b);
    add(&b, "SELECT COUNT(*) FROM ", -1);
    add(&b, T.name, -1);
    addkey(&b, ov, onul);
    n = count(b.s);
    free(b.s);
    return n;
}

/* the row at index i of the grid, all of it, into v / nul */
static int getrow(struct grid *g, int i, char **v, int *nul)
{
    struct buf b;
    sqlstmt *st;
    char num[32];
    int c, r;
    memset(&b, 0, sizeof b);
    gsql(g, &b, "*");
    if (g->order[0]) add(&b, " ORDER BY ", -1), add(&b, g->order, -1);
    sprintf(num, " LIMIT 1 OFFSET %d", i);
    add(&b, num, -1);
    st = sql_query(db, b.s);
    free(b.s);
    if (!st || (r = sql_next(st)) <= 0) {
        error(st && r == 0 ? "the row is gone (r refreshes)" : sql_errmsg(db));
        if (st) sql_finish(st);
        return 0;
    }
    for (c = 0; c < T.nc; c++) {
        nul[c] = sql_isnull(st, c);
        snprintf(v[c], MAXV + 1, "%s", nul[c] ? "" : sql_text(st, c));
    }
    sql_finish(st);
    return 1;
}

static void delrow(struct grid *g)
{
    char *v[MAXC], q[80];
    int nul[MAXC], i, n;
    struct buf b;
    for (i = 0; i < T.nc; i++) v[i] = xmalloc(MAXV + 1);
    if (getrow(g, g->cur, v, nul) && (n = keycount(v, nul)) > 0) {
        if (n > 1) sprintf(q, "Delete %d identical rows? (y/n) ", n);
        else strcpy(q, "Delete this row? (y/n) ");
        if (ask(q)) {
            memset(&b, 0, sizeof b);
            add(&b, "DELETE FROM ", -1);
            add(&b, T.name, -1);
            addkey(&b, v, nul);
            if (sql_exec(db, b.s) < 0) error(sql_errmsg(db));
            else sprintf(msg, "%d row%s deleted", sql_changes(db), sql_changes(db) == 1 ? "" : "s");
            free(b.s);
        }
    }
    for (i = 0; i < T.nc; i++) free(v[i]);
}

/* INSERT (add) or UPDATE the row from the form; 1 if done */
static int save(int addit, char **v, int *nul, char **ov, int *onul)
{
    struct buf b;
    int i, n = 0;
    char q[64];
    memset(&b, 0, sizeof b);
    if (addit) {
        add(&b, "INSERT INTO ", -1);
        add(&b, T.name, -1);
        add(&b, " (", -1);
        for (i = 0; i < T.nc; i++)
            if (!nul[i] || (i == T.nc - 1 && !n)) {
                if (n++) add(&b, ", ", 2);
                addname(&b, T.col[i]);
            }
        add(&b, ") VALUES (", -1);
        for (n = i = 0; i < T.nc; i++)
            if (!nul[i] || (i == T.nc - 1 && !n)) {
                if (n++) add(&b, ", ", 2);
                addval(&b, v[i], nul[i], T.isint[i]);
            }
        add(&b, ")", 1);
    } else {
        add(&b, "UPDATE ", -1);
        add(&b, T.name, -1);
        add(&b, " SET ", -1);
        for (i = 0; i < T.nc; i++)
            if (nul[i] != onul[i] || strcmp(v[i], ov[i])) {
                if (n++) add(&b, ", ", 2);
                addname(&b, T.col[i]);
                add(&b, " = ", 3);
                addval(&b, v[i], nul[i], T.isint[i]);
            }
        if (!n) {
            strcpy(msg, "nothing changed");
            free(b.s);
            return 1;
        }
        addkey(&b, ov, onul);
        if (T.pk < 0 && (n = keycount(ov, onul)) != 1) {
            if (n == 0) error("the row is gone (r refreshes)");
            sprintf(q, "Change %d identical rows? (y/n) ", n);
            if (n <= 0 || !ask(q)) {
                free(b.s);
                return n == 0;
            }
        }
    }
    i = sql_exec(db, b.s);
    free(b.s);
    if (i < 0) {
        error(sql_errmsg(db));
        return 0;
    }
    sprintf(msg, "%d row%s %s", sql_changes(db), sql_changes(db) == 1 ? "" : "s",
            addit ? "added" : "changed");
    return 1;
}

static char *fhelp[] = {
    "The record form: a line for each column, name (type) [value].",
    "",
    "  Tab, Down, ^J   the next field         Up   the field before",
    "  Left, Right     move in the field       Home ^A, End ^E",
    "  Backspace       delete before          Del ^D  delete at the cursor",
    "  ^U              clear the field        ^K   clear to its end",
    "  ^N              make the value NULL (it shows NULL; typing replaces it)",
    "  Enter, ^S       save: INSERT (add) or UPDATE (edit)",
    "  Esc             leave without saving   ^G   this help",
    "",
    "An INTEGER PRIMARY KEY left NULL gets the next number. A row is found",
    "for UPDATE by its PRIMARY KEY or, without one, by all its old values.",
};

/* the form for the current row (or a new one, with addit); 1 if saved */
static int form(struct grid *g, int addit)
{
    char *v[MAXC], *ov[MAXC], line[80];
    int nul[MAXC], onul[MAXC], i, f = 0, ft = 0, nw = 4, tw = 4, x, k, pos = 0, ok = 0, h = R - 2;
    for (i = 0; i < T.nc; i++) {
        v[i] = xmalloc(MAXV + 1);
        ov[i] = xmalloc(MAXV + 1);
        v[i][0] = ov[i][0] = 0;
        nul[i] = onul[i] = 1;
        if ((int)strlen(T.col[i]) > nw) nw = strlen(T.col[i]);
        if ((int)strlen(T.type[i]) + 6 > tw) tw = strlen(T.type[i]) + 6;
    }
    x = nw + tw + 4;
    if (x > C / 2) x = C / 2;
    if (!addit && !getrow(g, g->cur, ov, onul)) goto out;
    for (i = 0; i < T.nc; i++) {
        strcpy(v[i], ov[i]);
        nul[i] = onul[i];
    }
    pos = strlen(v[0]);
    for (;;) {
        if (f < ft) ft = f;
        if (f >= ft + h) ft = f - h + 1;
        for (i = 0; i < h; i++) {
            lbeg();
            if (ft + i < T.nc) {
                k = ft + i;
                lstr(T.col[k], -1);
                lpad(nw + 1);
                sprintf(line, "(%s%s)", T.type[k], k == T.pk ? " key" : T.notnull[k] ? " req" : "");
                lstr(line, -1);
                lpad(x - 1);
                lstr("[", 1);
                if (nul[k]) lattr(A_BOLD), lstr("NULL", 4), lattr(A_OFF);
                else if ((int)strlen(v[k]) > C - x - 2) lstr(v[k], C - x - 3), lstr(">", 1);
                else lstr(v[k], -1);
                lpad(C - 2);
                lstr("]", 1);
            }
            lend(i);
        }
        sprintf(line, "%s %s", addit ? "Add to" : "Edit", T.name);
        status(line, 0);
        keys("Tab/Down next  Up prev  ^N null  ^U clear  Enter/^S save  Esc cancel  ^G help");
        msg[0] = 0;
        k = edit(v[f], MAXV, f - ft, x, C - x - 2, &pos, &nul[f]);
        if (k == '\t' || k == K_DOWN || k == '\n') f = (f + 1) % T.nc;
        else if (k == K_UP || k == K_PGUP) f = f > 0 ? f - 1 : T.nc - 1;
        else if (k == CTL('N')) nul[f] = 1, v[f][0] = 0;
        else if (k == CTL('G')) view("Help: the form", fhelp, sizeof fhelp / sizeof *fhelp);
        else if (k == ESC || k == CTL('C')) break;
        else if (k == '\r' || k == CTL('S')) {
            if (save(addit, v, nul, ov, onul)) {
                ok = 1;
                break;
            }
            continue;
        } else continue;
        pos = strlen(v[f]);
    }
out:
    for (i = 0; i < T.nc; i++) free(v[i]), free(ov[i]);
    return ok;
}

/* ---------------------------------------------------------------- browsing */

static char *bhelp[] = {
    "Browsing rows: the current row is highlighted.",
    "",
    "  Up Down, k j        a row up or down",
    "  PgUp PgDn, ^B ^F    a screenful",
    "  Home End, g G       the first or last row",
    "  Left Right, h l     scroll the columns sideways",
    "  Enter, e            edit the row in a form",
    "  a                   add a row (a form)",
    "  D                   delete the row (asks first)",
    "  /                   filter: a WHERE condition (empty: all rows)",
    "  o                   order: ORDER BY columns [DESC] (empty: none)",
    "  r                   read the rows again",
    "  q, Esc              back",
    "",
    "Query results are read-only: edit, add, delete, filter and order are",
    "for a table's rows. Wide values end with >; the form shows all of it.",
};

static void browse(struct grid *g, const char *scr)
{
    int k, top;
    char save[256];
    clearscr();
    for (;;) {
        gdraw(g, scr);
        k = getkey();
        msg[0] = 0;
        top = g->top;
        if (k == K_UP || k == 'k') g->cur--;
        else if (k == K_DOWN || k == 'j') g->cur++;
        else if (k == K_PGDN || k == CTL('F') || k == ' ') g->cur += NVIS, top += NVIS;
        else if (k == K_PGUP || k == CTL('B')) g->cur -= NVIS, top -= NVIS;
        else if (k == K_HOME || k == 'g') g->cur = top = 0;
        else if (k == K_END || k == 'G') g->cur = top = g->total - 1;
        else if (k == K_RIGHT || k == 'l') {
            if (glast(g) < g->nc - 1) g->lcol++;
        } else if (k == K_LEFT || k == 'h') {
            if (g->lcol > 0) g->lcol--;
        } else if (k == 'q' || k == ESC || k == 'Q') return;
        else if (k == '?') {
            view("Help: browsing", bhelp, sizeof bhelp / sizeof *bhelp);
        } else if (k == 'r')
            gload(g); else if (!g->table && strchr("\reaD/o", k))
            error("query results are read-only");
        else if (k == '\r' || k == 'e' || k == 'a') {
            if (k != 'a' && !g->total) continue;
            if (form(g, k == 'a')) gload(g);
        } else if (k == 'D' && g->total) {
            delrow(g);
            gload(g);
        } else if (k == '/' || k == 'o') {
            char *s = k == '/' ? g->where : g->order;
            int n = k == '/' ? sizeof g->where : sizeof g->order;
            strcpy(save, s);
            for (;;) {
                if (lineedit(k == '/' ? "WHERE " : "ORDER BY ", s, n - 1, 0) == ESC) {
                    strcpy(s, save);
                    msg[0] = 0;
                    break;
                }
                g->cur = g->top = 0;
                if (gload(g)) break;
                gdraw(g, scr);
            }
            if (!strcmp(s, save)) gload(g);
        }
        /* keep cur in 0..total-1 and on the screen */
        if (g->cur >= g->total) g->cur = g->total - 1;
        if (g->cur < 0) g->cur = 0;
        if (top > g->total - NVIS) top = g->total - NVIS;
        if (top < 0) top = 0;
        if (g->cur < top || g->cur >= top + NVIS) top = g->cur - NVIS / 2;
        if (top > g->total - NVIS) top = g->total - NVIS;
        if (top < 0) top = 0;
        if (top != g->top) {
            g->top = top;
            gfetch(g, 0);
        }
    }
}

static void gdone(struct grid *g)
{
    int i;
    gfree(g);
    for (i = 0; i < g->nc; i++) free(g->name[i]);
    free(g->cell);
    g->nc = 0;
}

static void descview(const char *table)
{
    char *ln[MAXC + 24], q[80];
    sqlstmt *st;
    int n = 0, i, rows;
    if (!describe(table)) return;
    sprintf(q, "SELECT COUNT(*) FROM %s", table);
    rows = count(q);
    sprintf(q, "Table %s: %d column%s, %d row%s", table, T.nc, T.nc == 1 ? "" : "s",
            rows, rows == 1 ? "" : "s");
    ln[n++] = xstrdup(q);
    ln[n++] = xstrdup("");
    sprintf(q, "  %-20s %-14s %-5s %s", "column", "type", "null", "key");
    ln[n++] = xstrdup(q);
    for (i = 0; i < T.nc; i++) {
        sprintf(q, "  %-20s %-14s %-5s %s", T.col[i], T.type[i], T.notnull[i] ? "NO" : "YES",
                i == T.pk ? "PRIMARY KEY" : "");
        ln[n++] = xstrdup(q);
    }
    ln[n++] = xstrdup("");
    sprintf(q, "SHOW INDEXES FROM %s", table);
    if ((st = sql_query(db, q))) {
        sprintf(q, "  %-20s %-20s %s", "index", "column", "unique");
        ln[n++] = xstrdup(q);
        while (n < MAXC + 23 && sql_next(st) > 0) {
            sprintf(q, "  %-20.20s %-20.20s %s", sql_text(st, 0), sql_text(st, 2),
                    !strcmp(sql_text(st, 3), "1") ? "yes" : "no");
            ln[n++] = xstrdup(q);
        }
        sql_finish(st);
    }
    view("Describe", ln, n);
    for (i = 0; i < n; i++) free(ln[i]);
}

/* ---------------------------------------------------------------- the query screen */

#define NHIST 20
static char *hist[NHIST];
static int nhist;

static char *qhelp[] = {
    "The query line: type a statement and press Enter.",
    "",
    "  SELECT, SHOW, DESCRIBE, EXPLAIN   the rows are shown in a grid",
    "                                    (read-only; q comes back here)",
    "  INSERT, UPDATE, DELETE, ...       run; the status line says how",
    "                                    many rows changed",
    "",
    "  Left Right Home End   move in the line    Backspace  delete",
    "  Up Down               earlier statements  ^U  clear the line",
    "  Esc                   back to the tables",
    "  ?                     this help (on an empty line)",
    "",
    "A statement is one line (no ; needed). Examples:",
    "  SELECT e.name, d.name FROM emp e JOIN dept d ON e.dept = d.id",
    "  SELECT dept, COUNT(*) FROM emp GROUP BY dept ORDER BY 2 DESC",
};

static void runquery(const char *s)
{
    struct grid g;
    char w[12];
    memset(&g, 0, sizeof g);
    if (isquery(s)) {
        g.sql = (char *)s;
        if (gfetch(&g, 1)) browse(&g, "Query results");
        gdone(&g);
    } else if (sql_exec(db, s) < 0)
        error(sql_errmsg(db));
    else {
        firstword(s, w);
        if (!strcmp(w, "INSERT") || !strcmp(w, "UPDATE") || !strcmp(w, "DELETE"))
            sprintf(msg, "%d row%s changed", sql_changes(db), sql_changes(db) == 1 ? "" : "s");
        else
            strcpy(msg, "done");
    }
}

static void query(void)
{
    char s[MAXV + 1];
    int k, h = nhist, i;
    s[0] = 0;
    clearscr();
    for (;;) {
        lbeg();
        lstr("Type a statement and press Enter (? on an empty line: help).", -1);
        lend(0);
        lbeg();
        lend(1);
        lbeg();
        if (nhist) lattr(A_BOLD), lstr("Earlier statements (Up and Down bring them back):", -1);
        lend(2);
        for (i = 0; i < R - 5; i++) {
            lbeg();
            if (i < nhist) lstr("  ", 2), lstr(hist[nhist - 1 - i], -1);
            lend(i + 3);
        }
        status("Query", "Enter run  Up Down history  Esc back");
        k = lineedit("SQL> ", s, MAXV, 1);
        msg[0] = 0;
        if (k == ESC) return;
        if (k == '?')
            view("Help: query", qhelp, sizeof qhelp / sizeof *qhelp);
        else if (k == K_UP && h > 0)
            strcpy(s, hist[--h]);
        else if (k == K_DOWN) {
            if (h < nhist - 1) strcpy(s, hist[++h]);
            else h = nhist, s[0] = 0;
        } else if (k == '\r' && s[0]) {
            if (!nhist || strcmp(hist[nhist - 1], s)) {
                if (nhist == NHIST) {
                    free(hist[0]);
                    memmove(hist, hist + 1, (NHIST - 1) * sizeof *hist);
                    nhist--;
                }
                hist[nhist++] = xstrdup(s);
            }
            h = nhist;
            runquery(s);
        }
    }
}

/* ---------------------------------------------------------------- the tables screen */

static char *thelp[] = {
    "sqlui: a database's tables, their rows, a form for a record, SQL.",
    "",
    "The tables screen: the tables and how many rows each has.",
    "",
    "  Up Down, k j    choose a table",
    "  Enter, b        browse its rows",
    "  d               describe it: columns, types, keys, indexes",
    "  n               a new table: type the rest of CREATE TABLE",
    "  x               drop the table (asks first)",
    "  q               the query line: any SQL statement",
    "  r               read the list again",
    "  ?               help (on every screen)",
    "  Q               quit (so does ^C, anywhere)",
};

static char **tname;
static int *trows, nt;

static void tload(void)
{
    sqlstmt *st;
    int i, cap = 0;
    for (i = 0; i < nt; i++) free(tname[i]);
    nt = 0;
    if (!(st = sql_query(db, "SHOW TABLES"))) {
        error(sql_errmsg(db));
        return;
    }
    while (sql_next(st) > 0) {
        if (nt == cap) {
            cap = cap ? cap * 2 : 16;
            tname = realloc(tname, cap * sizeof *tname);
            trows = realloc(trows, cap * sizeof *trows);
            if (!tname || !trows) done(2);
        }
        tname[nt] = xstrdup(sql_text(st, 0));
        trows[nt++] = sql_int(st, 1);
    }
    sql_finish(st);
}

static void tbrowse(const char *table)
{
    struct grid g;
    char scr[40];
    memset(&g, 0, sizeof g);
    if (!describe(table)) return;
    g.table = 1;
    sprintf(scr, "Browse %s", table);
    if (gload(&g)) browse(&g, scr);
    gdone(&g);
}

static void tables(void)
{
    int cur = 0, top = 0, i, k, h = R - 3;
    char s[MAXV + 1], q[80];
    tload();
    clearscr();
    for (;;) {
        if (cur >= nt) cur = nt - 1;
        if (cur < 0) cur = 0;
        if (cur < top) top = cur;
        if (cur >= top + h) top = cur - h + 1;
        lbeg();
        lattr(A_BOLD);
        lstr("Table", -1);
        lpad(24);
        lstr("    Rows", -1);
        lend(0);
        for (i = 0; i < h; i++) {
            lbeg();
            if (top + i < nt) {
                if (top + i == cur) lattr(A_REV);
                lstr(tname[top + i], -1);
                lpad(24);
                sprintf(q, "%8d", trows[top + i]);
                lstr(q, -1);
                if (top + i == cur) lpad(34);
            } else if (!nt && i == 0)
                lstr("(no tables: n makes one, q runs SQL)", -1);
            lend(i + 1);
        }
        sprintf(q, "%d table%s", nt, nt == 1 ? "" : "s");
        status("Tables", q);
        keys("Enter browse  d describe  n new  x drop  q query  ? help  Q quit");
        gorc(cur - top + 1, 0);
        k = getkey();
        msg[0] = 0;
        if (k == K_UP || k == 'k') cur--;
        else if (k == K_DOWN || k == 'j') cur++;
        else if (k == K_PGUP || k == CTL('B')) cur -= h;
        else if (k == K_PGDN || k == CTL('F')) cur += h;
        else if (k == K_HOME || k == 'g') cur = 0;
        else if (k == K_END || k == 'G') cur = nt - 1;
        else if (k == 'Q') return;
        else if (k == '?') view("Help: tables", thelp, sizeof thelp / sizeof *thelp);
        else if (k == 'q') query();
        else if (k == 'r') ;
        else if (k == 'n') {
            strcpy(s, "CREATE TABLE ");
            while (lineedit("", s, MAXV, 0) == '\r') {
                if (sql_exec(db, s) == 0) {
                    strcpy(msg, "table made");
                    break;
                }
                error(sql_errmsg(db));
                status("Tables", 0);
            }
        } else if ((k == '\r' || k == 'b' || k == 'd' || k == 'x') && nt) {
            if (k == 'd') descview(tname[cur]);
            else if (k != 'x') tbrowse(tname[cur]);
            else {
                sprintf(q, "Drop table %.30s and all its rows? (y/n) ", tname[cur]);
                if (ask(q)) {
                    sprintf(q, "DROP TABLE %.30s", tname[cur]);
                    if (sql_exec(db, q) < 0) error(sql_errmsg(db));
                    else sprintf(msg, "table %.30s dropped", tname[cur]);
                }
            }
        } else
            continue;
        tload();                        /* (the counts may have changed) */
    }
}

int main(int argc, char **argv)
{
    int create = 0;
    char *e;
    if (argc > 1 && !strcmp(argv[1], "-c")) create = 1, argv++, argc--;
    if (argc != 2 || argv[1][0] == '-') {
        fprintf(stderr, "usage: sqlui [-c] dbdir\n");
        return 2;
    }
    if (!isatty(0) || !isatty(1)) {
        fprintf(stderr, "sqlui: not a terminal\n");
        return 2;
    }
    if (!(db = sql_open(argv[1], create))) {
        fprintf(stderr, "sqlui: %s\n", sql_errmsg(0));
        return 2;
    }
    dbname = argv[1];
    if ((e = strrchr(dbname, '/')) && e[1]) dbname = e + 1;
    scrinit();
    tables();
    msg[0] = 0;
    done(0);
    return 0;
}
