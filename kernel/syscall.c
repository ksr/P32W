/* syscall.c -- the system calls (M3, D22; os-design.md 4.4).
 * SYSCALL: the number in r1, arguments in r2-r5; the result in r1, a
 * failure as -errno; every other register is preserved (the trap frame). */
#include "defs.h"
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/usb.h>
#include <fcntl.h>

#define tf (curproc->tf)                         /* this process's trap frame (system calls sleep) */
#define PATHMAX 256
uint arg(int n) { return tf->r[n + 2]; }        /* arguments 0..3: r2..r5 */

int fdget(int fd, struct file **f)
{
    if (fd < 0 || fd >= NOFILE || !(*f = curproc->ofile[fd])) return -EBADF;
    return 0;
}

int fdalloc(struct file *f)
{
    int fd;
    for (fd = 0; fd < NOFILE; fd++)
        if (!curproc->ofile[fd]) { curproc->ofile[fd] = f; return fd; }
    return -EMFILE;
}

static int getpath(char *buf, uint user)
{
    int r = copyinstr(buf, user, PATHMAX);
    return r < 0 ? r : 0;
}

/* ---------------------------------------------------------------- files */
static int created_new;                 /* create() made a new inode (open: no permission check then) */

static struct inode *create(const char *path, ushort mode, int *err)
{
    char name[DIRSIZ + 1];
    struct inode *dp, *ip;
    created_new = 0;
    if (!(dp = nameiparent(path, name, err))) return NULL;
    ilock(dp);
    if ((ip = dirlookup(dp, name, 0)) != NULL) {
        iunlockput(dp);
        ilock(ip);
        /* creat / open(O_CREAT) on what exists: a file or a device opens (so `> /dev/null`
           works); only a directory refuses (mknod and mkdir still refuse anything) */
        if ((mode & IFMT) == IFREG && (ip->d.mode & IFMT) != IFDIR) return ip;
        iunlockput(ip);
        *err = -EEXIST;
        return NULL;
    }
    if (!access_ok(dp, 2)) { iunlockput(dp); *err = -EACCES; return NULL; }
    if (!(ip = ialloc(dp->dev, mode))) { iunlockput(dp); *err = -ENOSPC; return NULL; }
    ilock(ip);
    ip->d.uid = curproc->euid;
    ip->d.gid = curproc->egid;
    ip->d.nlink = 1;
    iupdate(ip);
    if ((mode & IFMT) == IFDIR) {
        dp->d.nlink++;                  /* the new directory's .. */
        iupdate(dp);
        if (dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0) panic("create dots");
        ip->d.nlink = 2;
        iupdate(ip);
    }
    if ((*err = dirlink(dp, name, ip->inum)) < 0) {
        if ((mode & IFMT) == IFDIR) { dp->d.nlink--; iupdate(dp); }    /* (its .. never came to be) */
        ip->d.nlink = 0;
        iupdate(ip);
        iunlockput(ip);
        iunlockput(dp);
        return NULL;
    }
    iunlockput(dp);
    created_new = 1;
    return ip;
}

/* open(path, flags, mode): the file table slot and the descriptor are taken
   first, so a call that fails for want of them creates nothing */
static int sys_open(void)
{
    char path[PATHMAX];
    int flags = arg(1), err, fd, acc = flags & O_ACCMODE;
    struct inode *ip;
    struct file *f;
    if ((err = getpath(path, arg(0))) < 0) return err;
    if (!(f = filealloc())) return -ENFILE;
    if ((fd = fdalloc(f)) < 0) { fileclose(f); return fd; }
    err = 0;
    if ((flags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL) && (ip = namei(path, &err)) != NULL) {
        iput(ip);                       /* O_EXCL: it must not exist yet */
        err = -EEXIST;
        goto bad;
    }
    if (flags & O_CREAT) {
        if (!(ip = create(path, IFREG | (arg(2) & 07777 & ~curproc->umask), &err))) goto bad;
    } else {
        if (!(ip = namei(path, &err))) goto bad;
        ilock(ip);
    }
    if (!((flags & O_CREAT) && created_new) &&       /* (a file just made is its maker's to open) */
        ((acc != O_WRONLY && !access_ok(ip, 4)) || (acc != O_RDONLY && !access_ok(ip, 2)))) {
        iunlockput(ip);
        err = -EACCES;
        goto bad;
    }
    if ((ip->d.mode & IFMT) == IFDIR && acc != O_RDONLY) { iunlockput(ip); err = -EISDIR; goto bad; }
    if ((flags & O_TRUNC) && (ip->d.mode & IFMT) == IFREG) itrunc(ip);
    if ((ip->d.mode & IFMT) == IFCHR || (ip->d.mode & IFMT) == IFBLK) {
        if ((err = devopen(ip, flags)) < 0) { iunlockput(ip); goto bad; }
    }
    iunlock(ip);
    f->type = FD_INODE;
    f->ip = ip;
    f->flags = flags & (O_ACCMODE | O_APPEND);
    return fd;
bad:
    curproc->ofile[fd] = 0;
    fileclose(f);
    return err;
}

static int sys_creat(void)
{
    tf->r[3] = O_WRONLY | O_CREAT | O_TRUNC;
    tf->r[4] = arg(1);
    return sys_open();
}

static int sys_close(void)
{
    struct file *f;
    int fd = arg(0), r;
    if ((r = fdget(fd, &f)) < 0) return r;
    curproc->ofile[fd] = NULL;
    fileclose(f);
    return 0;
}

static int sys_read(void)
{
    struct file *f;
    int r = fdget(arg(0), &f);
    if (r < 0) return r;
    if ((int)arg(2) < 0) return -EINVAL;
    return fileread(f, arg(1), arg(2));
}

static int sys_write(void)
{
    struct file *f;
    int r = fdget(arg(0), &f);
    if (r < 0) return r;
    if ((int)arg(2) < 0) return -EINVAL;
    return filewrite(f, arg(1), arg(2));
}

static int sys_lseek(void)
{
    struct file *f;
    int r = fdget(arg(0), &f), off = arg(1), whence = arg(2);
    if (r < 0) return r;
    if (f->type != FD_INODE) return -ESPIPE;
    if ((f->ip->d.mode & IFMT) == IFCHR && major(f->ip->d.addr[0]) != DEV_RDISK && major(f->ip->d.addr[0]) != DEV_RUSB) return -ESPIPE;
    if (whence == 1) off += f->off;
    else if (whence == 2) off += f->ip->d.size;
    else if (whence != 0) return -EINVAL;
    if (off < 0) return -EINVAL;
    f->off = off;
    return off;
}

static int sys_dup(void)
{
    struct file *f;
    int r = fdget(arg(0), &f);
    if (r < 0) return r;
    if ((r = fdalloc(f)) < 0) return r;
    filedup(f);
    return r;
}

static int sys_dup2(void)
{
    struct file *f;
    int r = fdget(arg(0), &f), fd2 = arg(1);
    if (r < 0) return r;
    if (fd2 < 0 || fd2 >= NOFILE) return -EBADF;
    if (fd2 == (int)arg(0)) return fd2;
    if (curproc->ofile[fd2]) fileclose(curproc->ofile[fd2]);
    curproc->ofile[fd2] = filedup(f);
    return fd2;
}

static int sys_pipe(void)
{
    struct file *r, *w;
    int fds[2], e;
    if ((e = pipealloc(&r, &w)) < 0) return e;
    if ((fds[0] = fdalloc(r)) < 0) { fileclose(r); fileclose(w); return -EMFILE; }
    if ((fds[1] = fdalloc(w)) < 0) { curproc->ofile[fds[0]] = NULL; fileclose(r); fileclose(w); return -EMFILE; }
    if (copyout(arg(0), fds, 8) < 0) {
        curproc->ofile[fds[0]] = curproc->ofile[fds[1]] = NULL;
        fileclose(r); fileclose(w);
        return -EFAULT;
    }
    return 0;
}

static int sys_stat(void)
{
    char path[PATHMAX];
    struct inode *ip;
    struct stat st;
    int err;
    if ((err = getpath(path, arg(0))) < 0) return err;
    if (!(ip = namei(path, &err))) return err;
    ilock(ip);
    stati(ip, &st);
    iunlockput(ip);
    return copyout(arg(1), &st, sizeof st);
}

static int sys_fstat(void)
{
    struct file *f;
    struct stat st;
    int r = fdget(arg(0), &f);
    if (r < 0) return r;
    if (f->type != FD_INODE) {
        memset(&st, 0, sizeof st);
        st.st_mode = (f->type == FD_SOCKET ? IFSOCK : IFIFO) | 0600;
    } else {
        ilock(f->ip);
        stati(f->ip, &st);
        iunlock(f->ip);
    }
    return copyout(arg(1), &st, sizeof st);
}

static int sys_link(void)
{
    char old[PATHMAX], new_[PATHMAX], name[DIRSIZ + 1];
    struct inode *ip, *dp;
    int err;
    if ((err = getpath(old, arg(0))) < 0 || (err = getpath(new_, arg(1))) < 0) return err;
    if (!(ip = namei(old, &err))) return err;
    ilock(ip);
    if ((ip->d.mode & IFMT) == IFDIR && curproc->euid) { iunlockput(ip); return -EPERM; }
    ip->d.nlink++;
    iupdate(ip);
    iunlock(ip);
    if (!(dp = nameiparent(new_, name, &err))) goto bad;
    ilock(dp);
    if (dp->dev != ip->dev) { iunlockput(dp); err = -EXDEV; goto bad; }
    if (!access_ok(dp, 2)) { iunlockput(dp); err = -EACCES; goto bad; }
    if ((err = dirlink(dp, name, ip->inum)) < 0) { iunlockput(dp); goto bad; }
    iunlockput(dp);
    iput(ip);
    return 0;
bad:
    ilock(ip);
    ip->d.nlink--;
    iupdate(ip);
    iunlockput(ip);
    return err;
}

static int unlink_path(const char *path, int dir)
{
    char name[DIRSIZ + 1];
    struct inode *dp, *ip;
    struct dirent de;
    uint off;
    int err;
    if (!(dp = nameiparent(path, name, &err))) return err;
    ilock(dp);
    if (!strncmp(name, ".", DIRSIZ) || !strncmp(name, "..", DIRSIZ)) { iunlockput(dp); return -EINVAL; }
    if (!(ip = dirlookup(dp, name, &off))) { iunlockput(dp); return -ENOENT; }
    ilock(ip);
    if (!access_ok(dp, 2) || ((dp->d.mode & ISVTX) && curproc->euid && curproc->euid != ip->d.uid)) {
        iunlockput(ip); iunlockput(dp); return -EACCES;
    }
    if ((ip->d.mode & IFMT) == IFDIR) {
        if (!dir) { iunlockput(ip); iunlockput(dp); return -EISDIR; }
        if (mounted_on(ip)) { iunlockput(ip); iunlockput(dp); return -EBUSY; }
        if (!isdirempty(ip)) { iunlockput(ip); iunlockput(dp); return -ENOTEMPTY; }
    } else if (dir) { iunlockput(ip); iunlockput(dp); return -ENOTDIR; }
    memset(&de, 0, sizeof de);
    writei(dp, (uint)&de, 0, off, sizeof de);
    if ((ip->d.mode & IFMT) == IFDIR) {
        dp->d.nlink--;                  /* its .. */
        iupdate(dp);
        ip->d.nlink = 0;                /* . and the entry */
    } else
        ip->d.nlink--;
    ip->d.ctime = fs_time();
    iupdate(ip);
    iunlockput(dp);
    iunlockput(ip);
    return 0;
}

static int sys_unlink(void)
{
    char path[PATHMAX];
    int err;
    if ((err = getpath(path, arg(0))) < 0) return err;
    return unlink_path(path, 0);
}

static int sys_rmdir(void)
{
    char path[PATHMAX];
    int err;
    if ((err = getpath(path, arg(0))) < 0) return err;
    return unlink_path(path, 1);
}

static int sys_mkdir(void)
{
    char path[PATHMAX];
    struct inode *ip;
    int err;
    if ((err = getpath(path, arg(0))) < 0) return err;
    if (!(ip = create(path, IFDIR | (arg(1) & 07777 & ~curproc->umask), &err))) return err;
    if ((ip->d.mode & IFMT) != IFDIR) { iunlockput(ip); return -EEXIST; }
    iunlockput(ip);
    return 0;
}

static int sys_mknod(void)
{
    char path[PATHMAX];
    struct inode *ip;
    int err, mode = arg(1);
    if (curproc->euid) return -EPERM;
    if ((err = getpath(path, arg(0))) < 0) return err;
    if ((mode & IFMT) != IFCHR && (mode & IFMT) != IFBLK) return -EINVAL;   /* (no FIFOs yet) */
    mode = (mode & IFMT) | (mode & 07777 & ~curproc->umask);
    if (!(ip = create(path, mode, &err))) return err;
    if ((ip->d.mode & IFMT) == IFREG) { iunlockput(ip); return -EEXIST; }
    ip->d.addr[0] = arg(2);
    iupdate(ip);
    iunlockput(ip);
    return 0;
}

/* rename(old, new): new (replaced if it is a file) becomes old's name. The
   checks come before anything is removed, so a failure leaves both names
   as they were: renaming a file to itself (by any path) does nothing, and
   new on another filesystem or a directory is refused. */
static int sys_rename(void)
{
    char old[PATHMAX], new_[PATHMAX], name[DIRSIZ + 1];
    struct inode *ip, *np, *dp;
    int err, dev;
    uint inum;
    if ((err = getpath(old, arg(0))) < 0 || (err = getpath(new_, arg(1))) < 0) return err;
    if (!(ip = namei(old, &err))) return err;
    ilock(ip);
    if ((ip->d.mode & IFMT) == IFDIR) { iunlockput(ip); return -EISDIR; }    /* (files only, for now) */
    dev = ip->dev;
    inum = ip->inum;
    iunlockput(ip);
    if ((np = namei(new_, &err)) != 0) {
        int same = np->dev == dev && np->inum == inum, isdir;
        ilock(np);
        isdir = (np->d.mode & IFMT) == IFDIR;
        iunlockput(np);
        if (same) return 0;
        if (isdir) return -EISDIR;
    }
    if (!(dp = nameiparent(new_, name, &err))) return err;
    ilock(dp);
    err = dp->dev != dev ? -EXDEV : !access_ok(dp, 2) ? -EACCES : 0;
    iunlockput(dp);
    if (err) return err;
    if (np && (err = unlink_path(new_, 0)) < 0) return err;
    if ((err = sys_link()) < 0) return err;
    return unlink_path(old, 0);
}

static int sys_chdir(void)
{
    char path[PATHMAX];
    struct inode *ip;
    int err;
    if ((err = getpath(path, arg(0))) < 0) return err;
    if (!(ip = namei(path, &err))) return err;
    ilock(ip);
    if ((ip->d.mode & IFMT) != IFDIR) { iunlockput(ip); return -ENOTDIR; }
    if (!access_ok(ip, 1)) { iunlockput(ip); return -EACCES; }
    iunlock(ip);
    iput(curproc->cwd);
    curproc->cwd = ip;
    return 0;
}

static int sys_chmod(void)
{
    char path[PATHMAX];
    struct inode *ip;
    int err;
    if ((err = getpath(path, arg(0))) < 0) return err;
    if (!(ip = namei(path, &err))) return err;
    ilock(ip);
    if (curproc->euid && curproc->euid != ip->d.uid) { iunlockput(ip); return -EPERM; }
    ip->d.mode = (ip->d.mode & IFMT) | (arg(1) & 07777);
    ip->d.ctime = fs_time();
    iupdate(ip);
    iunlockput(ip);
    return 0;
}

static int sys_chown(void)
{
    char path[PATHMAX];
    struct inode *ip;
    int err;
    if (curproc->euid) return -EPERM;
    if ((err = getpath(path, arg(0))) < 0) return err;
    if (!(ip = namei(path, &err))) return err;
    ilock(ip);
    ip->d.uid = arg(1);
    ip->d.gid = arg(2);
    ip->d.ctime = fs_time();
    iupdate(ip);
    iunlockput(ip);
    return 0;
}

static int sys_access(void)
{
    char path[PATHMAX];
    struct inode *ip;
    int err, ok;
    ushort euid = curproc->euid, egid = curproc->egid;
    if ((err = getpath(path, arg(0))) < 0) return err;
    curproc->euid = curproc->uid;       /* access() checks the REAL ids */
    curproc->egid = curproc->gid;
    ip = namei(path, &err);
    if (ip) {
        ilock(ip);
        ok = !(arg(1) & 7) || access_ok(ip, arg(1) & 7);
        iunlockput(ip);
    }
    curproc->euid = euid;
    curproc->egid = egid;
    if (!ip) return err;
    return ok ? 0 : -EACCES;
}

static int sys_ioctl(void)
{
    struct file *f;
    int r = fdget(arg(0), &f);
    uint dev;
    if (r < 0) return r;
    if (f->type == FD_SOCKET) return sock_ioctl(f, arg(1), arg(2));
    if (f->type != FD_INODE || (f->ip->d.mode & IFMT) != IFCHR) return -ENOTTY;
    dev = f->ip->d.addr[0];
    if (major(dev) >= NCDEV || !cdevsw[major(dev)].ioctl) return -ENOTTY;
    if (arg(1) == USBIOWRITE && (f->flags & O_ACCMODE) == O_RDONLY) return -EBADF;   /* (it writes) */
    return cdevsw[major(dev)].ioctl(minor(dev), arg(1), arg(2));
}

/* ---------------------------------------------------------------- processes */
static int sys_exit(void) { exit((arg(0) & 0xFF) << 8); return 0; }
static int sys_fork(void) { return fork(); }
static int sys_wait(void) { return wait(arg(0)); }
static int sys_execve(void) { return exec(arg(0), arg(1), arg(2)); }
static int sys_getpid(void) { return curproc->pid; }
static int sys_getppid(void) { return curproc->parent ? curproc->parent->pid : 1; }
static int sys_getuid(void) { return curproc->uid; }
static int sys_geteuid(void) { return curproc->euid; }
static int sys_getgid(void) { return curproc->gid; }
static int sys_getegid(void) { return curproc->egid; }
static int sys_setuid(void)
{
    ushort u = arg(0);
    if (curproc->euid && u != curproc->uid) return -EPERM;
    curproc->uid = curproc->euid = u;
    return 0;
}
static int sys_setgid(void)
{
    ushort g = arg(0);
    if (curproc->euid && g != curproc->gid) return -EPERM;
    curproc->gid = curproc->egid = g;
    return 0;
}
static int sys_umask(void) { int old = curproc->umask; curproc->umask = arg(0) & 0777; return old; }
static int sys_sbrk(void)
{
    uint old = curproc->brk;
    int r = sbrk_to(curproc, old + arg(0));
    return r < 0 ? r : (int)old;
}
static int sys_kill(void) { return kill(arg(0), arg(1)); }
static int sys_signal(void)
{
    int sig = arg(0);
    uint old;
    if (sig <= 0 || sig >= NSIG || sig == SIGKILL) return -EINVAL;
    if (arg(2)) curproc->sighandler[0] = arg(2);       /* the library's trampoline */
    old = curproc->sighandler[sig];
    curproc->sighandler[sig] = arg(1);
    return old;
}
static int sys_sigreturn(void)
{
    struct trapframe saved;
    if (copyin(&saved, tf->usp, sizeof saved) < 0) { exit(SIGSEGV); return 0; }
    saved.status = ST_S | ST_PIE;       /* back to user mode, whatever the frame says */
    *tf = saved;
    return tf->r[1];
}
static int sys_pause(void)
{
    for (;;) {
        if (curproc->killed) return -EINTR;
        sleep(&curproc->sig);
    }
}
static int sys_alarm(void)
{
    uint left = curproc->alarm > ticks ? (curproc->alarm - ticks) / HZ : 0;
    curproc->alarm = arg(0) ? ticks + arg(0) * HZ : 0;
    return left;
}
static int sys_nice(void) { return 0; }
static int sys_time(void)
{
    uint t = fs_time();
    if (arg(0) && copyout(arg(0), &t, 4) < 0) return -EFAULT;
    return t;
}
extern uint boottime;
static int sys_stime(void)
{
    uint t;
    if (curproc->euid) return -EPERM;
    if (copyin(&t, arg(0), 4) < 0) return -EFAULT;
    boottime = t - ticks / HZ;
    rtc_set(t);                         /* the battery-backed clock keeps it (D23) */
    return 0;
}
static int sys_sync(void) { fs_sync(); return 0; }

/* times(buf): CPU ticks (HZ a second) of this process and its reaped
   children, user and system; returns the ticks since boot */
static int sys_times(void)
{
    uint t[4];
    t[0] = curproc->utime; t[1] = curproc->stime;
    t[2] = curproc->cutime; t[3] = curproc->cstime;
    if (arg(0) && copyout(arg(0), t, sizeof t) < 0) return -EFAULT;
    return ticks;
}

/* utime(path, times): times[1] becomes the modification time (P32W keeps no
   access time); times == 0: now. The owner or root (or, for now, anyone
   who may write the file). */
static int sys_utime(void)
{
    char path[PATHMAX];
    struct inode *ip;
    uint t[2];
    int err;
    if ((err = getpath(path, arg(0))) < 0) return err;
    if (arg(1)) { if (copyin(t, arg(1), sizeof t) < 0) return -EFAULT; }
    else t[1] = fs_time();
    if (!(ip = namei(path, &err))) return err;
    ilock(ip);
    if (curproc->euid && curproc->euid != ip->d.uid &&
        (arg(1) || !access_ok(ip, 2))) { iunlockput(ip); return -EPERM; }
    ip->d.mtime = t[1];
    ip->d.ctime = fs_time();
    iupdate(ip);
    iunlockput(ip);
    return 0;
}
static int sys_uname(void)
{
    char u[5][32];
    memset(u, 0, sizeof u);
    safestrcpy(u[0], "P32W Unix", 32);
    safestrcpy(u[1], "p32w", 32);
    safestrcpy(u[2], "0.7", 32);
    safestrcpy(u[3], "M7", 32);
    safestrcpy(u[4], "p32w", 32);
    return copyout(arg(0), u, sizeof u);
}

/* reboot(how): root only; writes everything out, marks the filesystem
   clean and stops the machine (the emulators exit) */
static int sys_reboot(void)
{
    if (curproc->euid) return -EPERM;
    if (!(arg(0) & RB_NOSYNC)) fs_clean();      /* every mounted filesystem written out and marked clean */
    if (arg(0) & RB_REBOOT) {           /* as the reset button: the monitor at 0 */
        kprintf("\nP32W Unix: rebooting\n");
        IOW(TCTRL) = 0;                 /* the clock off */
        mtcr_imask(0);
        reset();
    }
    kprintf("\nP32W Unix: halted\n");
    halt(arg(0) & 0xFF);
    return 0;
}

/* pinfo(slot, buf): one process-table slot for ps (P32W: no /dev/kmem) */
struct pinfo {
    int pid, ppid, state, uid, tty, size;
    uint time;
    char name[16];
    uint ntlb, ncow, nstack, cntlb, cncow, cnstack;     /* (after the first 44 bytes: new) */
};
static int sys_pinfo(void)
{
    int k = arg(0), size = arg(2) ? arg(2) : 44;    /* (the buffer's size; 0: an older program) */
    struct proc *p;
    struct pinfo pi;
    if (k < 0 || k >= NPROC) return -EINVAL;
    if (size < 0 || size > (int)sizeof pi) size = sizeof pi;
    p = &proc[k];
    memset(&pi, 0, sizeof pi);
    pi.pid = p->state == UNUSED ? 0 : p->pid;
    pi.ppid = p->parent ? p->parent->pid : 0;
    pi.state = p->state;
    pi.uid = p->uid;
    pi.tty = p->tty;
    pi.size = p->brk + (USERTOP - p->stackbot);
    pi.time = p->utime + p->stime;
    memcpy(pi.name, p->name, 16);
    pi.ntlb = p->ntlb + (p == curproc ? ntlbrefill - tlbmark : 0);    /* (the running one: so far) */
    pi.ncow = p->ncow;
    pi.nstack = p->nstack;
    pi.cntlb = p->cntlb;
    pi.cncow = p->cncow;
    pi.cnstack = p->cnstack;
    return copyout(arg(1), &pi, size);
}

/* sysinfo(buf): uptime (s), memory total and free (bytes), processes, the
   load average (x 2048), for uptime and top */
extern uint loadavg[3];
static int sys_sysinfo(void)
{
    uint v[10];
    struct proc *p;
    int size = arg(1) ? arg(1) : 28;    /* (the buffer's size; 0: a program from before the counters) */
    if (size < 0 || size > (int)sizeof v) size = sizeof v;
    v[0] = ticks / HZ;
    v[1] = ram_bytes;                   /* (the RAM fitted: the probe's, D52) */
    v[2] = nfree_frames() * PGSIZE;
    v[3] = 0;
    for (p = proc; p < &proc[NPROC]; p++) if (p->state != UNUSED) v[3]++;
    v[4] = loadavg[0]; v[5] = loadavg[1]; v[6] = loadavg[2];
    v[7] = ntlbrefill; v[8] = ncowcopy; v[9] = nstackpage;
    return copyout(arg(0), v, size);
}

/* ptrace(req, pid, addr, data) (26), for debuggers. A child asks to be
   traced (TRACEME); from then on it stops at every signal (and after exec,
   with SIGTRAP), and its parent's wait() reports the stop. Its parent, the
   debugger, may then read and write its memory and registers, and let it
   go on. Words travel through the data pointer (unlike V7, so no word is
   mistaken for an error). Requests: 0 TRACEME; 1 PEEK (the word at addr to
   *data); 4 POKE (data to the word at addr: a breakpoint goes into a
   private copy of a shared text page); 3 GETREGS (r0..r15, pc: 17 words to
   *data); 6 SETREGS (from *data); 7 CONT (data: the signal to deliver, or
   0); 8 KILL. */
#undef tf                               /* (here tf is the traced child's) */
static int sys_ptrace(void)
{
    int req = arg(0), pid = arg(1);
    uint addr = arg(2), data = arg(3), pa, regs[17];
    struct proc *c;
    int k;
    if (req == 0) { curproc->traced = 1; return 0; }
    for (c = proc; c < &proc[NPROC]; c++)
        if (c->pid == pid && c->parent == curproc && c->traced && c->state != UNUSED && c->state != ZOMBIE) break;
    if (c == &proc[NPROC]) return -ESRCH;
    if (req == 8) { psignal(c, SIGKILL); return 0; }
    if (c->state != STOPPED) return -ESRCH;
    switch (req) {
    case 1: case 2:
        if (addr & 3) return -EFAULT;
        if (!(pa = va2pa(c->pt, addr, 0))) {
            static uint zero;           /* (an untouched heap page reads as zeros) */
            if (addr < c->textend || addr >= PGROUND(c->brk)) return -EFAULT;
            pa = (uint)&zero;
        }
        return copyout(data, (void *)pa, 4);
    case 4: case 5:
        if (addr & 3) return -EFAULT;
        if (!va2pa(c->pt, addr, 0)) grow_heap(c, addr);        /* (an untouched heap page: made now) */
        if (!(pa = pt_private(c->pt, addr))) return -EFAULT;
        *(uint *)pa = data;
        return 0;
    case 3:
        regs[0] = 0;
        for (k = 1; k < 15; k++) regs[k] = c->tf->r[k];
        regs[15] = c->tf->usp;
        regs[16] = c->tf->epc;
        return copyout(data, regs, sizeof regs);
    case 6:
        if (copyin(regs, data, sizeof regs) < 0) return -EFAULT;
        for (k = 1; k < 15; k++) c->tf->r[k] = regs[k];
        c->tf->usp = regs[15];
        c->tf->epc = regs[16];
        return 0;
    case 7:
        if (data >= NSIG) return -EINVAL;
        c->contsig = data;
        c->state = RUNNABLE;
        return 0;
    }
    return -EINVAL;
}
#define tf (curproc->tf)

/* mount(special, dir, flags) (21): the filesystem on a block device on a
   directory; umount(special) (22). Root only. */
static int sys_mount(void)
{
    char path[PATHMAX];
    struct inode *dev, *dir;
    int err, d;
    if (curproc->euid) return -EPERM;
    if ((err = getpath(path, arg(0))) < 0) return err;
    if (!(dev = namei(path, &err))) return err;
    ilock(dev);
    if ((dev->d.mode & IFMT) != IFBLK) { iunlockput(dev); return -ENOTBLK; }
    d = dev->d.addr[0];
    iunlockput(dev);
    if (major(d) >= NBDEV || !bdevsw[major(d)].open) return -ENXIO;
    if ((err = bdevsw[major(d)].open(minor(d), 0)) < 0) return err;
    if ((err = getpath(path, arg(1))) < 0) return err;
    if (!(dir = namei(path, &err))) return err;
    ilock(dir);
    if ((dir->d.mode & IFMT) != IFDIR) { iunlockput(dir); return -ENOTDIR; }
    if (dir->inum == ROOTINO) { iunlockput(dir); return -EBUSY; }
    iunlock(dir);
    if ((err = fs_mount(d, dir)) < 0) iput(dir);
    return err;
}

static int sys_umount(void)
{
    char path[PATHMAX];
    struct inode *dev;
    int err, d;
    if (curproc->euid) return -EPERM;
    if ((err = getpath(path, arg(0))) < 0) return err;
    if (!(dev = namei(path, &err))) return err;
    ilock(dev);
    if ((dev->d.mode & IFMT) != IFBLK) { iunlockput(dev); return -ENOTBLK; }
    d = dev->d.addr[0];
    iunlockput(dev);
    return fs_umount(d);
}

/* ustat(dev, buf): a mounted filesystem's blocks, free blocks, inodes and
   free inodes (dev: a block device's number, st_rdev), for df; EINVAL if
   nothing is mounted from it */
static int sys_ustat(void)
{
    uint v[5];
    struct superblock *s = mounted_super(arg(0));
    if (!s) return -EINVAL;
    v[0] = s->nblocks; v[1] = s->free_blocks; v[2] = s->ninodes; v[3] = s->free_inodes;
    v[4] = mounted_wasclean(arg(0));
    return copyout(arg(1), v, sizeof v);
}

/* fcntl(fd, cmd, arg): F_DUPFD, F_GETFL / F_SETFL (O_NONBLOCK and O_APPEND
   may change; O_NONBLOCK is for sockets, D45), F_GETFD / F_SETFD (no
   close-on-exec yet: 0) */
static int sys_fcntl(void)
{
    struct file *f;
    int fd, r = fdget(arg(0), &f);
    if (r < 0) return r;
    switch (arg(1)) {
    case F_DUPFD:
        if ((int)arg(2) < 0 || (int)arg(2) >= NOFILE) return -EINVAL;
        for (fd = arg(2); fd < NOFILE; fd++)
            if (!curproc->ofile[fd]) { curproc->ofile[fd] = filedup(f); return fd; }
        return -EMFILE;
    case F_GETFD: case F_SETFD: return 0;
    case F_GETFL: return f->flags;
    case F_SETFL:
        f->flags = (f->flags & ~(O_NONBLOCK | O_APPEND)) | (arg(2) & (O_NONBLOCK | O_APPEND));
        return 0;
    }
    return -EINVAL;
}

/* setsid(): leave the controlling terminal; the next terminal opened becomes it */
static int sys_setsid(void) { curproc->tty = -1; return curproc->pid; }

/* (entries 75-160: networking, D45, at 4.2BSD's numbers; 160, netctl, is P32W's) */
static int (*const calls[])(void) = {
    0, sys_exit, sys_fork, sys_read, sys_write, sys_open, sys_close, sys_wait, sys_creat,
    sys_link, sys_unlink, sys_execve, sys_chdir, sys_time, sys_mknod, sys_chmod, sys_chown,
    sys_sbrk, sys_stat, sys_lseek, sys_getpid, sys_mount, sys_umount, sys_setuid, sys_getuid, sys_stime, sys_ptrace,
    sys_alarm, sys_fstat, sys_pause, sys_utime, 0, 0, sys_access, sys_nice, 0, sys_sync, sys_kill,
    0, sys_getppid, 0, sys_dup, sys_pipe, sys_times, 0, 0, sys_setgid, sys_getgid, sys_signal,
    sys_geteuid, sys_getegid, 0, 0, 0, sys_ioctl, 0, 0, 0, 0, 0, sys_umask, sys_mkdir,
    sys_rmdir, sys_rename, sys_dup2, sys_uname, sys_sigreturn, 0, 0, 0, sys_reboot, sys_pinfo, sys_setsid, sys_sysinfo, sys_ustat,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, sys_fcntl, sys_select, 0,
    0, 0, sys_socket, sys_connect, sys_accept, 0, 0, 0, 0, sys_bind,
    sys_setsockopt, sys_listen, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, sys_getsockopt, 0, 0, 0, 0, 0, 0,
    sys_recvfrom, 0, 0, 0, 0, 0, 0, 0, sys_sendto, sys_shutdown,
    0, 0, 0, 0, 0, 0, sys_getpeername, 0, 0, 0,
    0, 0, 0, 0, 0, sys_getsockname, 0, 0, 0, 0,
    0, 0, 0, 0, 0, sys_netctl,
};
#define NCALLS (sizeof calls / sizeof calls[0])

void syscall(struct trapframe *t)
{
    uint n = t->r[1];
    int r;
    t->epc += 4;                        /* continue after the SYSCALL */
    if (n < NCALLS && calls[n]) {
        r = calls[n]();
        if (n == SYS_sigreturn || (n == SYS_execve && r == 0)) return;
    } else
        r = -ENOSYS;
    tf->r[1] = r;
}
