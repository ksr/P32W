#!/bin/sh
# test.sh -- build the SQL library, sql and apitest with the host's cc and
# run the tests; their output must be test.expect.
#
#   sh test.sh        run the tests (exit 0 if all is as expected)
#   sh test.sh -u     the same, then write the output to test.expect
#
# The parts: test.sql (every statement, and errors); the database opened
# again (it is kept); the command line; a big table (B-tree splits, index
# against full scan, rows that move, long TEXT keys); the C API (apitest.c);
# a writer killed in the middle of a transaction (the next open rolls back).
# Everything happens in a temporary directory.

here=$(cd "$(dirname "$0")" && pwd)
tmp=${TMPDIR:-/tmp}/sqltest.$$
CC=${CC:-cc}
rm -rf "$tmp"
mkdir -p "$tmp" || exit 2
trap 'rm -rf "$tmp"' 0
cd "$tmp" || exit 2
lib="$here/sqldb.c $here/sqlselect.c $here/sqleval.c $here/sqlparse.c $here/sqlbtree.c $here/sqlstore.c"
$CC -O -w -o sql "$here/sql.c" $lib || exit 2
$CC -O -w -o apitest -I"$here" "$here/apitest.c" $lib || exit 2

# n rows for table big: id, k (in 0..999), s (half of them with a long
# common start, so their index keys are cut short and look the same)
bigrows() {
    awk -v n="$1" -v from="$2" 'BEGIN {
        for (i = from; i < from + n; i++) {
            if (i % 2) s = sprintf("a long common start that is longer than forty bytes %05d", i)
            else s = sprintf("k%05d", (i * 37) % 100000)
            printf "INSERT INTO big VALUES (%d, %d, '\''%s'\'');\n", i, (i * 7919) % 1000, s
        }
    }'
}

{
    echo "### test.sql"
    ./sql -c db < "$here/test.sql" 2>&1
    echo "exit $?"

    echo "### the database opened again"
    ./sql db "SELECT COUNT(*) FROM emp" "SELECT name FROM emp WHERE dept = 20 ORDER BY name" \
             "SHOW TABLES" ".tables" ".schema emp" 2>&1
    ls db

    echo "### the command line"
    ./sql db "SELECT 'open" 2>&1; echo "exit $?"
    ./sql nosuchdb "SELECT 1" 2>&1; echo "exit $?"
    ./sql 2>&1; echo "exit $?"
    ./sql -csv db "SELECT id, name FROM dept ORDER BY id LIMIT 2"
    ./sql db "SELECT 1; SELECT 2" "SELECT 3"
    echo "SELECT 'from a pipe';" | ./sql db
    printf 'SELECT 1 +\n  2\n  AS three;\nSELECT 4 -- no ; at the end' | ./sql db
    printf "SELECT 'in a file';\nSELECT nosuch FROM dept;\n" > r.sql
    printf '.read r.sql\n.read nofile\n.mode\nSELECT 5;\n' | ./sql db 2>&1

    echo "### a big table"
    {
        echo "CREATE TABLE big (id INTEGER PRIMARY KEY, k INTEGER NOT NULL, s TEXT);"
        echo "CREATE INDEX big_k ON big (k);"
        echo "BEGIN;"
        bigrows 1500 1
        echo "COMMIT;"
        echo "CREATE INDEX big_s ON big (s);"     # made from the rows there are
        bigrows 1500 1501
    } > big.sql
    ./sql -c bigdb < big.sql 2>&1
    cat > q.sql <<'EOF'
SELECT COUNT(*), SUM(k), MIN(s), MAX(id) FROM big;
SELECT COUNT(*), SUM(id) FROM big WHERE k = 500;
SELECT COUNT(*), SUM(id) FROM big WHERE k + 0 = 500;
SELECT COUNT(*), SUM(id) FROM big WHERE k BETWEEN 100 AND 199;
SELECT COUNT(*), SUM(id) FROM big WHERE k + 0 BETWEEN 100 AND 199;
SELECT COUNT(*), SUM(id) FROM big WHERE k > 990 OR k < 5;
SELECT id, k FROM big WHERE k >= 997 ORDER BY k DESC, id LIMIT 4;
SELECT id FROM big WHERE s = 'a long common start that is longer than forty bytes 01234';
SELECT COUNT(*) FROM big WHERE s >= 'a long common start that is longer than forty bytes 02000';
SELECT COUNT(*) FROM big WHERE s || '' >= 'a long common start that is longer than forty bytes 02000';
SELECT COUNT(*) FROM big WHERE s LIKE 'k00%';
SELECT b.id, c.id FROM big b JOIN big c ON c.id = b.k WHERE b.id < 4 ORDER BY b.id;
SELECT k / 100, COUNT(*) FROM big GROUP BY k / 100 ORDER BY 1 LIMIT 3;
EOF
    ./sql bigdb < q.sql 2>&1
    ./sql bigdb "EXPLAIN SELECT * FROM big WHERE s = 'x'" "EXPLAIN SELECT * FROM big b JOIN big c ON c.id = b.k"
    echo "-- delete half, make every third row longer (it moves), ask again"
    ./sql bigdb "DELETE FROM big WHERE k < 500" \
        "UPDATE big SET s = s || ' and now a good deal longer than it was before, so that it moves' WHERE id % 3 = 0" \
        "SHOW TABLES" < /dev/null 2>&1
    sed -e 's/forty bytes 01234/forty bytes 01235/' q.sql | ./sql bigdb 2>&1
    ./sql bigdb "SELECT COUNT(*) FROM big WHERE s LIKE '%moves'" \
        "CREATE UNIQUE INDEX big_su ON big (s)" \
        "INSERT INTO big VALUES (5000, 1, 'a long common start that is longer than forty bytes 01235')" \
        "INSERT INTO big VALUES (5000, 1, 'a long common start that is longer than forty bytes 01235 but not the same')" \
        "SELECT id, k FROM big WHERE s LIKE '%not the same'" 2>&1

    echo "### the C API"
    ./apitest apidb 2>&1

    echo "### a writer killed in the middle of a transaction"
    {
        echo "CREATE TABLE t (id INTEGER PRIMARY KEY, v TEXT);"
        awk 'BEGIN { for (i = 1; i <= 300; i++) printf "INSERT INTO t (v) VALUES (%c%0200d%c);\n", 39, i, 39 }'
    } | ./sql -c crashdb 2>&1
    ./sql crashdb "SELECT COUNT(*), SUM(id), SUM(LENGTH(v)) FROM t"
    mkfifo fifo
    ./sql crashdb < fifo > crash.out 2>&1 &
    pid=$!
    exec 3> fifo
    {
        echo "BEGIN;"
        echo "UPDATE t SET v = v || v;"
        echo "DELETE FROM t WHERE id % 2 = 0;"
        awk 'BEGIN { for (i = 1; i <= 300; i++) printf "INSERT INTO t (v) VALUES (%cnew%0300d%c);\n", 39, i, 39 }'
        echo "SELECT 'ready', COUNT(*) FROM t;"
    } >&3
    n=0
    while ! grep ready crash.out > /dev/null 2>&1 && [ $n -lt 60 ]; do sleep 1; n=$((n + 1)); done
    cat crash.out
    kill -9 $pid
    wait $pid 2> /dev/null
    exec 3>&-
    ls crashdb | grep -v tbl | grep -v idx
    ./sql crashdb "SELECT COUNT(*), SUM(id), SUM(LENGTH(v)) FROM t" "INSERT INTO t (v) VALUES ('after')" \
        "SELECT MAX(id) FROM t"
    ls crashdb
} > out 2>&1

if [ "$1" = -u ]; then
    cp out "$here/test.expect"
    echo "test.expect written"
fi
if cmp -s out "$here/test.expect"; then
    echo "sql: all tests passed"
else
    diff "$here/test.expect" out
    echo "sql: tests FAILED"
    exit 1
fi
