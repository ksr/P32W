# sql/ — a small SQL database for P32W Unix

A library (`libsql.a`, header `sqldb.h`) that keeps tables in a directory and
runs SQL on them, in the spirit of a tiny SQLite or dBASE, and a command,
`sql`, to use it by hand or from scripts. Manual pages: `sql.1`, `sqldb.3`.

```
$ sql -c shop
sql> CREATE TABLE item (id INTEGER PRIMARY KEY, name TEXT NOT NULL, price INTEGER);
sql> INSERT INTO item (name, price) VALUES ('pen', 3), ('ink', 7);
(2 rows)
sql> SELECT name, price FROM item WHERE price > 5;
name  price
----  -----
ink   7
```

## Files

| file | what |
|------|------|
| `sqldb.h` | the API (installed as `/usr/include/sqldb.h`) |
| `sqlint.h` | the library's inside: limits, file formats, structures |
| `sqlstore.c` | memory and errors; the pager (page cache, journal, lock); table files |
| `sqlbtree.c` | indexes: B+trees |
| `sqlparse.c` | the lexer and parser: SQL text to trees |
| `sqleval.c` | values, expressions, functions, column names, the planner, table cursors |
| `sqlselect.c` | SELECT: joins, grouping, aggregates, DISTINCT, sorting |
| `sqldb.c` | INSERT, UPDATE, DELETE, CREATE, DROP, SHOW, transactions; the API |
| `sql.c` | the `sql` command (it uses only the API) |
| `apitest.c` | a test (and example) of the C API |
| `Makefile` | building on P32W: `make`, `make install`, `make apitest` |
| `test.sh`, `test.sql`, `test.expect` | the tests, run on the host |

The library is six files because the native `cc` keeps a whole file in memory:
each file here needs at most 1.9 MB of its heap (measured on P32W with
`CCMEM`), well inside a process's 8 MiB.

## Design

**A database is a directory.** Each table is a file `NAME.tbl`, each index a
file `NAME.idx`; while a process writes, there is also `lock` and `journal`.
Every file is an array of 2048-byte pages, page 0 a header starting with a
magic number (`P32T`, `P32I`) and the number of pages in use. Integers on
disk are 32 bits, little-endian (except in index keys).

**Tables.** The header holds the columns (name, type, NOT NULL / PRIMARY
KEY / UNIQUE, length limit), the list of the table's indexes, the number of
rows, the next INTEGER PRIMARY KEY number and a page that may have room.
Rows live in slotted pages: a count of slots, the lowest used byte, then
slots of (offset, length) growing up while rows are packed down from the end
of the page. A row's rowid is `page * 256 + slot`. A record is, per column, a
tag byte (0 NULL, 1 INTEGER + 4 bytes, 2 TEXT + 2-byte length + bytes + NUL).
A deleted row frees its slot; a page is compacted when a row needs the room.
An insert tries the hinted page, then the last page, then a new page.

**Indexes.** A B+tree: nodes are pages of fixed-size entries (key, rowid),
inner nodes with a child pointer per entry; leaves are chained for range
scans. Keys compare with `memcmp`: an INTEGER is stored big-endian with its
sign bit flipped; a TEXT is its first 40 bytes, zero-padded (longer values
share a key and are told apart by the row). NULLs are not indexed. A full
node splits in half; a deleted entry just leaves its leaf (no merging).
PRIMARY KEY and UNIQUE columns get indexes of their own (`t.pk`, `t.uN`).

**Queries.** The parser makes trees; column names are resolved to places in
a row. The planner looks at the WHERE (and ON) conditions ANDed together for
`col = expr`, `col < / <= / > / >= expr` or `col BETWEEN a AND b` on an
indexed column, where `expr` reads no column of the same table: a SEARCH
then reads just that part of the index; otherwise a SCAN reads every row.
Bounds are inclusive and the conditions are still tested on every row, so
the index only has to be a superset. A join (two tables) is nested loops;
the inner table uses an index on the column the condition compares with the
outer row, and the order of the loops is swapped when only the first table
has such an index (not for LEFT JOIN). `EXPLAIN SELECT` shows the plan.
Rows are produced one per `sql_next`, except that GROUP BY, aggregates and
ORDER BY collect the whole result first (groups in a hash table, then
`qsort`). UPDATE and DELETE collect the rowids first, then change the rows.

**Writing, locking, crashes.** A process that writes takes the lock: it
creates `lock` with `O_CREAT|O_EXCL` and writes its pid in it. Another writer
waits for it (up to 10 seconds, then an error); a lock whose process is gone
(`kill(pid, 0)` fails with ESRCH) is taken over. Before a page that existed
when the transaction began is first changed, its old contents are appended
to `journal`. New files are noted in the journal; dropped files are renamed
to `~N.del`. Commit writes the changed pages, then removes the journal (that
is the moment of commit), then the dropped files. Rollback (or an error in a
statement, or the next process to find a journal with no live writer)
writes the saved pages back, removes new files and renames dropped ones
back. Files never shrink; the header's page count says what is in use.
Every statement outside BEGIN … COMMIT is its own transaction.

What is guaranteed: a statement (or a transaction) is applied entirely or
not at all if the process dies — killed, crashed, out of memory. Against a
crash of the whole system it holds as far as P32W Unix writes files in the
order they were written (there is no fsync). Readers take no lock: a query
running while another process writes may see part of that change (or, at
worst, stop with a "damaged" error); the database itself is not harmed.
Damage is caught where it is cheap: magic numbers, page types, slot and
record bounds, index node sizes and loops — an error, not a crash (the
tests flip random bytes in the files to check this).

## Limits and what is left out

- Types: INTEGER (32 bits) and TEXT (≤ 1000 bytes stored, ≤ 4000 while
  computing); CHAR(n)/VARCHAR(n) limit the length. No floating point, no
  dates, no BLOBs. AVG is an integer (truncated toward zero).
- Names ≤ 20 characters (table and index names are file names: letters,
  digits, `_`); ≤ 32 columns and ≤ 16 indexes a table; a row ≤ 2036 bytes.
- Joins of at most two tables; no subqueries, UNION, views, CASE,
  ALTER TABLE, DEFAULT, foreign keys, multi-column keys or indexes,
  `COUNT(DISTINCT x)`, `INSERT … SELECT`.
- Inside a transaction an error that had changed something rolls back the
  whole transaction (there are no savepoints); one that changed nothing
  (a syntax error, a duplicate key found before writing) leaves it open.
- CREATE/DROP is refused while the same handle has a query open.
- Space of deleted rows is reused within its page and by the hint, but
  files never shrink and there is no VACUUM; index leaves are not merged.
- Sorting and grouping keep their result in memory (a process has 8 MiB).
- Two handles on one database in one process cannot both write (the
  second gets an error at once rather than waiting).

## Building and testing

On the Mac: `sh test.sh` builds `sql` and `apitest` with the host `cc`,
runs `test.sql` and the other parts (persistence, the command line, a
3000-row table with index splits and long keys, the C API, a writer killed
with `kill -9` in the middle of a transaction) and compares everything with
`test.expect` (`sh test.sh -u` rewrites it). Every file also compiles with
`cc/p32cc.py`.

On P32W (sources in a directory, e.g. `/usr/src/sql`):

```
make                 # libsql.a and sql
make install         # as root: /usr/lib/libsql.a, /usr/include/sqldb.h, /bin/sql
cc -o prog prog.c -lsql
```

The same `test.sql` run by `sql` on P32W (built there by its own `cc`) gives
output identical to the host's.

## sqlui — a full-screen interface

`sqlui [-c] dbdir` (manual page `sqlui.1`) is the database on a whole VT100
screen, in the spirit of dBASE's ASSIST and BROWSE: the tables with their
row counts; a table's rows as a grid (a screenful read at a time with
`LIMIT`/`OFFSET`, so tables of any size), with a filter (`WHERE`) and an
order (`ORDER BY`); a form to edit or add a record; a line for any SQL
statement, its rows shown in the same grid. `?` shows the keys anywhere.

| file | what |
|------|------|
| `sqlui.c` | the screens: tables, browse grid, record form, query line |
| `sqluiscr.c` | the terminal (CBREAK, no echo, put back on every exit and on ^C), the screen (only changed lines are sent again), the line editor, SQL quoting |
| `sqlui.h` | what the two share |
| `sqlui_test.py` | the test, on the host: builds sqlui, drives it in a pseudo-terminal through a small VT100 interpreter |

It uses only the API. A row is found again for UPDATE and DELETE by its
PRIMARY KEY (the engine has no rowid to offer); in a table without one, by
all of its values, and identical rows are counted and confirmed first. On
P32W: `make sqlui` (`cc -o sqlui sqlui.c sqluiscr.c libsql.a`).
