/* sql -- query and change a database of sqldb(3) (a directory of tables).
 *
 *   sql [-c] [-csv] dbdir [statement ...]
 *
 * With statements (or commands) as arguments, sql runs them and stops. Otherwise it reads statements
 * from the standard input: at a terminal with the prompt "sql> " (and
 * "   ...> " while a statement goes on over more lines), else quietly, as a
 * script. A statement ends with ";". A query (SELECT, SHOW, DESCRIBE,
 * EXPLAIN) prints its rows: in columns under a header, or with -csv as
 * comma-separated values. -c makes the database if it is not there.
 *
 * A line starting with "." (where a statement would start) is a command:
 *   .tables           the tables
 *   .schema [table]   the CREATE statements that would make them again
 *   .read file        run the statements in a file
 *   .mode column|csv  how rows are printed
 *   .help  .quit
 * The program uses only the library's API: it is also an example of it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "sqldb.h"

#define WROWS 100                   /* rows looked at to choose column widths */

struct buf { char *s; int n, cap; };    /* a growing string */

static sqldb *db;
static int csv, interactive, errors, depth;

static void add(struct buf *b, const char *s, int n)
{
    if (b->n + n + 1 > b->cap) {
        b->cap = (b->n + n + 1) * 2;
        if (!(b->s = realloc(b->s, b->cap))) {
            fprintf(stderr, "sql: out of memory\n");
            exit(2);
        }
    }
    memcpy(b->s + b->n, s, n);
    b->n += n;
    b->s[b->n] = 0;
}

/* an error, with where it was: the file (or "line") and line number */
static void report(const char *msg, const char *file, int line)
{
    fflush(stdout);
    if (interactive && !depth) fprintf(stderr, "error: %s\n", msg);
    else if (line && !strcmp(file, "line")) fprintf(stderr, "sql: line %d: %s\n", line, msg);
    else if (line) fprintf(stderr, "sql: %s:%d: %s\n", file, line, msg);
    else fprintf(stderr, "sql: %s\n", msg);
    errors++;
}

/* ---------------------------------------------------------------- printing rows */

static const char *cell(sqlstmt *st, int i)
{
    if (sql_isnull(st, i)) return csv ? "" : "NULL";
    return sql_text(st, i);
}

static void csvout(const char *s, int last)
{
    if (strpbrk(s, ",\"\n")) {          /* quoted, with " doubled */
        putchar('"');
        for (; *s; s++) {
            if (*s == '"') putchar('"');
            putchar(*s);
        }
        putchar('"');
    } else
        fputs(s, stdout);
    putchar(last ? '\n' : ',');
}

/* one row in columns of widths w (no spaces at the end of the line) */
static void padrow(char **v, int *w, int nc)
{
    int i, n, pad = 0;
    for (i = 0; i < nc; i++) {
        if (*v[i])
            for (; pad > 0; pad--) putchar(' ');
        fputs(v[i], stdout);
        n = strlen(v[i]);
        pad += w[i] + 2 - n;
        if (n > w[i]) pad = 2;
    }
    putchar('\n');
}

/* print a query's rows; 0 if it failed */
static int print(sqlstmt *st)
{
    int nc = sql_ncols(st), *w, i, j, n = 0, r;
    char **keep, **row;
    if (csv) {
        for (i = 0; i < nc; i++) csvout(sql_colname(st, i), i == nc - 1);
        while ((r = sql_next(st)) > 0)
            for (i = 0; i < nc; i++) csvout(cell(st, i), i == nc - 1);
        return r == 0;
    }
    /* columns: the first rows are kept to find the widths */
    w = malloc(nc * sizeof *w);
    row = malloc(nc * sizeof *row);
    keep = malloc(WROWS * nc * sizeof *keep);
    if (!w || !row || !keep) {
        fprintf(stderr, "sql: out of memory\n");
        exit(2);
    }
    for (i = 0; i < nc; i++) w[i] = strlen(sql_colname(st, i));
    while (n < WROWS && (r = sql_next(st)) > 0) {
        for (i = 0; i < nc; i++) {
            if (!(keep[n * nc + i] = strdup(cell(st, i)))) {
                fprintf(stderr, "sql: out of memory\n");
                exit(2);
            }
            if ((int)strlen(keep[n * nc + i]) > w[i]) w[i] = strlen(keep[n * nc + i]);
        }
        n++;
    }
    if (n) {
        for (i = 0; i < nc; i++) row[i] = (char *)sql_colname(st, i);
        padrow(row, w, nc);
        for (i = 0; i < nc; i++) {
            for (j = 0; j < w[i]; j++) putchar('-');
            if (i < nc - 1) fputs("  ", stdout);
        }
        putchar('\n');
        for (j = 0; j < n; j++) {
            padrow(keep + j * nc, w, nc);
            for (i = 0; i < nc; i++) free(keep[j * nc + i]);
        }
        if (n == WROWS)                 /* the rest as they come */
            while ((r = sql_next(st)) > 0) {
                for (i = 0; i < nc; i++) row[i] = (char *)cell(st, i);
                padrow(row, w, nc);
            }
    }
    free(w);
    free(row);
    free(keep);
    return r == 0;
}

/* ---------------------------------------------------------------- statements */

/* the first word of a statement, in upper case */
static void firstword(const char *s, char *w)
{
    int n = 0;
    for (;;) {
        while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
        if (s[0] == '-' && s[1] == '-') {
            while (*s && *s != '\n') s++;
        } else if (s[0] == '/' && s[1] == '*') {
            for (s += 2; *s && !(s[0] == '*' && s[1] == '/'); s++) ;
            if (*s) s += 2;
        } else
            break;
    }
    for (; n < 10 && ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z')); s++)
        w[n++] = *s >= 'a' ? *s - 32 : *s;
    w[n] = 0;
}

static void run(const char *text, const char *file, int line)
{
    char w[12];
    sqlstmt *st;
    firstword(text, w);
    if (!w[0] && !strchr(text, ';')) return;    /* nothing */
    if (!strcmp(w, "SELECT") || !strcmp(w, "SHOW") || !strcmp(w, "DESCRIBE") ||
        !strcmp(w, "DESC") || !strcmp(w, "EXPLAIN")) {
        if (!(st = sql_query(db, text))) {
            report(sql_errmsg(db), file, line);
            return;
        }
        if (!print(st)) report(sql_errmsg(db), file, line);
        sql_finish(st);
    } else if (sql_exec(db, text) < 0)
        report(sql_errmsg(db), file, line);
    else if (interactive && !depth && (!strcmp(w, "INSERT") || !strcmp(w, "UPDATE") || !strcmp(w, "DELETE")))
        printf("(%d row%s)\n", sql_changes(db), sql_changes(db) == 1 ? "" : "s");
    fflush(stdout);                     /* (a pipe sees each result as it is made) */
}

/* the end of the first whole statement in s (its ;), or -1 */
static int stmtend(const char *s)
{
    int i, q;
    for (i = 0; s[i]; i++) {
        if (s[i] == '\'' || s[i] == '"') {          /* a string: '' inside is a quote */
            for (q = s[i++]; s[i] && s[i] != q; i++) ;
            if (!s[i]) return -1;
        } else if (s[i] == '-' && s[i + 1] == '-') {
            while (s[i + 1] && s[i + 1] != '\n') i++;
        } else if (s[i] == '/' && s[i + 1] == '*') {
            for (i += 2; s[i] && !(s[i] == '*' && s[i + 1] == '/'); i++) ;
            if (!s[i]) return -1;
            i++;
        } else if (s[i] == ';')
            return i;
    }
    return -1;
}

static int blank(const char *s)
{
    for (; *s; s++)
        if (*s != ' ' && *s != '\t' && *s != '\n' && *s != '\r') return 0;
    return 1;
}

static void process(FILE *f, const char *file);

static void help(void)
{
    fputs("Statements end with ;. Some of them:\n"
          "  CREATE TABLE t (id INTEGER PRIMARY KEY, name VARCHAR(20) NOT NULL, ...)\n"
          "  CREATE [UNIQUE] INDEX name ON t (column)   DROP TABLE t   DROP INDEX name\n"
          "  INSERT INTO t [(columns)] VALUES (...), ...\n", stdout);
    fputs("  SELECT ... FROM t [JOIN u ON ...] [WHERE ...] [GROUP BY ...]\n"
          "         [ORDER BY ...] [LIMIT n]\n"
          "  UPDATE t SET column = value WHERE ...   DELETE FROM t WHERE ...\n"
          "  BEGIN  COMMIT  ROLLBACK  SHOW TABLES  SHOW INDEXES  DESCRIBE t\n"
          "  EXPLAIN SELECT ...\n", stdout);
    fputs("Commands:\n"
          "  .tables           the tables\n"
          "  .schema [table]   their CREATE statements\n"
          "  .read file        run the statements in a file\n"
          "  .mode column|csv  how rows are printed\n"
          "  .quit             leave (so does the end of the input)\n", stdout);
}

/* .schema: CREATE statements made from SHOW INDEXES and DESCRIBE. The
   indexes named t.pk and t.uN belong to the table's PRIMARY KEY and
   UNIQUE columns; the others were made by CREATE INDEX. */
static void schema(const char *table)
{
    char q[80], ix[16][3][24], *t;
    sqlstmt *st, *tl;
    int n, i, j, uni;
    if (!(tl = sql_query(db, "SHOW TABLES"))) {
        report(sql_errmsg(db), "", 0);
        return;
    }
    while (sql_next(tl) > 0) {
        t = (char *)sql_text(tl, 0);
        if (table && strcmp(table, t)) continue;
        n = 0;                          /* the indexes: name, column, "1" if unique */
        snprintf(q, sizeof q, "SHOW INDEXES FROM %s", t);
        if ((st = sql_query(db, q))) {
            for (; n < 16 && sql_next(st) > 0; n++)
                for (j = 0; j < 3; j++) snprintf(ix[n][j], 24, "%s", sql_text(st, j == 2 ? 3 : j == 1 ? 2 : 0));
            sql_finish(st);
        }
        printf("CREATE TABLE %s (\n", t);
        snprintf(q, sizeof q, "DESCRIBE %s", t);
        if ((st = sql_query(db, q))) {
            for (i = 0; sql_next(st) > 0; i++) {
                for (uni = j = 0; j < n; j++)
                    if (strstr(ix[j][0], ".u") && !strcmp(ix[j][1], sql_text(st, 0))) uni = 1;
                printf("%s  %s %s%s%s%s", i ? ",\n" : "", sql_text(st, 0), sql_text(st, 1),
                       !strcmp(sql_text(st, 3), "PRI") ? " PRIMARY KEY" : "",
                       !strcmp(sql_text(st, 2), "NO") && strcmp(sql_text(st, 3), "PRI") ? " NOT NULL" : "",
                       uni ? " UNIQUE" : "");
            }
            sql_finish(st);
        }
        printf("\n);\n");
        for (j = 0; j < n; j++)
            if (!strchr(ix[j][0], '.'))
                printf("CREATE %sINDEX %s ON %s (%s);\n", ix[j][2][0] == '1' ? "UNIQUE " : "", ix[j][0], t, ix[j][1]);
    }
    sql_finish(tl);
}

/* a line starting with . */
static void dot(char *line, const char *file, int lineno)
{
    char *cmd = strtok(line + 1, " \t\r\n"), *arg = strtok(0, " \t\r\n"), msg[240];
    sqlstmt *st;
    FILE *f;
    if (!cmd) return;
    if (!strcmp(cmd, "quit") || !strcmp(cmd, "exit")) {
        sql_close(db);
        exit(errors ? 1 : 0);
    } else if (!strcmp(cmd, "help"))
        help();
    else if (!strcmp(cmd, "tables")) {
        if (!(st = sql_query(db, "SHOW TABLES"))) report(sql_errmsg(db), file, lineno);
        else {
            while (sql_next(st) > 0) printf("%s\n", sql_text(st, 0));
            sql_finish(st);
        }
    } else if (!strcmp(cmd, "schema"))
        schema(arg);
    else if (!strcmp(cmd, "mode") && arg && (!strcmp(arg, "csv") || !strcmp(arg, "column")))
        csv = !strcmp(arg, "csv");
    else if (!strcmp(cmd, "read") && arg) {
        if (depth > 8) report(".read: too deep", file, lineno);
        else if (!(f = fopen(arg, "r"))) {
            snprintf(msg, sizeof msg, "%.200s: cannot open it", arg);
            report(msg, file, lineno);
        } else {
            depth++;
            process(f, arg);
            depth--;
            fclose(f);
        }
    } else
        report("unknown command (.help lists them)", file, lineno);
}

/* run the statements in a file (or the terminal) */
static void process(FILE *f, const char *file)
{
    struct buf b;
    char line[512];
    int lineno = 0, start = 0, e, prompt = interactive && !depth, n;
    memset(&b, 0, sizeof b);
    add(&b, "", 0);
    for (;;) {
        if (prompt) {
            fputs(blank(b.s) ? "sql> " : "   ...> ", stdout);
            fflush(stdout);
        }
        if (!fgets(line, sizeof line, f)) break;
        n = strlen(line);
        if (n && line[n - 1] == '\n') lineno++;
        if (blank(b.s) && line[0] == '.') {
            b.n = 0;
            b.s[0] = 0;
            dot(line, file, lineno);
            continue;
        }
        if (blank(b.s)) start = lineno + (n && line[n - 1] != '\n');
        add(&b, line, n);
        while ((e = stmtend(b.s)) >= 0) {   /* each whole statement */
            b.s[e] = 0;
            run(b.s, file, start);
            memmove(b.s, b.s + e + 1, b.n - e);
            b.n -= e + 1;
            start = lineno;
        }
    }
    if (!blank(b.s)) run(b.s, file, start);     /* a last one without ; */
    if (prompt) putchar('\n');
    free(b.s);
}

int main(int argc, char **argv)
{
    struct buf b;
    int i = 1, e, create = 0;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (!strcmp(argv[i], "-c")) create = 1;
        else if (!strcmp(argv[i], "-csv")) csv = 1;
        else break;
    }
    if (i >= argc || argv[i][0] == '-') {
        fprintf(stderr, "usage: sql [-c] [-csv] dbdir [statement ...]\n");
        return 2;
    }
    if (!(db = sql_open(argv[i], create))) {
        fprintf(stderr, "sql: %s\n", sql_errmsg(0));
        return 2;
    }
    if (i + 1 < argc) {
        depth = 1;                      /* (no prompts, no row counts) */
        for (i++; i < argc; i++) {
            memset(&b, 0, sizeof b);
            add(&b, argv[i], strlen(argv[i]));
            if (b.s[0] == '.')          /* a command */
                dot(b.s, "", 0);
            else {                      /* statements */
                while ((e = stmtend(b.s)) >= 0) {
                    b.s[e] = 0;
                    run(b.s, "", 0);
                    memmove(b.s, b.s + e + 1, b.n - e);
                    b.n -= e + 1;
                }
                if (!blank(b.s)) run(b.s, "", 0);
            }
            free(b.s);
        }
    } else {
        interactive = isatty(0);
        process(stdin, "line");
    }
    sql_close(db);
    return errors ? 1 : 0;
}
