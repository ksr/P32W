-- test.sql -- the SQL library's tests (run by test.sh; the output must be
-- test.expect). Each part says what it checks; "error:" lines are wanted.
SELECT '== tables and types' AS test;
CREATE TABLE dept (id INTEGER PRIMARY KEY, name VARCHAR(10) NOT NULL UNIQUE, city TEXT);
CREATE TABLE emp (
  id INT PRIMARY KEY,
  name CHAR(12) NOT NULL,
  dept INTEGER,             -- the department's id
  salary INTEGER,
  boss INTEGER
);
CREATE TABLE emp (x INTEGER);
CREATE TABLE IF NOT EXISTS emp (x INTEGER);
CREATE TABLE bad (x FLOAT);
CREATE TABLE bad (x INTEGER PRIMARY KEY, y INTEGER PRIMARY KEY);
CREATE TABLE bad (x INTEGER DEFAULT 3);
CREATE TABLE bad (x INTEGER, x TEXT);
CREATE TABLE "a-b" (x INTEGER);
CREATE TABLE averyveryverylongname1 (x INTEGER);
SHOW TABLES;
DESCRIBE emp;
DESCRIBE dept;

SELECT '== insert' AS test;
INSERT INTO dept VALUES (10, 'eng', 'Boston'), (20, 'ops', 'Austin'), (30, 'sales', NULL);
INSERT INTO dept (name) VALUES ('empty');
INSERT INTO emp VALUES (1, 'ann', 10, 120, NULL);
INSERT INTO emp (id, name, dept, salary, boss) VALUES
  (2, 'bob', 10, 100, 1), (3, 'cy', 20, 90, 1), (4, 'dee', 20, 95, 3),
  (5, 'ed', NULL, 60, 1), (6, 'flo', 30, '75', 1);
INSERT INTO emp (name, salary) VALUES ('gus', NULL);
INSERT INTO emp VALUES (1, 'dup', 10, 1, NULL);
INSERT INTO emp (id) VALUES (99);
INSERT INTO emp VALUES (8, 'thirteenchars', 10, 1, NULL);
INSERT INTO emp VALUES (8, 'h', 10, 'lots', NULL);
INSERT INTO emp VALUES (8, 'h', 10);
INSERT INTO emp (id, nosuch) VALUES (8, 1);
INSERT INTO emp (id, id) VALUES (8, 9);
INSERT INTO dept VALUES (50, 'eng', 'Paris');
INSERT INTO nosuch VALUES (1);
-- a failed row undoes the whole statement: 9 must not be there
INSERT INTO emp (id, name) VALUES (9, 'ok'), (1, 'dup');
SELECT * FROM emp;
SELECT * FROM dept;

SELECT '== expressions' AS test;
SELECT 1 + 2 * 3, (1 + 2) * 3, 7 / 2, -7 / 2, 7 % 3, -7 % 3, - - 4, +5;
SELECT 'ab' || 'cd' || 12, 'x' || NULL, 2147483647, -2147483648;
SELECT 2147483647 + 1;
SELECT -2147483648 - 1;
SELECT 65536 * 65536;
SELECT 1 / 0;
SELECT 5 % 0;
SELECT 'abc' + 1;
SELECT '12' + 1, ' 7 ' * 2, '3' = 3, '10' > 9, 'abc' > 999999;
SELECT 1 = 1, 1 <> 1, 1 != 2, 2 < 1, 2 <= 2, 3 > 2, 3 >= 4, 'a' < 'b', 'B' < 'a';
SELECT NULL = NULL, NULL IS NULL, 1 IS NOT NULL, NULL + 1, NOT NULL;
SELECT NULL AND 0, NULL AND 1, NULL OR 1, NULL OR 0, 1 AND 1, 0 OR 0, NOT 0, NOT 3;
SELECT 3 BETWEEN 1 AND 5, 3 NOT BETWEEN 1 AND 5, 'm' BETWEEN 'a' AND 'k', NULL BETWEEN 1 AND 2;
SELECT 2 IN (1, 2, 3), 4 IN (1, 2), 4 NOT IN (1, 2), 4 IN (1, NULL), 1 IN (1, NULL), NULL IN (1);
SELECT 'Hello' LIKE 'h%', 'Hello' LIKE '_ell_', 'Hello' LIKE 'H%x', 'a%b' LIKE 'a%', 'abc' NOT LIKE '%b%', '' LIKE '%';
SELECT UPPER('MiXed'), LOWER('MiXed'), LENGTH('hello'), LENGTH(12345), ABS(-5), ABS(5), ABS(NULL);
SELECT SUBSTR('hello', 2), SUBSTR('hello', 2, 3), SUBSTR('hello', 0, 2), SUBSTR('hello', -3), SUBSTR('hello', 9), SUBSTR('hello', 2, -1);
SELECT COALESCE(NULL, NULL, 'third'), IFNULL(NULL, 0), IFNULL(5, 0), COALESCE(NULL);
SELECT NOSUCH(1);
SELECT UPPER(1, 2);
SELECT 'it''s', "name" FROM dept WHERE id = 10;
SELECT 2147483648;
SELECT ABS(-2147483648);

SELECT '== where, order, limit' AS test;
SELECT name, salary FROM emp WHERE salary > 90 ORDER BY salary DESC;
SELECT name FROM emp WHERE dept IS NULL ORDER BY name;
SELECT name FROM emp WHERE dept = NULL;
SELECT name FROM emp WHERE NOT (salary < 100) ORDER BY 1;
SELECT name, salary FROM emp WHERE salary BETWEEN 75 AND 100 AND name LIKE '%e%' OR id = 1 ORDER BY id;
SELECT name FROM emp WHERE id IN (2, 4, 6) ORDER BY name DESC;
SELECT name, salary FROM emp ORDER BY salary, name;
SELECT name, salary FROM emp ORDER BY salary DESC LIMIT 3;
SELECT name, salary FROM emp ORDER BY salary DESC LIMIT 2 OFFSET 2;
SELECT name FROM emp ORDER BY id LIMIT 2, 3;
SELECT name FROM emp LIMIT 2;
SELECT name AS who, salary * 12 AS yearly FROM emp WHERE id < 4 ORDER BY yearly DESC;
SELECT name FROM emp ORDER BY 3;
SELECT DISTINCT dept FROM emp ORDER BY dept;
SELECT DISTINCT boss, dept FROM emp WHERE boss = 1;
SELECT id, name FROM emp WHERE name > 'd' AND id <= 5 ORDER BY name;
SELECT name FROM emp WHERE salary * 2 > 200;
SELECT name FROM emp WHERE nosuch = 1;
SELECT * FROM nosuch;

SELECT '== aggregates' AS test;
SELECT COUNT(*), COUNT(dept), COUNT(salary), SUM(salary), MIN(salary), MAX(salary), AVG(salary) FROM emp;
SELECT dept, COUNT(*) AS n, SUM(salary), AVG(salary) FROM emp GROUP BY dept ORDER BY dept;
SELECT dept, COUNT(*) FROM emp GROUP BY dept HAVING COUNT(*) > 1 ORDER BY 2 DESC, dept;
SELECT dept, MAX(name), MIN(name) FROM emp WHERE dept IS NOT NULL GROUP BY dept ORDER BY dept DESC;
SELECT COUNT(*), SUM(salary), MAX(name) FROM emp WHERE id > 100;
SELECT dept, COUNT(*) FROM emp WHERE id > 100 GROUP BY dept;
SELECT COUNT(*) * 10 + 1, MAX(salary) - MIN(salary) FROM emp;
SELECT boss, COUNT(*) FROM emp GROUP BY boss ORDER BY COUNT(*) DESC, boss;
SELECT dept % 20, COUNT(*) FROM emp WHERE dept IS NOT NULL GROUP BY dept % 20 ORDER BY 1;
SELECT name FROM emp WHERE COUNT(*) > 1;
SELECT COUNT(SUM(salary)) FROM emp;
SELECT COUNT(DISTINCT dept) FROM emp;
SELECT name, COUNT(*) FROM emp HAVING COUNT(*) > 100;

SELECT '== joins' AS test;
SELECT emp.name, dept.name FROM emp JOIN dept ON emp.dept = dept.id ORDER BY emp.name;
SELECT e.name, d.city FROM emp e, dept d WHERE e.dept = d.id AND d.city = 'Austin' ORDER BY 1;
SELECT e.name, d.name AS dname FROM emp AS e LEFT JOIN dept AS d ON e.dept = d.id ORDER BY e.id;
SELECT d.name, COUNT(e.id) FROM dept d LEFT JOIN emp e ON e.dept = d.id GROUP BY d.name ORDER BY d.name;
SELECT w.name, b.name AS boss FROM emp w JOIN emp b ON w.boss = b.id WHERE b.name <> 'ann' ORDER BY w.name;
SELECT d.name, e.name FROM dept d INNER JOIN emp e ON e.dept = d.id AND e.salary > 95 ORDER BY 1, 2;
SELECT COUNT(*) FROM emp, dept;
SELECT * FROM emp e JOIN dept d ON e.dept = d.id WHERE e.id = 4;
SELECT d.* FROM emp e JOIN dept d ON e.dept = d.id WHERE e.id = 4;
SELECT name FROM emp, dept;
SELECT x.name FROM emp e JOIN dept d ON e.dept = d.id;
SELECT * FROM emp JOIN emp ON 1 = 1;
EXPLAIN SELECT e.name FROM emp e JOIN dept d ON e.dept = d.id;
EXPLAIN SELECT e.name FROM dept d JOIN emp e ON e.dept = d.id WHERE d.city = 'Austin';
EXPLAIN SELECT * FROM emp e LEFT JOIN dept d ON e.dept = d.id;

SELECT '== indexes' AS test;
EXPLAIN SELECT * FROM emp WHERE dept = 20;
CREATE INDEX emp_dept ON emp (dept);
CREATE INDEX emp_dept ON emp (salary);
CREATE INDEX IF NOT EXISTS emp_dept ON emp (salary);
CREATE INDEX emp_x ON emp (nosuch);
CREATE UNIQUE INDEX emp_boss ON emp (boss);
CREATE INDEX emp_name ON emp (name);
EXPLAIN SELECT * FROM emp WHERE dept = 20;
EXPLAIN SELECT * FROM emp WHERE 20 = dept AND salary > 0;
EXPLAIN SELECT * FROM emp WHERE dept > 10 AND dept <= 30;
EXPLAIN SELECT * FROM emp WHERE name BETWEEN 'b' AND 'e' ORDER BY name;
EXPLAIN SELECT * FROM emp WHERE dept = 20 OR dept = 30;
EXPLAIN SELECT * FROM emp WHERE id = 3;
EXPLAIN SELECT dept, COUNT(*) FROM emp GROUP BY dept;
SELECT name FROM emp WHERE dept = 20 ORDER BY name;
SELECT name FROM emp WHERE dept = '20' ORDER BY name;
SELECT name FROM emp WHERE dept > 10 AND dept <= 30 ORDER BY name;
SELECT name FROM emp WHERE dept < 20;
SELECT name FROM emp WHERE name BETWEEN 'b' AND 'e';
SELECT name FROM emp WHERE name >= 'dee';
SELECT name FROM emp WHERE name = 'cy';
SELECT name FROM emp WHERE dept = NULL;
SELECT name FROM emp WHERE id = 3;
SHOW INDEXES;
SHOW INDEXES FROM dept;
DROP INDEX emp_name;
DROP INDEX emp_name;
DROP INDEX IF EXISTS emp_name;
DROP INDEX "emp.pk";
SHOW INDEXES FROM emp;

SELECT '== update, delete' AS test;
UPDATE emp SET salary = salary + 10 WHERE dept = 20;
SELECT name, salary FROM emp WHERE dept = 20 ORDER BY name;
UPDATE emp SET dept = 30, name = UPPER(name) WHERE name = 'ed';
SELECT id, name, dept FROM emp WHERE dept = 30 ORDER BY id;
SELECT name FROM emp WHERE dept IS NULL;
UPDATE emp SET id = 2 WHERE id = 3;
UPDATE emp SET name = NULL WHERE id = 3;
UPDATE emp SET salary = 'x' WHERE id = 3;
UPDATE emp SET nosuch = 1;
UPDATE emp SET id = id + 100 WHERE id = 7;
SELECT id, name FROM emp WHERE id > 6;
INSERT INTO emp (name) VALUES ('hal');
SELECT id, name FROM emp WHERE name = 'hal';
UPDATE dept SET city = 'a much longer city name than before ' || city WHERE id = 10;
SELECT * FROM dept WHERE id = 10;
DELETE FROM emp WHERE dept = 30;
SELECT id, name, dept FROM emp ORDER BY id;
SELECT name FROM emp WHERE dept = 30;
DELETE FROM emp WHERE id = 12345;
DELETE FROM nosuch;
SELECT COUNT(*) FROM emp;

SELECT '== transactions' AS test;
BEGIN;
INSERT INTO dept VALUES (40, 'temp', 'Rome');
UPDATE emp SET salary = 0;
SELECT COUNT(*) FROM dept;
SELECT SUM(salary) FROM emp;
ROLLBACK;
SELECT COUNT(*) FROM dept;
SELECT SUM(salary) FROM emp;
BEGIN TRANSACTION;
INSERT INTO dept VALUES (40, 'kept', 'Rome');
CREATE TABLE scratch (a INTEGER);
INSERT INTO scratch VALUES (1), (2);
COMMIT;
SELECT * FROM dept WHERE id = 40;
SELECT * FROM scratch;
-- an error that changed nothing leaves the transaction open
BEGIN;
INSERT INTO dept VALUES (41, 'kept2', NULL);
SELECT nosuch FROM dept;
INSERT INTO dept VALUES (10, 'clash', NULL);
COMMIT;
SELECT id, name FROM dept WHERE id > 39 ORDER BY id;
-- an error after a statement changed something rolls the transaction back
BEGIN;
INSERT INTO dept VALUES (42, 'gone', NULL);
INSERT INTO dept VALUES (43, 'gone2', NULL), (10, 'clash', NULL);
SELECT id, name FROM dept WHERE id > 39 ORDER BY id;
ROLLBACK;
BEGIN;
BEGIN;
DROP TABLE scratch;
CREATE INDEX dept_city ON dept (city);
ROLLBACK;
SELECT * FROM scratch;
SHOW INDEXES FROM dept;
BEGIN;
DROP TABLE scratch;
COMMIT;
SELECT * FROM scratch;
DROP TABLE scratch;
DROP TABLE IF EXISTS scratch;

SELECT '== syntax errors' AS test;
SELEC 1;
SELECT FROM emp;
SELECT 1 +;
SELECT (1;
SELECT 1 2;
SELECT * FROM emp WHERE;
SELECT * FROM emp ORDER;
INSERT emp VALUES (1);
CREATE TABLE;
SELECT 1 # 2;
SELECT 1 FROM emp a, dept b, emp c;
SELECT * FROM emp LEFT JOIN dept;
SELECT 99999999999;
SELECT 1.5;
UPDATE emp SET;
;;
SELECT 'after errors' /* a comment */ , 1; -- and another

SELECT '== introspection' AS test;
SHOW TABLES;
DESCRIBE nosuch;
SHOW COLUMNS FROM dept;
.tables
.schema
.schema dept
.mode csv
SELECT id, name, city FROM dept ORDER BY id;
SELECT 'a,b', 'say "hi"', NULL, 3;
.mode column
.nosuch
SELECT 'done';
