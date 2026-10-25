/* apitest -- try the C API of sqldb(3); run by test.sh, whose test.expect
 * holds what it must print. Also an example of the API in use.
 *
 *   apitest dir      (dir must not exist: it is made)
 */
#include <stdio.h>
#include <string.h>
#include "sqldb.h"

static sqldb *db;

static void exec(const char *sql)
{
    int r = sql_exec(db, sql);
    printf("exec %-50.50s -> %d", sql, r);
    if (r < 0) printf(" (%s)", sql_errmsg(db));
    else printf(" changes %d", sql_changes(db));
    printf("\n");
}

/* print a query's rows, with each value's type */
static void query(const char *sql)
{
    static const char *tn[] = { "null", "int", "text" };
    sqlstmt *st = sql_query(db, sql);
    int i, r;
    printf("query %s\n", sql);
    if (!st) {
        printf("  error: %s\n", sql_errmsg(db));
        return;
    }
    printf(" ");
    for (i = 0; i < sql_ncols(st); i++) printf(" [%s]", sql_colname(st, i));
    printf("\n");
    while ((r = sql_next(st)) > 0) {
        printf(" ");
        for (i = 0; i < sql_ncols(st); i++)
            printf(" %s:%s/%d%s", tn[sql_coltype(st, i)], sql_text(st, i), sql_int(st, i),
                   sql_isnull(st, i) ? "/null" : "");
        printf("\n");
    }
    printf("  end %d%s%s\n", r, r < 0 ? ": " : "", r < 0 ? sql_errmsg(db) : "");
    printf("  again %d\n", sql_next(st));
    sql_finish(st);
}

int main(int argc, char **argv)
{
    sqldb *db2;
    sqlstmt *st;
    int n;
    if (argc != 2) return 2;
    if (!sql_open(argv[1], 0)) printf("open without create: %s\n", sql_errmsg(0));
    if (!(db = sql_open(argv[1], 1))) {
        printf("open: %s\n", sql_errmsg(0));
        return 1;
    }
    /* several statements in one string */
    exec("CREATE TABLE p (id INTEGER PRIMARY KEY, name TEXT, n INTEGER);"
         "INSERT INTO p (name, n) VALUES ('one', 1), ('two', NULL), ('3', 3)");
    exec("UPDATE p SET n = n * 10 WHERE n IS NOT NULL");
    exec("SELECT * FROM p");
    exec("SELEKT");
    query("SELECT id, name, n, n + id AS sum, 'x' || name FROM p ORDER BY id");
    query("SELECT COUNT(*), MAX(name) FROM p");
    query("SHOW TABLES");
    query("INSERT INTO p VALUES (9, 'no', 0)");
    query("SELECT 1; SELECT 2");
    query("SELECT id / 0 FROM p");
    /* a query stays open while other statements run */
    st = sql_query(db, "SELECT name FROM p ORDER BY id");
    sql_next(st);
    printf("open query: first row %s\n", sql_text(st, 0));
    exec("INSERT INTO p (name) VALUES ('four')");
    exec("CREATE TABLE q (a INTEGER)");
    for (n = 1; sql_next(st) > 0; n++) printf("open query: row %d %s\n", n + 1, sql_text(st, 0));
    printf("past the end: [%s] %d %d\n", sql_text(st, 0), sql_isnull(st, 0), sql_coltype(st, 0));
    sql_finish(st);
    exec("CREATE TABLE q (a INTEGER)");
    /* a rollback cancels a query opened in the transaction */
    exec("BEGIN");
    exec("CREATE TABLE r (a INTEGER)");
    st = sql_query(db, "SELECT * FROM p");
    exec("ROLLBACK");
    printf("after rollback: next %d (%s)\n", sql_next(st), sql_errmsg(db));
    sql_finish(st);
    /* one writer: a second handle cannot write while the first holds a transaction */
    db2 = sql_open(argv[1], 0);
    exec("BEGIN");
    exec("INSERT INTO q VALUES (1)");
    printf("second handle write: %d (%s)\n", sql_exec(db2, "INSERT INTO q VALUES (2)"), sql_errmsg(db2));
    exec("COMMIT");
    printf("second handle write: %d\n", sql_exec(db2, "INSERT INTO q VALUES (2)"));
    sql_close(db2);
    query("SELECT a FROM q ORDER BY a");
    printf("bad arguments: %d %s %d [%s]\n", sql_ncols(0), sql_next(0) < 0 ? "ok" : "bad",
           sql_exec(0, "SELECT 1"), sql_colname(0, 0));
    sql_close(db);
    db = sql_open(argv[1], 0);          /* again: the data is kept */
    query("SELECT name FROM p WHERE id = 4");
    sql_close(db);
    return 0;
}
