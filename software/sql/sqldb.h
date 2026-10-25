/* sqldb.h -- a small SQL database for P32W Unix (see sqldb(3)).
 *
 * A database is a directory; each table is one file (NAME.tbl) and each
 * index another (NAME.idx). Statements are run with sql_exec; queries
 * (SELECT, SHOW, DESCRIBE, EXPLAIN) with sql_query, then sql_next to step
 * through the rows. Link with libsql.a (cc prog.c -lsql). */
#ifndef _SQLDB_H
#define _SQLDB_H

#define SQL_NULL    0               /* types of a value (sql_coltype) */
#define SQL_INTEGER 1
#define SQL_TEXT    2

typedef struct sqldb sqldb;         /* an open database */
typedef struct sqlstmt sqlstmt;     /* a query being read: a result set */

sqldb *sql_open(const char *dir, int create);  /* 0 on error: sql_errmsg(0) */
void sql_close(sqldb *db);
int sql_exec(sqldb *db, const char *sql);       /* 0 ok, -1 error */
sqlstmt *sql_query(sqldb *db, const char *sql); /* 0 on error */
int sql_next(sqlstmt *st);                      /* 1 row, 0 end, -1 error */
int sql_ncols(sqlstmt *st);
const char *sql_colname(sqlstmt *st, int i);
int sql_isnull(sqlstmt *st, int i);
int sql_coltype(sqlstmt *st, int i);
int sql_int(sqlstmt *st, int i);
const char *sql_text(sqlstmt *st, int i);       /* valid until the next sql_next */
void sql_finish(sqlstmt *st);
int sql_changes(sqldb *db);
const char *sql_errmsg(sqldb *db);

#endif
