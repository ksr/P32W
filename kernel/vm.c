/* vm.c -- physical frames and user address spaces (M3, D22; os-design.md 4.2).
 *
 * Every process has one page table: 2,048 entries in TLBLO format (PFN | X |
 * W | V) in two physically contiguous frames, covering user addresses 0 to
 * 8 MiB (D34). The TLB-miss fast path in start.s indexes it with the
 * faulting VPN, so a refill is one load. The kernel runs untranslated and reaches user memory
 * through the page table (va2pa, copyin, copyout). TLB entries are tagged
 * with the process's ASID (its slot + 1), so a switch needs no flush; the
 * whole TLB is flushed when a mapping is removed or a slot is reused.
 *
 * fork shares pages (M6): every frame has a reference count; a writable
 * page becomes read-only + PTE_COW in both processes, and the first write
 * (a PROTECT fault, or the kernel's copyout) gives the writer its own copy
 * -- or, if it is the last sharer, simply makes the page writable again.
 * Text pages (read-only) are shared outright.
 */
#include "defs.h"

extern char _end[];
uint *curpt;                    /* the running process's page table (start.s reads it) */
static uint freelist;           /* physical frames, each holding the next's address */
static int nfree;
static int nreserved;           /* frames promised to heaps (sbrk), not yet mapped: no
                                   other allocation may take them */
static uchar fref[(RAMTOP - RAMBASE) / PGSIZE];  /* sharers of each frame */
#define REF(pa) fref[((pa) - RAMBASE) / PGSIZE]

/* RAM is up to four memory cards (D50, D52), each jumpered to a window
   (window n = n x 16 MiB) and fitted with 1 to 8 banks of 2 MiB (D60); the
   card in window 0 holds the ROM and the kernel. A bank that is not there
   does not answer: an access is a BUSERROR, which probe() turns into a failed
   probe. Every bank found is RAM, with holes between them allowed; the
   frames of the others are marked in use for good, so that nothing
   (pt_new's search included) ever takes them. */
#define BANK (2u << 20)
uint ram_bytes;                 /* RAM fitted (sysinfo) */
uint ramtop = BANK;             /* the end of the highest bank */
static uint banks = 1;          /* bit (address / 2 MiB): fitted */

volatile int probing, probe_fault;  /* (trap.c skips a probe's faulting access) */

static int bank_fitted(uint a)
{
    volatile uint *p = (volatile uint *)(a + 0x100);
    uint v, old;
    probe_fault = 0;
    probing = 1;
    old = *p;                           /* (put back after: it may be in use) */
    if (!probe_fault) {
        *p = 0x5AA5C33Cu;
        v = *p;
        *p = old;
    }
    probing = 0;
    return !probe_fault && v == 0x5AA5C33Cu;
}

/* is physical address a in RAM that is fitted? */
int ram_fitted(uint a)
{
    return a >= RAMBASE && a < RAMTOP && (banks >> (a / BANK) & 1);
}

/* the byte at a, or -1 if nothing answers there (a card not fitted) */
int probe8(uint a)
{
    volatile uchar *p = (volatile uchar *)a;
    int v;
    probe_fault = 0;
    probing = 1;
    v = *p;
    probing = 0;
    return probe_fault ? -1 : v;
}

void kinit(void)
{
    uint a = PGROUND((uint)_end), b;
    for (b = BANK; b < RAMTOP; b += BANK)       /* (bank 0 of window 0 holds the kernel) */
        if (bank_fitted(b)) {
            banks |= 1u << (b / BANK);
            ramtop = b + BANK;
        }
    for (b = 0, ram_bytes = 0; b < RAMTOP; b += BANK)
        if (banks >> (b / BANK) & 1) ram_bytes += BANK;
    ram_bytes -= RAMBASE;                       /* (the ROM hides the first MiB) */
    for (; a + PGSIZE <= RAMTOP; a += PGSIZE)
        if (ram_fitted(a)) frame_free(a);
        else REF(a) = 0xFF;                     /* (not there) */
}

uint frame_alloc(void)
{
    uint a = frame_alloc_raw();
    if (a) memset((void *)a, 0, PGSIZE);
    return a;
}

/* a frame as it is, for a caller about to fill every byte (exec) */
uint frame_alloc_raw(void)
{
    uint a = freelist;
    if (!a || nfree <= nreserved) return 0;
    freelist = *(uint *)a;
    nfree--;
    REF(a) = 1;
    return a;
}

void frame_free(uint pa)
{
    if (pa & (PGSIZE - 1) || pa < RAMBASE || pa >= RAMTOP) panic("frame_free");
    if (REF(pa) > 1) { REF(pa)--; return; }         /* still shared */
    REF(pa) = 0;
    *(uint *)pa = freelist;
    freelist = pa;
    nfree++;
}

int nfree_frames(void) { return nfree; }

/* a page table: PTFRAMES free frames side by side, zeroed (0 if memory
   is too fragmented, or full). Found by the frames' reference counts; the
   free list is then walked once to take them out */
uint *pt_new(void)
{
    uint first = PGROUND((uint)_end), a, *pp;
    int i, k, n = (RAMTOP - first) / PGSIZE;
    for (i = 0; i + PTFRAMES <= n; i++) {
        for (k = 0; k < PTFRAMES && !REF(first + (i + k) * PGSIZE); k++)
            ;
        if (k == PTFRAMES) break;
    }
    if (i + PTFRAMES > n || nfree - PTFRAMES < nreserved) return 0;
    a = first + i * PGSIZE;
    for (pp = &freelist; *pp; )
        if (*pp >= a && *pp < a + PTFRAMES * PGSIZE) { *pp = *(uint *)*pp; nfree--; }
        else pp = (uint *)*pp;
    for (k = 0; k < PTFRAMES; k++) REF(a + k * PGSIZE) = 1;
    memset((void *)a, 0, PTFRAMES * PGSIZE);
    return (uint *)a;
}

/* every page the table maps, then the table */
void pt_free(uint *pt)
{
    int i;
    if (!pt) return;
    for (i = 0; i < NPTE; i++)
        if (pt[i] & PTE_V) frame_free(pt[i] & ~(PGSIZE - 1));
    for (i = 0; i < PTFRAMES; i++) frame_free((uint)pt + i * PGSIZE);
}

/* a fresh zeroed page at user address va; 0 or -ENOMEM */
int map_page(uint *pt, uint va, uint perm)
{
    uint pa;
    uint i = va >> 12;
    if (va >= USERTOP) return -EFAULT;
    if (pt[i] & PTE_V) {
        if (pt[i] & PTE_COW) { int r = cow_break(pt, va); if (r < 0) return r; }
        pt[i] = (pt[i] & ~(PTE_W | PTE_X)) | perm | PTE_V;
        return 0;
    }
    if (!(pa = frame_alloc())) return -ENOMEM;
    pt[i] = pa | perm | PTE_V;
    return 0;
}

/* a new page at va whose every byte the caller is about to fill: not zeroed */
int map_page_raw(uint *pt, uint va, uint perm)
{
    uint pa;
    if (va >= USERTOP || (pt[va >> 12] & PTE_V)) return map_page(pt, va, perm);
    if (!(pa = frame_alloc_raw())) return -ENOMEM;
    pt[va >> 12] = pa | perm | PTE_V;
    return 0;
}

void unmap_range(uint *pt, uint from, uint to)
{
    uint va;
    for (va = from & ~(PGSIZE - 1); va < to; va += PGSIZE) {
        uint i = va >> 12;
        if (pt[i] & PTE_V) {
            frame_free(pt[i] & ~(PGSIZE - 1));
            pt[i] = 0;
        }
    }
    tlbflush();
}

/* fork: the same pages, shared; writable ones copy-on-write in both */
uint *pt_copy(uint *pt)
{
    uint *n = pt_new();
    int i;
    if (!n) return NULL;
    for (i = 0; i < NPTE; i++) {
        if (!(pt[i] & PTE_V)) continue;
        if (pt[i] & (PTE_W | PTE_COW))
            pt[i] = (pt[i] & ~PTE_W) | PTE_COW;
        n[i] = pt[i];
        if (REF(pt[i] & ~(PGSIZE - 1)) == 255) panic("fork: a page shared 255 times");
        REF(pt[i] & ~(PGSIZE - 1))++;
    }
    tlbflush();                         /* the parent's writable entries are gone */
    return n;
}

/* the first write to a copy-on-write page: 0, or -ENOMEM (-EFAULT if not COW) */
int cow_break(uint *pt, uint va)
{
    uint i = va >> 12, old, pa;
    if (va >= USERTOP || !(pt[i] & PTE_V) || !(pt[i] & PTE_COW)) return -EFAULT;
    old = pt[i] & ~(PGSIZE - 1);
    if (REF(old) > 1) {
        if (!(pa = frame_alloc())) return -ENOMEM;
        memcpy((void *)pa, (void *)old, PGSIZE);
        REF(old)--;
        ncowcopy++;
        if (curproc && pt == curproc->pt) curproc->ncow++;
    } else
        pa = old;                       /* the last sharer: it is ours */
    pt[i] = pa | (pt[i] & PTE_X) | PTE_W | PTE_V;
    return 0;
}

/* the page at va the process's own (not shared with another, even if
   read-only), so a debugger can write a breakpoint into it: its physical
   address, or 0 */
uint pt_private(uint *pt, uint va)
{
    uint i = va >> 12, old, pa;
    if (va >= USERTOP || !(pt[i] & PTE_V)) return 0;
    if (pt[i] & PTE_COW) { if (cow_break(pt, va) < 0) return 0; }
    old = pt[i] & ~(PGSIZE - 1);
    if (REF(old) > 1) {
        if (!(pa = frame_alloc())) return 0;
        memcpy((void *)pa, (void *)old, PGSIZE);
        REF(old)--;
        pt[i] = pa | (pt[i] & (PGSIZE - 1));
    }
    tlbflush();
    return (pt[i] & ~(PGSIZE - 1)) | (va & (PGSIZE - 1));
}

/* the physical address of a user address, or 0 */
uint va2pa(uint *pt, uint va, int write)
{
    uint e;
    if (va >= USERTOP) return 0;
    e = pt[va >> 12];
    if (!(e & PTE_V) || (write && !(e & PTE_W))) return 0;
    return (e & ~(PGSIZE - 1)) | (va & (PGSIZE - 1));
}

/* the stack grows on demand below its lowest page, down to MAXSTACK */
int grow_stack(struct proc *p, uint va)
{
    uint a;
    if (va >= USERTOP || va < USERTOP - MAXSTACK || va < PGROUND(p->brk) + PGSIZE) return -1;
    for (a = va & ~(PGSIZE - 1); a < p->stackbot; a += PGSIZE) {
        if (map_page(p->pt, a, PTE_W) < 0) return -1;
        p->nstack++;
        nstackpage++;
    }
    p->stackbot = va & ~(PGSIZE - 1);
    return 0;
}

uint ntlbrefill, ncowcopy, nstackpage;     /* (ntlbrefill: counted by start.s's refill) */

static uint xlate(uint va, int write)
{
    uint pa;
    if (write && va < USERTOP && (curproc->pt[va >> 12] & PTE_COW)) {
        if (cow_break(curproc->pt, va) < 0) return 0;
        tlbflush();                     /* (a stale read-only entry may be in the TLB) */
    }
    pa = va2pa(curproc->pt, va, write);
    if (!pa && (grow_heap(curproc, va) == 0 || (va < curproc->stackbot && grow_stack(curproc, va) == 0)))
        pa = va2pa(curproc->pt, va, write);
    return pa;
}

int user_ok(uint va, uint n, int write)
{
    uint a;
    if (n == 0) return 1;
    if (va + n < va || va + n > USERTOP) return 0;
    for (a = va & ~(PGSIZE - 1); a < va + n; a += PGSIZE)
        if (!xlate(a < va ? va : a, write)) return 0;
    return 1;
}

int copyin(void *dst, uint src, uint n)
{
    uchar *d = dst;
    while (n) {
        uint pa = xlate(src, 0), c;
        if (!pa) return -EFAULT;
        c = PGSIZE - (src & (PGSIZE - 1));
        if (c > n) c = n;
        memcpy(d, (void *)pa, c);
        d += c; src += c; n -= c;
    }
    return 0;
}

int copyout(uint dst, const void *src, uint n)
{
    const uchar *s = src;
    while (n) {
        uint pa = xlate(dst, 1), c;
        if (!pa) return -EFAULT;
        c = PGSIZE - (dst & (PGSIZE - 1));
        if (c > n) c = n;
        memcpy((void *)pa, s, c);
        s += c; dst += c; n -= c;
    }
    return 0;
}

/* a NUL-terminated string from user space: its length, -EFAULT, -ENAMETOOLONG */
int copyinstr(char *dst, uint src, uint max)
{
    uint k;
    for (k = 0; k < max; k++) {
        uint pa = xlate(src + k, 0);
        if (!pa) return -EFAULT;
        dst[k] = *(char *)pa;
        if (!dst[k]) return k;
    }
    return -ENAMETOOLONG;
}

/* move the break: map or unmap whole pages */
/* Heap pages are promised by sbrk and made on first use: a fault on one (or
   copyin/copyout reaching it) maps a zeroed frame. Each promise holds a
   frame in reserve (nreserved), so that a page sbrk granted is always there
   when touched: sbrk fails, as before, when memory is short, rather than a
   later access. fork promises the child's untouched pages too; exit and
   exec give back what was never used. */
int heap_reserve(uint n)
{
    if ((int)n > nfree - nreserved) return -ENOMEM;
    nreserved += n;
    return 0;
}

void heap_unreserve(uint n) { nreserved -= n; }

/* va is an untouched heap page of p: map it (a zeroed frame, from p's
   reserve). 0, or -1 if it is not one */
int grow_heap(struct proc *p, uint va)
{
    if (!p->nlazy || va < p->textend || va >= PGROUND(p->brk) || (p->pt[va >> 12] & PTE_V)) return -1;
    p->nlazy--;
    nreserved--;
    if (map_page(p->pt, va & ~(PGSIZE - 1), PTE_W) < 0) { p->nlazy++; nreserved++; return -1; }  /* (cannot fail) */
    return 0;
}

int sbrk_to(struct proc *p, uint newbrk)
{
    uint old = PGROUND(p->brk), new_ = PGROUND(newbrk), a;
    if (newbrk < p->textend || new_ + PGSIZE > p->stackbot) return -ENOMEM;
    if (new_ > old) {                   /* promised, not mapped */
        if (heap_reserve((new_ - old) / PGSIZE) < 0) return -ENOMEM;
        p->nlazy += (new_ - old) / PGSIZE;
    } else if (new_ < old) {            /* the touched pages freed, the untouched given back */
        for (a = new_; a < old; a += PGSIZE)
            if (!(p->pt[a >> 12] & PTE_V)) { p->nlazy--; nreserved--; }
        unmap_range(p->pt, new_, old);
    }
    p->brk = newbrk;
    return 0;
}

void switchuvm(struct proc *p)
{
    curpt = p->pt;
    mtcr_asid(p->asid);
}
