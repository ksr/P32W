/* people.c -- the SQL library from C: cc -o people people.c -lsql */
#include <stdio.h>
#include <sqldb.h>

int main(void)
{
    sqldb *db = sql_open("shop", 1);
    sqlstmt *st;
    if (!db) { printf("%s\n", sql_errmsg(0)); return 1; }
    st = sql_query(db, "SELECT name, qty FROM items WHERE qty > 2 ORDER BY qty DESC");
    if (!st) { printf("%s\n", sql_errmsg(db)); return 1; }
    while (sql_next(st) == 1)
        printf("%-8s %d\n", sql_text(st, 0), sql_int(st, 1));
    sql_finish(st);
    sql_close(db);
    return 0;
}
