/* defs.h -- every kernel file includes this (M3, D22) */
#include "types.h"
#include "param.h"
#include <signal.h>
#include <errno.h>
#include "p32.h"
#include "fs.h"
#include "buf.h"
#include "file.h"
#include "proc.h"

struct stat;

/* klib.c */
void *memset(void *d, int c, uint n);
void *memcpy(void *d, const void *s, uint n);
void *memmove(void *d, const void *s, uint n);
int memcmp(const void *a, const void *b, uint n);
uint strlen(const char *s);
int strncmp(const char *a, const char *b, uint n);
char *safestrcpy(char *d, const char *s, int n);

/* kprintf.c */
void kprintf(const char *fmt, ...);
void panic(const char *msg);
void kputc(int c);

/* vm.c */
extern uint *curpt;
void kinit(void);
uint frame_alloc(void);
uint frame_alloc_raw(void);
void frame_free(uint pa);
int nfree_frames(void);
extern uint ram_bytes, ramtop;
int ram_fitted(uint a);
extern volatile int probing, probe_fault;
uint *pt_new(void);
void pt_free(uint *pt);
int map_page(uint *pt, uint va, uint perm);
int map_page_raw(uint *pt, uint va, uint perm);
struct proc;
int heap_reserve(uint n);
void heap_unreserve(uint n);
int grow_heap(struct proc *p, uint va);
void unmap_range(uint *pt, uint from, uint to);
uint *pt_copy(uint *pt);
int cow_break(uint *pt, uint va);
uint va2pa(uint *pt, uint va, int write);
uint pt_private(uint *pt, uint va);
int copyin(void *dst, uint src, uint n);
int copyout(uint dst, const void *src, uint n);
int copyinstr(char *dst, uint src, uint max);
int user_ok(uint va, uint n, int write);
int grow_stack(struct proc *p, uint va);
int sbrk_to(struct proc *p, uint newbrk);
void switchuvm(struct proc *p);

/* proc.c */
void procinit(void);
struct proc *allocproc(void);
void userinit(void);
void scheduler(void);
void sched(void);
void yield(void);
void swtch(struct context *old, struct context *new_);
void sleep(void *chan);
void wakeup(void *chan);
int fork(void);
void exit(int status);
int wait(uint statusp);
int kill(int pid, int sig);
void psignal(struct proc *p, int sig);
void issig(void);
extern uint ticks;
extern int need_resched;

/* trap.c */
void trap(struct trapframe *tf);
void clockintr(int user);
void reset(void);
#define RB_REBOOT 0x100                 /* reboot(): restart instead of halting */
#define RB_NOSYNC 0x200                 /* reboot(): without writing the cache out (fsck -y on /) */

/* syscall.c */
void syscall(struct trapframe *tf);
uint arg(int n);

/* exec.c */
int exec(uint path, uint argv, uint envp);

/* bio.c */
void binit(void);
struct buf *bread(int dev, uint blkno);
int cached_blocks(int dev, uint *out, int max);
void bdrop(int dev);
struct buf *bnoread(int dev, uint blkno);
void bwrite(struct buf *b);
void bdwrite(struct buf *b);
void brelse(struct buf *b);
void bflush(void);
void binval(int dev);
void fs_clean(void);
int ram_open(int minor, int flags);
int ram_close(int minor);
void ram_strategy(struct buf *b);
int usb_init(void);
int usb_open(int minor, int flags);
int usb_close(int minor);
void usb_strategy(struct buf *b);
int usb_rawopen(int minor, int flags);
int usb_rawread(int minor, uint dst, int n, uint off);
int usb_rawwrite(int minor, uint src, int n, uint off);
int usb_rawioctl(int minor, int req, uint arg);
#define USBDEV (2 << 8)                 /* /dev/usb: block device 2, minor 0 */

/* cf.c */
int cf_init(void);
int cf_open(int minor, int flags);
int cf_close(int minor);
void cf_strategy(struct buf *b);
int cf_rw(uint sector, uchar *data, int nsect, int write);
int ata_rw(int d, uint sector, uchar *data, int nsect, int write);
struct seg;
int cf_readv(int minor, uint blkno, int nblk, uint skip, struct seg *seg, int nseg);

/* rtc.c */
int rtc_init(void);
uint rtc_get(void);
void rtc_set(uint t);

/* fs.c */
extern struct mount mounts[];
#define sb (mounts[0].super)                /* the root filesystem's superblock */
struct superblock *sbof(int dev);
int fs_mount(int dev, struct inode *on);
int fs_umount(int dev);
extern uint fs_start;
void fsinit(int dev);
struct inode *iget(int dev, uint inum);
struct inode *idup(struct inode *ip);
void ilock(struct inode *ip);
void iunlock(struct inode *ip);
void iput(struct inode *ip);
void iunlockput(struct inode *ip);
void iupdate(struct inode *ip);
struct inode *ialloc(int dev, ushort mode);
void itrunc(struct inode *ip);
int readi(struct inode *ip, uint dst, int user, uint off, uint n);
int readi_pages(struct inode *ip, uint *pt, uint va, uint off, uint n);
int writei(struct inode *ip, uint src, int user, uint off, uint n);
struct inode *dirlookup(struct inode *dp, const char *name, uint *poff);
int dirlink(struct inode *dp, const char *name, uint inum);
int isdirempty(struct inode *dp);
struct inode *namei(const char *path, int *err);
int mounted_on(struct inode *ip);
struct superblock *mounted_super(int dev);
int mounted_wasclean(int dev);
struct inode *nameiparent(const char *path, char *name, int *err);
int access_ok(struct inode *ip, int mode);
void stati(struct inode *ip, struct stat *st);
void fs_sync(void);
uint fs_time(void);

/* file.c */
void fileinit(void);
struct file *filealloc(void);
struct file *filedup(struct file *f);
void fileclose(struct file *f);
int fileread(struct file *f, uint dst, int n);
int filewrite(struct file *f, uint src, int n);
int pipealloc(struct file **r, struct file **w);
int file_inready(struct file *f);
int fdget(int fd, struct file **f);
int fdalloc(struct file *f);
/* net.c: sockets through the network coprocessor (D45) */
struct sock;
extern int selchan;
int net_init(void);
void netintr(void);
void net_tick(void);
void sock_close(struct sock *s);
int sock_read(struct file *f, uint dst, int n);
int sock_write(struct file *f, uint src, int n);
int sock_ioctl(struct file *f, int cmd, uint arg);
int sys_socket(void), sys_bind(void), sys_listen(void), sys_accept(void), sys_connect(void);
int sys_sendto(void), sys_recvfrom(void), sys_shutdown(void), sys_setsockopt(void), sys_getsockopt(void);
int sys_getsockname(void), sys_getpeername(void), sys_netctl(void), sys_select(void);
int devopen(struct inode *ip, int flags);

/* tty.c */
void ttyinit(void);
void ttyintr(int unit);
int tty_ready(int minor);
int ptyopen(int minor, int flags);
int ptyclose(int minor);
int ptyread(int minor, uint dst, int n, uint off);
int ptywrite(int minor, uint src, int n, uint off);
int ptyioctl(int minor, int cmd, uint arg);
int pty_ready(int minor);
int ttyopen(int minor, int flags);
int ttyclose(int minor);
int ttyread(int minor, uint dst, int n, uint off);
int ttywrite(int minor, uint src, int n, uint off);
int ttyioctl(int minor, int cmd, uint arg);
int cttyopen(int minor, int flags);
int cttyclose(int minor);
int cttyread(int minor, uint dst, int n, uint off);
int cttywrite(int minor, uint src, int n, uint off);
int cttyioctl(int minor, int cmd, uint arg);
void tty_kputc(int c);
void vtinput(int c);

/* video.c */
extern int vt_present;
int video_init(void);
void vt_putc(int c);
void vt_flush(void);
void vt_sync(void);
int vt_room(void);
void vidintr(void);
void vtstart(void);                     /* (tty.c) */
void kbdintr(void);
void kbdtick(void);
int vgopen(int minor, int flags);       /* /dev/video (D110) */
int vgclose(int minor);
int vgread(int minor, uint dst, int n, uint off);
int vgwrite(int minor, uint src, int n, uint off);
int vgioctl(int minor, int cmd, uint arg);
int msopen(int minor, int flags);       /* /dev/mouse (D120) */
int msclose(int minor);
int msread(int minor, uint dst, int n, uint off);
int ms_ready(void);
int probe8(uint a);
