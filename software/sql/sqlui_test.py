#!/usr/bin/env python3
"""sqlui_test.py -- test sqlui on the host under a pseudo-terminal.

Builds sqlui and sql with the host's cc (in a temporary directory), makes a
database, then drives sqlui with keystrokes: the output is run through a
small VT100 interpreter and the screen is checked. Also checks that the
terminal's modes are the same after sqlui as before it (on Q and on ^C).

    python3 sqlui_test.py        exit 0 if every check passed
    python3 sqlui_test.py -v     also print the screen at each step
"""
import os, pty, re, select, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
LIB = ["sqldb.c", "sqlselect.c", "sqleval.c", "sqlparse.c", "sqlbtree.c", "sqlstore.c"]
VERBOSE = "-v" in sys.argv
failures = 0


class Screen:
    """Enough of a VT100 for sqlui: CUP, EL, ED, SGR 0/1/7, BEL, CR, LF."""

    def __init__(self, rows, cols):
        self.rows, self.cols = rows, cols
        self.ch = [[" "] * cols for _ in range(rows)]
        self.rev = [[False] * cols for _ in range(rows)]
        self.r = self.c = 0
        self.attr = False
        self.pending = b""

    def feed(self, data):
        data = self.pending + data
        self.pending = b""
        i = 0
        while i < len(data):
            b = data[i]
            if b == 27:
                m = re.match(rb"\x1b\[([0-9;?]*)([A-Za-z])", data[i:])
                if not m:
                    if len(data) - i < 12:
                        self.pending = data[i:]
                        return
                    i += 1
                    continue
                self.csi(m.group(1).decode(), m.group(2).decode())
                i += m.end()
                continue
            if b == 13:
                self.c = 0
            elif b == 10:
                self.r = min(self.r + 1, self.rows - 1)
            elif b == 8:
                self.c = max(self.c - 1, 0)
            elif b >= 32:
                if self.c < self.cols:
                    self.ch[self.r][self.c] = chr(b)
                    self.rev[self.r][self.c] = self.attr
                    self.c += 1
            i += 1

    def csi(self, args, op):
        nums = [int(x) if x.isdigit() else 0 for x in args.split(";")] if args else []
        if op == "H":
            self.r = (nums[0] if nums and nums[0] else 1) - 1
            self.c = (nums[1] if len(nums) > 1 and nums[1] else 1) - 1
        elif op == "K":
            for c in range(self.c, self.cols):
                self.ch[self.r][c] = " "
                self.rev[self.r][c] = False
        elif op == "J":
            for r in range(self.rows):
                self.ch[r] = [" "] * self.cols
                self.rev[r] = [False] * self.cols
        elif op == "m":
            for n in nums or [0]:
                if n == 0:
                    self.attr = False
                elif n == 7:
                    self.attr = True

    def line(self, r):
        return "".join(self.ch[r]).rstrip()

    def text(self):
        return "\n".join(self.line(r) for r in range(self.rows))

    def isrev(self, r):
        return self.rev[r][0]


class UI:
    def __init__(self, workdir, args, rows=24, cols=80):
        self.screen = Screen(rows, cols)
        env = dict(os.environ, LINES=str(rows), COLUMNS=str(cols), TERM="vt100")
        # the shell notes the terminal's modes before and after (and ignores ^C itself)
        cmd = "trap '' INT; stty -g > before; ./sqlui %s; echo $? > status; stty -g > after" % args
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.chdir(workdir)
            os.execve("/bin/sh", ["sh", "-c", cmd], env)
        self.read(1.0)

    def read(self, quiet=0.3, limit=10.0):
        end = time.time() + limit
        while time.time() < end:
            r, _, _ = select.select([self.fd], [], [], quiet)
            if not r:
                return
            try:
                data = os.read(self.fd, 65536)
            except OSError:
                return
            if not data:
                return
            self.screen.feed(data)

    def send(self, keys, quiet=0.3):
        for k in keys if isinstance(keys, list) else [keys]:
            os.write(self.fd, k.encode() if isinstance(k, str) else k)
            self.read(quiet)
        if VERBOSE:
            print("---- after %r\n%s" % (keys, self.screen.text()))

    def wait(self):
        self.read(0.5)
        _, st = os.waitpid(self.pid, 0)
        os.close(self.fd)
        return st


UP, DOWN, RIGHT, LEFT = "\x1b[A", "\x1b[B", "\x1b[C", "\x1b[D"
PGDN, PGUP, ENTER, ESC, TAB = "\x1b[6~", "\x1b[5~", "\r", "\x1b", "\t"


def check(what, ok, ui=None):
    global failures
    if ok:
        print("ok    %s" % what)
    else:
        failures += 1
        print("FAIL  %s" % what)
        if ui:
            print(ui.screen.text())


def has(ui, s):
    return s in ui.screen.text()


def main():
    tmp = tempfile.mkdtemp(prefix="sqluitest.")
    lib = [os.path.join(HERE, f) for f in LIB]
    for prog, srcs in (("sqlui", ["sqlui.c", "sqluiscr.c"]), ("sql", ["sql.c"])):
        r = subprocess.run(["cc", "-O", "-w", "-I", HERE, "-o", os.path.join(tmp, prog)] +
                           [os.path.join(HERE, f) for f in srcs] + lib)
        if r.returncode:
            print("sqlui_test: cannot build %s" % prog)
            return 2
    sqlcmd = ["CREATE TABLE dept (id INTEGER PRIMARY KEY, name TEXT NOT NULL);",
              "INSERT INTO dept VALUES (10, 'Sales'), (20, 'Lab'), (30, 'Office');",
              "CREATE TABLE emp (id INTEGER PRIMARY KEY, name VARCHAR(40) NOT NULL, "
              "dept INTEGER, salary INTEGER, note TEXT);",
              "CREATE INDEX emp_dept ON emp (dept);",
              "CREATE TABLE notes (a TEXT, b INTEGER);",
              "INSERT INTO notes VALUES ('twice', 1), ('twice', 1), ('once', 2), (NULL, 3);",
              "CREATE TABLE wide (c1 TEXT, c2 TEXT, c3 TEXT, c4 TEXT, c5 TEXT, c6 TEXT);",
              "INSERT INTO wide VALUES ('%s', 'b', 'c', 'd', 'e', 'f');" % ("x" * 50)]
    for i in range(1, 101):
        sqlcmd.append("INSERT INTO emp VALUES (%d, 'person %03d', %d, %d, %s);" %
                      (i, i, (i % 3 + 1) * 10, 1000 + (i * 37) % 500,
                       "NULL" if i % 4 == 0 else "'note %d'" % i))
    subprocess.run([os.path.join(tmp, "sql"), "-c", "db"], cwd=tmp,
                   input="\n".join(sqlcmd).encode(), check=True)

    def sql(q):
        return subprocess.run([os.path.join(tmp, "sql"), "db", q], cwd=tmp,
                              capture_output=True).stdout.decode()

    def modes(f):
        # stty -g; the host may set PENDIN (0x20000000) in lflag by itself
        # when a terminal goes back to canonical mode: it is not a mode we set
        s = open(os.path.join(tmp, f)).read().strip()
        return re.sub(r"lflag=([0-9a-f]+)", lambda m: "lflag=%x" % (int(m.group(1), 16) & ~0x20000000), s)

    def restored():
        return modes("before") == modes("after") and "lflag" in modes("after")

    # ---- the tables, browsing and paging
    ui = UI(tmp, "db")
    check("tables screen lists the tables", has(ui, "dept") and has(ui, "notes") and
          re.search(r"emp\s+100", ui.screen.text()) is not None, ui)
    check("status line names the database and screen", "db | Tables" in ui.screen.line(22), ui)
    ui.send("?")
    check("help on the tables screen", has(ui, "describe it: columns"), ui)
    ui.send("q")
    ui.send(["j", "d"])
    check("describe shows columns and indexes", has(ui, "PRIMARY KEY") and has(ui, "emp_dept")
          and has(ui, "VARCHAR(40)"), ui)
    ui.send("q")
    ui.send(ENTER)
    check("browse emp: row 1 of 100", has(ui, "Browse emp") and has(ui, "row 1 of 100"), ui)
    check("header and first row", "salary" in ui.screen.line(0) and "person 001" in ui.screen.line(1), ui)
    check("current row highlighted", ui.screen.isrev(1) and not ui.screen.isrev(2), ui)
    ui.send(["j", DOWN])
    check("down twice: row 3", has(ui, "row 3 of 100") and ui.screen.isrev(3), ui)
    ui.send(PGDN)
    check("PgDn: row 24, a new screenful", has(ui, "row 24 of 100") and "person 022" in ui.screen.line(1), ui)
    ui.send("\x06")
    check("^F pages too", has(ui, "row 45 of 100"), ui)
    ui.send("G")
    check("G: the last row", has(ui, "row 100 of 100") and "person 100" in ui.screen.line(21), ui)
    ui.send(PGUP)
    check("PgUp", has(ui, "row 79 of 100"), ui)
    ui.send("g")
    check("g: the first row", has(ui, "row 1 of 100") and "person 001" in ui.screen.line(1), ui)
    check("NULL shown", "NULL" in ui.screen.line(4), ui)

    # ---- editing a record
    ui.send(ENTER)
    check("form for row 1", has(ui, "Edit emp") and has(ui, "[person 001") and has(ui, "(INTEGER key)"), ui)
    ui.send([TAB, "\x15", "Zed O'Neil", TAB, TAB, "\x15", "4321", ENTER])
    check("edit saved", has(ui, "1 row changed") and "Zed O'Neil" in ui.screen.line(1), ui)
    check("edit in the database", "Zed O'Neil  20    4321" in sql("SELECT name, dept, salary FROM emp WHERE id = 1"))
    ui.send([ENTER, TAB, TAB, "\x0e", ENTER])
    check("^N makes NULL", "NULL" in sql("SELECT COALESCE(dept, 'NULL') FROM emp WHERE id = 1"))
    ui.send([ENTER, TAB, "\x0e", ENTER])
    check("NOT NULL error stays in the form", has(ui, "Edit emp") and "NULL" in ui.screen.line(22)
          and "|" in ui.screen.line(22), ui)
    ui.send(ESC)
    check("Esc leaves the form unsaved", has(ui, "Browse emp") and "Zed O'Neil" in ui.screen.line(1), ui)

    # ---- adding and deleting
    ui.send(["a", TAB, "New Person", TAB, "20", TAB, "999", ENTER])
    check("add a record", has(ui, "1 row added") and has(ui, "of 101"), ui)
    check("added in the database (id 101)", "New Person" in sql("SELECT name FROM emp WHERE id = 101"))
    ui.send(["G", "D"])
    check("delete asks first", has(ui, "Delete this row? (y/n)"), ui)
    ui.send("n")
    check("n keeps it", has(ui, "row 101 of 101"), ui)
    ui.send(["D", "y"])
    check("delete", has(ui, "1 row deleted") and has(ui, "row 100 of 100"), ui)
    check("deleted in the database", "100" in sql("SELECT COUNT(*) FROM emp"))

    # ---- filter and order
    ui.send(["/", "dept = 20", ENTER])
    n20 = sql("SELECT COUNT(*) FROM emp WHERE dept = 20").split()[-1]
    check("filter", has(ui, "row 1 of %s" % n20) and has(ui, "WHERE dept = 20"), ui)
    ui.send(["o", "salary DESC", ENTER])
    top = sql("SELECT name FROM emp WHERE dept = 20 ORDER BY salary DESC LIMIT 1").split("\n")[2]
    check("order by", top.strip() in ui.screen.line(1) and has(ui, "ORDER BY salary DESC"), ui)
    ui.send(["/", "\x15", "nosuch = 1", ENTER])
    check("a bad filter shows the error", "nosuch" in ui.screen.line(22) and has(ui, "WHERE nosuch"), ui)
    ui.send(ESC)
    check("Esc keeps the old filter", has(ui, "WHERE dept = 20"), ui)
    ui.send(["/", "\x15", ENTER, "o", "\x15", ENTER])
    check("empty filter: all rows", has(ui, "row 1 of 100"), ui)
    ui.send("?")
    check("help while browsing", has(ui, "Help: browsing"), ui)
    ui.send(["q", "q"])
    check("back to the tables", has(ui, "db | Tables"), ui)

    # ---- the query screen
    ui.send("q")
    check("query screen", has(ui, "SQL>") and has(ui, "Query"), ui)
    ui.send(["SELECT e.name, d.name FROM emp e JOIN dept d ON e.dept = d.id WHERE e.id < 5 ORDER BY e.id", ENTER])
    check("a join's results", has(ui, "Query results") and has(ui, "row 1 of 3") and
          "Lab" in ui.screen.text() and "Office" in ui.screen.text(), ui)
    ui.send("e")
    check("query results are read-only", has(ui, "read-only"), ui)
    ui.send("q")
    ui.send(["\x15", "SELECT * FROM nosuch", ENTER])
    check("an SQL error in the status line", "no such table" in ui.screen.line(22), ui)
    ui.send(["\x15", "UPDATE emp SET salary = salary + 1 WHERE dept = 10", ENTER])
    n10 = sql("SELECT COUNT(*) FROM emp WHERE dept = 10").split()[-1]
    check("UPDATE: rows changed", "%s rows changed" % n10 in ui.screen.line(22), ui)
    ui.send(["\x15", UP])
    check("history: Up brings back the last", "UPDATE emp SET" in ui.screen.line(23), ui)
    ui.send(UP)
    check("history: Up again", "SELECT * FROM nosuch" in ui.screen.line(23), ui)
    ui.send(["\x15", "SELECT COUNT(*) FROM emp", ENTER])
    check("count query", "100" in ui.screen.line(1), ui)
    ui.send(["q", ESC])

    # ---- a table without a primary key
    ui.send(["G"])
    ui.send(["k", ENTER])
    check("browse notes (no key)", has(ui, "Browse notes") and has(ui, "row 1 of 4"), ui)
    ui.send(["D"])
    check("identical rows: asks for both", has(ui, "Delete 2 identical rows?"), ui)
    ui.send("y")
    check("both deleted", has(ui, "2 rows deleted") and has(ui, "of 2"), ui)
    ui.send(["j", "j", ENTER, "\x15", "was null", ENTER])
    check("update by all values (NULL too)", "was null" in sql("SELECT a FROM notes WHERE b = 3"), ui)
    ui.send("q")

    # ---- a new table, drop
    ui.send(["n", "t2 (k INTEGER PRIMARY KEY, v TEXT)", ENTER])
    check("new table", has(ui, "table made") and re.search(r"t2\s+0", ui.screen.text()), ui)
    ui.send(["n", "bad", ENTER])
    check("bad CREATE shows the error", "|" in ui.screen.line(22) and "CREATE TABLE bad" in ui.screen.line(23), ui)
    ui.send([ESC, "G", "k"])
    ui.send(["x"])
    check("drop asks", has(ui, "Drop table t2"), ui)
    ui.send("y")
    check("dropped", has(ui, "table t2 dropped") and "t2" not in sql("SHOW TABLES"), ui)
    ui.send("Q")
    st = ui.wait()
    check("Q: exit status 0", os.WEXITSTATUS(st) == 0 and open(os.path.join(tmp, "status")).read().strip() == "0")
    check("Q: terminal modes restored", restored())

    # ---- sideways scrolling on a narrow screen
    ui = UI(tmp, "db", rows=12, cols=40)
    ui.send(["G", ENTER])
    check("narrow: browse wide", has(ui, "Browse wide") and "x" * 28 + ">" in ui.screen.line(1), ui)
    check("narrow: some columns", "cols 1" in ui.screen.text() and "c6" not in ui.screen.line(0), ui)
    ui.send([RIGHT, RIGHT])
    check("narrow: scrolled sideways", "c3" in ui.screen.line(0) and "c1" not in ui.screen.line(0), ui)
    ui.send(ENTER)
    check("narrow: form shows a long value cut", has(ui, "Edit wide"), ui)
    ui.send([ESC, "q", "Q"])
    ui.wait()
    check("narrow: terminal restored", restored())

    # ---- ^C anywhere leaves the terminal as it was
    ui = UI(tmp, "db")
    ui.send(["j", ENTER, ENTER, TAB, "abc"])
    ui.send("\x03")
    ui.wait()
    check("^C: exit status not 0", open(os.path.join(tmp, "status")).read().strip() != "0")
    check("^C: terminal modes restored", restored())
    check("^C: nothing saved", "Zed O'Neil" in sql("SELECT name FROM emp WHERE id = 1"))

    # ---- -c and a missing database
    ui = UI(tmp, "nodb")
    ui.wait()
    check("missing database: status 2", open(os.path.join(tmp, "status")).read().strip() == "2")
    ui = UI(tmp, "-c newdb")
    check("-c makes it", has(ui, "no tables") and has(ui, "newdb | Tables"), ui)
    ui.send("Q")
    ui.wait()

    subprocess.run(["rm", "-rf", tmp])
    if failures:
        print("sqlui: %d checks FAILED" % failures)
        return 1
    print("sqlui: all tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
