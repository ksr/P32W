/* sqlstore.c -- the SQL library's storage (sqldb(3)): memory and errors, the
 * pager (page cache, journal, lock), and table files (indexes are in
 * sqlbtree.c).
 *
 * Writing. Only one process writes at a time: it holds the lock, a file
 * "lock" in the database directory made with O_CREAT|O_EXCL (holding the
 * writer's pid, so a lock left by a dead process can be taken over). Before
 * a page that existed when the transaction began is changed for the first
 * time, its old contents go to the file "journal". Changed pages stay in
 * the cache (or are written when the cache needs room); at commit they are
 * all written and the journal is removed: that unlink is the moment the
 * transaction happens. A rollback -- or the next process to open the
 * database after a crash -- writes the journal's pages back. New files are
 * noted in the journal (rollback removes them); dropped files are renamed
 * to ~N.del and removed after the commit. Files only grow: the header's
 * page count says which pages are in use, so a rolled-back append leaves
 * just unused pages at the end.
 *
 * Readers take no lock: a query run while another process writes may see
 * part of that write (a statement, or a transaction), never a broken page.
 */
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <dirent.h>
#include <time.h>
#include <sys/stat.h>
#include "sqlint.h"

sqldb *D;
struct sqlstmt *S;
struct parser *P;

/* ---------------------------------------------------------------- errors, memory */

/* report an error: back to the API function that started the work */
void fail(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(D->err, sizeof D->err, fmt, ap);
    va_end(ap);
    longjmp(*D->jb, 1);
}

void *aalloc(arena *a, int n)
{
    struct chunk *c = a->head;
    char *p;
    n = (n + 7) & ~7;
    if (!c || c->used + n > c->size) {
        int size = n > 8000 ? n : 8000;
        if (!(c = malloc(sizeof(struct chunk) + size))) fail("out of memory");
        c->size = size;
        c->used = 0;
        c->next = a->head;
        a->head = c;
    }
    p = (char *)(c + 1) + c->used;
    c->used += n;
    return p;
}

void *zalloc(int n)
{
    void *p = aalloc(&S->a, n);
    memset(p, 0, n);
    return p;
}

void amark(arena *a, struct mark *m)
{
    m->c = a->head;
    m->used = a->head ? a->head->used : 0;
}

/* free what was allocated since the mark */
void arelease(arena *a, struct mark *m)
{
    struct chunk *c;
    while (a->head && a->head != m->c) {
        c = a->head;
        a->head = c->next;
        free(c);
    }
    if (a->head) a->head->used = m->used;
}

void afree(arena *a)
{
    struct mark m;
    m.c = 0;
    m.used = 0;
    arelease(a, &m);
}

char *astrndup(arena *a, const char *s, int n)
{
    char *p = aalloc(a, n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

void vpush(struct vec *v, void *x)
{
    void **nv;
    if (v->n == v->cap) {
        v->cap = v->cap ? 2 * v->cap : 8;
        nv = aalloc(&S->a, v->cap * sizeof *nv);
        if (v->n) memcpy(nv, v->v, v->n * sizeof *nv);
        v->v = nv;
    }
    v->v[v->n++] = x;
}

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

int ieq(const char *a, const char *b)       /* equal, ignoring case */
{
    while (*a && lower(*a) == lower(*b)) a++, b++;
    return lower(*a) == lower(*b);
}

int get16(const unsigned char *p) { return p[0] | p[1] << 8; }
void put16(unsigned char *p, int v) { p[0] = v; p[1] = v >> 8; }

int get32(const unsigned char *p)
{
    return (int)((unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24);
}

void put32(unsigned char *p, int v)
{
    p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

int getbe(const unsigned char *p)
{
    return (int)((unsigned)p[0] << 24 | (unsigned)p[1] << 16 | (unsigned)p[2] << 8 | p[3]);
}

/* ---------------------------------------------------------------- the pager */

void sq_dpath(char *buf, const char *name)
{
    snprintf(buf, 256, "%s/%s", D->dir, name);
}

static struct pfile *pf_add(const char *name, int fd, int ro)
{
    struct pfile *f = malloc(sizeof *f);
    if (!f) { close(fd); fail("out of memory"); }
    memset(f, 0, sizeof *f);
    strcpy(f->name, name);
    f->fd = fd;
    f->rdonly = ro;
    f->orig = -1;
    f->next = D->files;
    D->files = f;
    return f;
}

/* a file of the database (0 if it does not exist or is empty) */
struct pfile *sq_pfopen(const char *name)
{
    struct pfile *f;
    struct stat st;
    char path[256];
    int fd, ro = 0;
    for (f = D->files; f; f = f->next)
        if (!strcmp(f->name, name)) return f;
    sq_dpath(path, name);
    if ((fd = open(path, O_RDWR)) < 0 && errno == EACCES) {
        fd = open(path, O_RDONLY);
        ro = 1;
    }
    if (fd < 0) {
        if (errno == ENOENT) return 0;
        fail("%s: %s", name, strerror(errno));
    }
    if (fstat(fd, &st) < 0 || st.st_size < PGSZ) {     /* (made by a writer that crashed) */
        close(fd);
        return 0;
    }
    return pf_add(name, fd, ro);
}

/* forget a file: its pages in the cache, its descriptor */
void sq_pfclose(struct pfile *f)
{
    struct pfile **pp;
    int i;
    for (i = 0; i < NFRAME; i++)
        if (D->fr[i].f == f) D->fr[i].f = 0;
    for (pp = &D->files; *pp; pp = &(*pp)->next)
        if (*pp == f) { *pp = f->next; break; }
    close(f->fd);
    free(f->jbits);
    free(f);
}

static void pg_write(struct frame *fr)
{
    if (lseek(fr->f->fd, (off_t)fr->pg * PGSZ, SEEK_SET) < 0 ||
        write(fr->f->fd, fr->data, PGSZ) != PGSZ)
        fail("%s: cannot write: %s", fr->f->name, strerror(errno));
    fr->dirty = 0;
}

/* the cache's frame for a page: read it in (load), or zero it */
static struct frame *frame(struct pfile *f, int pg, int load)
{
    struct frame *fr, *v = 0;
    int i;
    if (pg < 0 || pg >= 1 << 20) fail("%s: bad page number %d (damaged file?)", f->name, pg);
    for (i = 0; i < NFRAME; i++) {
        fr = &D->fr[i];
        if (fr->f == f && fr->pg == pg) {
            fr->stamp = ++D->clock;
            return fr;
        }
        if (!v || (v->f && (!fr->f || fr->stamp < v->stamp))) v = fr;   /* empty, or least recently used */
    }
    if (v->f && v->dirty) pg_write(v);
    v->f = 0;
    if (load) {
        if (lseek(f->fd, (off_t)pg * PGSZ, SEEK_SET) < 0 || read(f->fd, v->data, PGSZ) != PGSZ)
            fail("%s: page %d cannot be read (damaged file?)", f->name, pg);
    } else
        memset(v->data, 0, PGSZ);
    v->f = f;
    v->pg = pg;
    v->dirty = 0;
    v->stamp = ++D->clock;
    return v;
}

unsigned char *sq_pg(struct pfile *f, int pg)
{
    return frame(f, pg, 1)->data;
}

int sq_npages(struct pfile *f)
{
    return get32(sq_pg(f, 0) + H_NPAGES);
}

static void jwrite(const void *p, int n)
{
    if (write(D->jfd, p, n) != n) fail("cannot write the journal: %s", strerror(errno));
}

/* a journal record: 'P' name page data (a page's old contents), 'C' name (a
   file made), 'D' name newname (a file renamed to be removed at commit) */
static void jrec(int type, const char *name, const char *name2, int pg, const unsigned char *data)
{
    unsigned char h[80];
    char path[256];
    int n = 0, l;
    if (D->jfd < 0) {
        sq_dpath(path, "journal");
        if ((D->jfd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0)
            fail("cannot make the journal: %s", strerror(errno));
        jwrite("P32J", 4);
    }
    h[n++] = type;
    h[n++] = l = strlen(name);
    memcpy(h + n, name, l);
    n += l;
    if (name2) {
        h[n++] = l = strlen(name2);
        memcpy(h + n, name2, l);
        n += l;
    }
    if (data) {
        put32(h + n, pg);
        n += 4;
    }
    jwrite(h, n);
    if (data) jwrite(data, PGSZ);
    D->stmtmod = 1;
}

/* a page that is about to be changed (call it before changing the page) */
unsigned char *sq_touch(struct pfile *f, int pg)
{
    struct frame *fr;
    if (!D->locked) fail("internal error: a write without the lock");
    if (f->rdonly) fail("%s: the database is read-only", f->name);
    if (f->orig < 0) {                  /* first write in this transaction */
        f->orig = sq_npages(f);
        if (f->orig < 1 || f->orig > 1 << 20) fail("%s: damaged header", f->name);
        if (!(f->jbits = calloc((f->orig + 7) / 8, 1))) fail("out of memory");
    }
    fr = frame(f, pg, 1);
    if (pg < f->orig && !(f->jbits[pg >> 3] & 1 << (pg & 7))) {
        jrec('P', f->name, 0, pg, fr->data);
        f->jbits[pg >> 3] |= 1 << (pg & 7);
    }
    fr->dirty = 1;
    D->stmtmod = 1;
    return fr->data;
}

/* a new page, zeroed, at the end of the file: its number */
int sq_newpage(struct pfile *f)
{
    unsigned char *h = sq_touch(f, 0);
    int pg = get32(h + H_NPAGES);
    struct frame *fr;
    put32(h + H_NPAGES, pg + 1);
    fr = frame(f, pg, 0);
    memset(fr->data, 0, PGSZ);
    fr->dirty = 1;
    return pg;
}

/* make a new file with a header page */
struct pfile *sq_create(const char *name, const char *magic)
{
    char path[256];
    struct pfile *f;
    struct frame *fr;
    struct stat st;
    int fd;
    sq_dpath(path, name);
    if (stat(path, &st) == 0 && st.st_size < PGSZ) unlink(path);   /* left by a crash */
    if ((fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0644)) < 0)
        fail("cannot make %s: %s", name, strerror(errno));
    f = pf_add(name, fd, 0);
    jrec('C', name, 0, 0, 0);
    f->orig = 0;                        /* nothing in it to save */
    fr = frame(f, 0, 0);
    memcpy(fr->data, magic, 4);
    put32(fr->data + H_NPAGES, 1);
    fr->dirty = 1;
    D->ddl = 1;
    return f;
}

/* drop a file: renamed now, removed at commit */
void sq_dropfile(const char *name)
{
    char from[256], to[256];
    struct pfile *f;
    struct dname *d;
    for (f = D->files; f; f = f->next)
        if (!strcmp(f->name, name)) { sq_pfclose(f); break; }
    if (!(d = malloc(sizeof *d))) fail("out of memory");
    sprintf(d->name, "~%d.del", ++D->delseq);
    d->next = D->dels;
    D->dels = d;
    jrec('D', name, d->name, 0, 0);
    sq_dpath(from, name);
    sq_dpath(to, d->name);
    if (rename(from, to) < 0) fail("cannot remove %s: %s", name, strerror(errno));
    D->ddl = 1;
}

static int rd(int fd, int off, void *p, int n)
{
    return lseek(fd, (off_t)off, SEEK_SET) >= 0 && read(fd, p, n) == n;
}

/* put back what the journal saved, newest first; 1 if done (or no
   journal). It never fails(): it runs while an error is handled. */
static int replay(void)
{
    static unsigned char page[PGSZ];
    char path[256], name[40], name2[40];
    unsigned char h[4];
    struct stat st;
    int fd, tfd, *offs = 0, n = 0, cap = 0, off = 4, l, l2, pg, i, ok = 1;
    sq_dpath(path, "journal");
    if ((fd = open(path, O_RDONLY)) < 0) return 1;
    if (fstat(fd, &st) < 0 || !rd(fd, 0, h, 4) || memcmp(h, "P32J", 4)) goto done;
    for (;;) {                          /* find the whole records (a torn last one is left) */
        if (!rd(fd, off, h, 2) || (h[0] != 'P' && h[0] != 'C' && h[0] != 'D') || h[1] > 32) break;
        l = 2 + h[1];
        if (h[0] == 'D') {
            if (!rd(fd, off + l, h + 2, 1) || h[2] > 32) break;
            l += 1 + h[2];
        }
        if (h[0] == 'P') l += 4 + PGSZ;
        if (off + l > st.st_size) break;
        if (n == cap) {
            int *no = realloc(offs, (cap = cap ? 2 * cap : 64) * sizeof *offs);
            if (!no) { ok = 0; goto done; }
            offs = no;
        }
        offs[n++] = off;
        off += l;
    }
    for (i = n - 1; i >= 0; i--) {
        off = offs[i];
        rd(fd, off, h, 2);
        l = h[1];
        memset(name, 0, sizeof name);
        rd(fd, off + 2, name, l);
        sq_dpath(path, name);
        if (h[0] == 'P') {
            rd(fd, off + 2 + l, h, 4);
            pg = get32(h);
            if (!rd(fd, off + 6 + l, page, PGSZ) || (tfd = open(path, O_WRONLY)) < 0) { ok = 0; continue; }
            if (lseek(tfd, (off_t)pg * PGSZ, SEEK_SET) < 0 || write(tfd, page, PGSZ) != PGSZ) ok = 0;
            close(tfd);
        } else if (h[0] == 'C')
            unlink(path);
        else {
            char to[256];
            rd(fd, off + 2 + l, h, 1);
            l2 = h[0];
            memset(name2, 0, sizeof name2);
            rd(fd, off + 3 + l, name2, l2);
            sq_dpath(to, name2);
            rename(to, path);
        }
    }
done:
    free(offs);
    close(fd);
    if (ok) {
        sq_dpath(path, "journal");
        unlink(path);
    }
    return ok;
}

/* take the lock if it is free: 1; 0 if a live process holds it */
static int lock_once(int *owner)
{
    char path[256], b[16];
    struct stat st;
    int fd, n, pid;
    sq_dpath(path, "lock");
    *owner = 0;
    if ((fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644)) >= 0) {
        n = sprintf(b, "%d\n", getpid());
        write(fd, b, n);
        close(fd);
        return 1;
    }
    if (errno != EEXIST) fail("cannot lock the database: %s", strerror(errno));
    if ((fd = open(path, O_RDONLY)) < 0) return 0;     /* (just went: try again) */
    n = read(fd, b, sizeof b - 1);
    close(fd);
    b[n > 0 ? n : 0] = 0;
    *owner = pid = atoi(b);
    if ((pid > 0 ? kill(pid, 0) < 0 && errno == ESRCH
                 : stat(path, &st) == 0 && st.st_mtime < time(0) - 5) && unlink(path) == 0)
        return lock_once(owner);        /* it was left by a dead process */
    if (pid == getpid()) fail("the database is locked by another sql_open in this process");
    return 0;
}

/* the write lock, waiting for another writer if need be */
void sq_lock(void)
{
    int owner, tries = 0, i;
    if (D->locked) return;
    while (!lock_once(&owner)) {
        if (++tries > LOCKWAIT) fail("the database is locked by process %d", owner);
        sleep(1);
    }
    D->locked = 1;
    for (i = 0; i < NFRAME; i++)        /* another process may have changed any page */
        D->fr[i].f = 0;
    if (!replay()) fail("cannot roll back the journal left by a crash");
}

void sq_unlock(void)
{
    char path[256];
    if (!D->locked) return;
    sq_dpath(path, "lock");
    unlink(path);
    D->locked = 0;
}

/* finish what a crashed writer left: its journal, and (dels) the files it
   dropped. Nothing is done while another process holds the lock. */
void sq_recover(int dels)
{
    char path[256];
    struct dirent *de;
    DIR *dir;
    int owner, n, found = 0;
    sq_dpath(path, "journal");
    if (D->locked) return;
    if (access(path, F_OK) == 0) found = 1;
    if (dels && (dir = opendir(D->dir))) {
        while ((de = readdir(dir)))
            if ((n = strlen(de->d_name)) > 4 && !strcmp(de->d_name + n - 4, ".del")) found = 1;
        closedir(dir);
    }
    if (!found || !lock_once(&owner)) return;
    D->locked = 1;
    replay();
    if (dels && (dir = opendir(D->dir))) {
        while ((de = readdir(dir)))
            if ((n = strlen(de->d_name)) > 4 && !strcmp(de->d_name + n - 4, ".del")) {
                sq_dpath(path, de->d_name);
                unlink(path);
            }
        closedir(dir);
    }
    sq_unlock();
}

static void txn_clear(void)
{
    struct pfile *f;
    struct dname *d;
    for (f = D->files; f; f = f->next) {
        f->orig = -1;
        free(f->jbits);
        f->jbits = 0;
    }
    while ((d = D->dels)) {
        D->dels = d->next;
        free(d);
    }
    D->ddl = 0;
}

void sq_commit(void)
{
    char path[256];
    struct dname *d;
    int i;
    for (i = 0; i < NFRAME; i++)
        if (D->fr[i].f && D->fr[i].dirty) pg_write(&D->fr[i]);
    if (D->jfd >= 0) {
        close(D->jfd);
        D->jfd = -1;
        sq_dpath(path, "journal");
        if (unlink(path) < 0) fail("cannot remove the journal: %s", strerror(errno));
    }
    for (d = D->dels; d; d = d->next) {
        sq_dpath(path, d->name);
        unlink(path);
    }
    txn_clear();
}

/* undo the transaction (never fails) */
void sq_rollback(void)
{
    int i, ddl = D->ddl;
    for (i = 0; i < NFRAME; i++) {
        D->fr[i].f = 0;
        D->fr[i].dirty = 0;
    }
    if (D->jfd >= 0) {
        close(D->jfd);
        D->jfd = -1;
    }
    if (!replay() && !D->err[0])
        strcpy(D->err, "cannot roll back the journal");
    txn_clear();
    if (ddl) {                          /* the tables loaded may be wrong now */
        sq_reset();
        D->gen++;
    }
}

/* forget the tables and files: between statements, when nothing is open */
void sq_reset(void)
{
    struct table *t;
    int i;
    for (i = 0; i < NFRAME; i++)
        D->fr[i].f = 0;
    while ((t = D->tables)) {
        D->tables = t->next;
        free(t);
    }
    while (D->files) sq_pfclose(D->files);
}

/* ---------------------------------------------------------------- tables */

int sq_th(struct table *t, int off) { return get32(sq_pg(t->pf, 0) + off); }
void sq_thset(struct table *t, int off, int v) { put32(sq_touch(t->pf, 0) + off, v); }

/* a table, loaded from its header (0 or an error if there is none) */
struct table *sq_table(const char *name, int must)
{
    char fn[40];
    struct table *t;
    struct pfile *f;
    unsigned char *h, *p;
    int i;
    for (t = D->tables; t; t = t->next)
        if (!strcmp(t->name, name)) return t;
    if (strlen(name) > MAXNAME) goto none;
    sprintf(fn, "%s.tbl", name);
    if (!(f = sq_pfopen(fn))) goto none;
    h = sq_pg(f, 0);
    if (memcmp(h, "P32T", 4) || get32(h + T_NCOLS) < 1 || get32(h + T_NCOLS) > MAXCOL ||
        get32(h + T_NIDX) < 0 || get32(h + T_NIDX) > MAXIDX)
        fail("%s: not a table (damaged?)", fn);
    if (!(t = calloc(1, sizeof *t))) fail("out of memory");
    strcpy(t->name, name);
    t->pf = f;
    t->ncols = get32(h + T_NCOLS);
    t->nidx = get32(h + T_NIDX);
    for (i = 0; i < t->ncols; i++) {
        p = h + T_COLS + i * CE;
        memcpy(t->col[i].name, p, MAXNAME);
        t->col[i].type = p[21];
        t->col[i].flags = p[22];
        t->col[i].maxlen = get16(p + 24);
    }
    for (i = 0; i < t->nidx; i++) {
        p = h + T_IDX + i * IE;
        memcpy(t->idx[i].name, p, MAXNAME + 4);
        t->idx[i].col = p[25];
        t->idx[i].unique = p[26];
        if (t->idx[i].col >= t->ncols) fail("%s: damaged header", fn);
        t->idx[i].ks = t->col[t->idx[i].col].type == SQL_INTEGER ? 4 : TKEY;
    }
    t->next = D->tables;
    D->tables = t;
    return t;
none:
    if (must) fail("no such table: %s", name);
    return 0;
}

/* write a table's columns and indexes into its header */
void sq_savetable(struct table *t)
{
    unsigned char *h = sq_touch(t->pf, 0), *p;
    int i;
    put32(h + T_NCOLS, t->ncols);
    put32(h + T_NIDX, t->nidx);
    memset(h + T_NAME, 0, 24);
    strcpy((char *)h + T_NAME, t->name);
    memset(h + T_COLS, 0, MAXCOL * CE + MAXIDX * IE);
    for (i = 0; i < t->ncols; i++) {
        p = h + T_COLS + i * CE;
        strcpy((char *)p, t->col[i].name);
        p[21] = t->col[i].type;
        p[22] = t->col[i].flags;
        put16(p + 24, t->col[i].maxlen);
    }
    for (i = 0; i < t->nidx; i++) {
        p = h + T_IDX + i * IE;
        strcpy((char *)p, t->idx[i].name);
        p[25] = t->idx[i].col;
        p[26] = t->idx[i].unique;
    }
}

/* ---------------------------------------------------------------- rows */

/* a data page, checked */
unsigned char *sq_dpage(struct table *t, int pg)
{
    unsigned char *p = sq_pg(t->pf, pg);
    int n = get16(p);
    if (p[4] != 'D' || n > 255 || get16(p + D_TOP) < D_SLOTS + 4 * n || get16(p + D_TOP) > PGSZ)
        fail("%s.tbl: page %d is damaged", t->name, pg);
    return p;
}

/* is there room for a len-byte row? 0 no, 1 yes, 2 after compacting;
   *slot: the slot it would take. (Every slot is checked first: compacting
   a damaged page would spread the damage.) */
static int pg_room(struct table *t, unsigned char *p, int len, int *slot)
{
    int n = get16(p), s, i, l, used = 0, need;
    for (i = 0; i < n; i++)
        if ((l = get16(p + D_SLOTS + 4 * i + 2))) {
            used += l;
            if (get16(p + D_SLOTS + 4 * i) < D_SLOTS + 4 * n || get16(p + D_SLOTS + 4 * i) + l > PGSZ)
                fail("%s.tbl: a damaged page", t->name);
        }
    if (used > PGSZ) fail("%s.tbl: a damaged page", t->name);
    for (s = 0; s < n && get16(p + D_SLOTS + 4 * s + 2); s++) ;
    if (s == n && n >= 255) return 0;
    need = len + (s == n ? 4 : 0);
    *slot = s;
    if (get16(p + D_TOP) - (D_SLOTS + 4 * n) >= need) return 1;
    return PGSZ - D_SLOTS - 4 * n - used >= need ? 2 : 0;
}

/* put a row in slot s, which pg_room said fits (how) */
static void pg_place(unsigned char *p, int s, unsigned char *rec, int len, int how)
{
    unsigned char tmp[PGSZ];
    int n = get16(p), i, top = PGSZ, l;
    if (how == 2) {                     /* move the rows together at the end */
        memcpy(tmp, p, PGSZ);
        for (i = 0; i < n; i++)
            if ((l = get16(p + D_SLOTS + 4 * i + 2))) {
                top -= l;
                memcpy(p + top, tmp + get16(tmp + D_SLOTS + 4 * i), l);
                put16(p + D_SLOTS + 4 * i, top);
            }
        put16(p + D_TOP, top);
    }
    if (s == n) put16(p, n + 1);
    top = get16(p + D_TOP) - len;
    memcpy(p + top, rec, len);
    put16(p + D_TOP, top);
    put16(p + D_SLOTS + 4 * s, top);
    put16(p + D_SLOTS + 4 * s + 2, len);
}

/* add a row: its rowid (page * 256 + slot) */
int sq_insrow(struct table *t, unsigned char *rec, int len)
{
    int np = sq_npages(t->pf), cand[2], i, pg, s, how;
    unsigned char *p;
    cand[0] = sq_th(t, T_HINT);
    cand[1] = np - 1;
    for (i = 0; i < 2; i++) {
        pg = cand[i];
        if (pg < 1 || pg >= np) continue;
        if ((how = pg_room(t, sq_dpage(t, pg), len, &s))) {
            pg_place(sq_touch(t->pf, pg), s, rec, len, how);
            goto done;
        }
    }
    pg = sq_newpage(t->pf);
    p = sq_touch(t->pf, pg);
    put16(p + D_TOP, PGSZ);
    p[4] = 'D';
    pg_place(p, s = 0, rec, len, 1);
    sq_thset(t, T_HINT, pg);
done:
    sq_thset(t, T_NROWS, sq_th(t, T_NROWS) + 1);
    return pg << 8 | s;
}

/* copy a row into buf: its length, 0 if there is no such row */
int sq_fetch(struct table *t, int rid, unsigned char *buf)
{
    int pg = rid >> 8, s = rid & 255, off, len;
    unsigned char *p;
    if (pg < 1 || pg >= sq_npages(t->pf)) return 0;
    p = sq_dpage(t, pg);
    if (s >= get16(p) || !(len = get16(p + D_SLOTS + 4 * s + 2))) return 0;
    off = get16(p + D_SLOTS + 4 * s);
    if (off < D_SLOTS + 4 * get16(p) || off + len > PGSZ) fail("%s.tbl: page %d is damaged", t->name, pg);
    memcpy(buf, p + off, len);
    return len;
}

void sq_delrow(struct table *t, int rid)
{
    int pg = rid >> 8, s = rid & 255, n, i, used = 0;
    unsigned char *p = sq_touch(t->pf, pg);
    put16(p + D_SLOTS + 4 * s + 2, 0);
    for (n = get16(p); n > 0 && !get16(p + D_SLOTS + 4 * (n - 1) + 2); n--) ;
    put16(p, n);                        /* free slots at the end go */
    for (i = 0; i < n; i++) used += get16(p + D_SLOTS + 4 * i + 2);
    if (used < PGSZ * 3 / 4) sq_thset(t, T_HINT, pg);   /* room here for the next insert */
    sq_thset(t, T_NROWS, sq_th(t, T_NROWS) - 1);
}

/* replace a row: its rowid, a new one if it had to move */
int sq_updrow(struct table *t, int rid, unsigned char *rec, int len)
{
    int pg = rid >> 8, s = rid & 255, how;
    unsigned char *p = sq_touch(t->pf, pg);
    if (get16(p + D_SLOTS + 4 * s) + get16(p + D_SLOTS + 4 * s + 2) > PGSZ) fail("%s.tbl: a damaged page", t->name);
    if (len <= get16(p + D_SLOTS + 4 * s + 2)) {    /* fits where it was */
        memcpy(p + get16(p + D_SLOTS + 4 * s), rec, len);
        put16(p + D_SLOTS + 4 * s + 2, len);
        return rid;
    }
    put16(p + D_SLOTS + 4 * s + 2, 0);  /* free it, then look for room in its page */
    if ((how = pg_room(t, p, len, &s))) {
        pg_place(p, s, rec, len, how);
        return pg << 8 | s;
    }
    sq_thset(t, T_NROWS, sq_th(t, T_NROWS) - 1);
    return sq_insrow(t, rec, len);
}

/* a row's record: for each column a tag, 0 NULL, 1 INTEGER (4 bytes),
   2 TEXT (length(2), the bytes, a NUL) */
int sq_mkrec(struct table *t, val *v, unsigned char *buf)
{
    unsigned char *p = buf;
    int i, l;
    for (i = 0; i < t->ncols; i++) {
        if (v[i].t == SQL_NULL)
            *p++ = 0;
        else if (v[i].t == SQL_INTEGER) {
            *p++ = 1;
            put32(p, v[i].i);
            p += 4;
        } else {
            l = strlen(v[i].s);
            *p++ = 2;
            put16(p, l);
            memcpy(p + 2, v[i].s, l + 1);
            p += l + 3;
        }
        if (p - buf > MAXREC) fail("row too long for %s (the most is %d bytes)", t->name, MAXREC);
    }
    return p - buf;
}

/* the values of a record (TEXT points into rec) */
void sq_readrec(struct table *t, unsigned char *rec, int len, val *v)
{
    unsigned char *p = rec, *e = rec + len;
    int i, l;
    for (i = 0; i < t->ncols; i++) {
        if (p >= e) goto bad;
        switch (*p++) {
        case 0:
            v[i].t = SQL_NULL;
            break;
        case 1:
            if (p + 4 > e) goto bad;
            v[i].t = SQL_INTEGER;
            v[i].i = get32(p);
            p += 4;
            break;
        case 2:
            if (p + 2 > e) goto bad;
            l = get16(p);
            if (p + 3 + l > e || p[2 + l]) goto bad;
            v[i].t = SQL_TEXT;
            v[i].s = (char *)p + 2;
            p += 3 + l;
            break;
        default:
            goto bad;
        }
    }
    return;
bad:
    fail("%s.tbl: a damaged row", t->name);
}
