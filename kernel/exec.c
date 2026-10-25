/* exec.c -- replace the current program (M3, D22; os-design.md 3.2, 4.2).
 *
 * An a.out (assembler/p32ld.py): a 32-byte header, text, data. The new
 * address space: text at 0x1000 (read + execute), data and bss after it on
 * the next page (read + write), the break after the bss, and the stack
 * below 0x0040_0000 holding argc, argv[], NULL, envp[], NULL and the
 * strings. A set-user-id file runs with its owner's effective uid.
 */
#include "defs.h"

#define AOUT_MAGIC 0x50333277

struct aout { uint magic, text, data, bss, entry, flags, r0, r1; };

/* the argument strings, copied in before the old program goes */
static int getargs(uint uv, char *buf, uint *used, uint *ptrs, int *n)
{
    int k;
    for (k = 0;; k++) {
        uint up;
        int len;
        if (k >= NCARGS) return -E2BIG;
        if (copyin(&up, uv + 4 * k, 4) < 0) return -EFAULT;
        if (!up) break;
        len = copyinstr(buf + *used, up, MAXARG - *used);
        if (len < 0) return len == -ENAMETOOLONG ? -E2BIG : len;
        ptrs[*n + k] = *used;
        *used += len + 1;
    }
    *n += k;
    return k;
}

int exec(uint upath, uint uargv, uint uenvp)
{
    char path[256], *args;
    struct inode *ip;
    struct aout h;
    uint *pt = NULL, sp, used = 0, *ptrs, va, end, *uptrs;     /* (the tables: a page, not the kernel stack) */
    int err, argc, envc, n = 0, k;
    struct proc *p = curproc;
    ushort euid = p->euid, egid = p->egid;

    if ((err = copyinstr(path, upath, sizeof path)) < 0) return err;
    if (!(ip = namei(path, &err))) return err;
    ilock(ip);
    if ((ip->d.mode & IFMT) != IFREG || !access_ok(ip, 1)) { iunlockput(ip); return -EACCES; }
    if (readi(ip, (uint)&h, 0, 0, sizeof h) != sizeof h || h.magic != AOUT_MAGIC ||
        h.entry < USERTEXT || h.entry >= USERTEXT + h.text) { iunlockput(ip); return -ENOEXEC; }
    if (ip->d.mode & ISUID) euid = ip->d.uid;
    if (ip->d.mode & ISGID) egid = ip->d.gid;
    if (!(args = (char *)frame_alloc())) { iunlockput(ip); return -ENOMEM; }
    if (!(ptrs = (uint *)frame_alloc())) { frame_free((uint)args); iunlockput(ip); return -ENOMEM; }
    uptrs = ptrs + 2 * NCARGS;          /* (4 NCARGS + 3 words fit the page) */
    argc = uargv ? getargs(uargv, args, &used, ptrs, &n) : 0;
    envc = (argc >= 0 && uenvp) ? getargs(uenvp, args, &used, ptrs, &n) : 0;
    if (argc < 0 || envc < 0) { frame_free((uint)args); frame_free((uint)ptrs); iunlockput(ip); return argc < 0 ? argc : envc; }

    /* the new address space */
    err = -ENOMEM;
    if (!(pt = pt_new())) goto fail;
    /* (a page the file fills whole is not zeroed first: the load overwrites
       every byte, and a short file fails the exec before it can run) */
    end = USERTEXT + h.text;
    for (va = USERTEXT; va < end; va += PGSIZE)
        if ((va + PGSIZE <= end ? map_page_raw : map_page)(pt, va, PTE_X) < 0) goto fail;
    va = PGROUND(end);                              /* data + bss */
    end = va + h.data + h.bss;
    for (k = va; (uint)k < end; k += PGSIZE)
        if (((uint)k + PGSIZE <= va + h.data ? map_page_raw : map_page)(pt, k, PTE_W) < 0) goto fail;
    if (readi_pages(ip, pt, USERTEXT, sizeof h, h.text) != (int)h.text ||     /* the contents */
        readi_pages(ip, pt, va, sizeof h + h.text, h.data) != (int)h.data) { err = -ENOEXEC; goto fail; }
    /* the stack: strings at the top, then argc, argv[], NULL, envp[], NULL */
    {
        uint strs = (USERTOP - used) & ~3;
        sp = strs - 4 * (3 + argc + envc);
        for (va = sp & ~(PGSIZE - 1); va < USERTOP; va += PGSIZE)
            if (map_page(pt, va, PTE_W) < 0) goto fail;
        uptrs[0] = argc;
        for (k = 0; k < argc; k++) uptrs[1 + k] = strs + ptrs[k];
        uptrs[1 + argc] = 0;
        for (k = 0; k < envc; k++) uptrs[2 + argc + k] = strs + ptrs[argc + k];
        uptrs[2 + argc + envc] = 0;
        for (k = 0; k < (int)used; k++)
            *(char *)va2pa(pt, strs + k, 0) = args[k];
        for (k = 0; k < 3 + argc + envc; k++)
            *(uint *)va2pa(pt, sp + 4 * k, 0) = uptrs[k];
    }
    iunlockput(ip);
    frame_free((uint)args);
    frame_free((uint)ptrs);

    /* commit: the old program is gone */
    pt_free(p->pt);
    heap_unreserve(p->nlazy);           /* (its untouched heap pages) */
    p->nlazy = 0;
    p->pt = pt;
    tlbflush();
    switchuvm(p);
    p->textend = PGROUND(h.text + USERTEXT);
    p->brk = (PGROUND(h.text + USERTEXT) + h.data + h.bss + 7) & ~7u;   /* (aligned: a bss may be of odd size) */
    p->stackbot = sp & ~(PGSIZE - 1);
    p->euid = euid;
    p->egid = egid;
    for (k = 1; k < NSIG; k++)          /* caught signals revert to default; ignored stay ignored */
        if (p->sighandler[k] != (uint)SIG_IGN) p->sighandler[k] = 0;
    {
        char *s = path, *last = path;
        for (; *s; s++) if (*s == '/') last = s + 1;
        safestrcpy(p->name, last, sizeof p->name);
    }
    memset(p->tf, 0, sizeof *p->tf);
    p->tf->epc = h.entry;
    p->tf->usp = sp;
    p->tf->status = ST_S | ST_PIE;
    if (p->traced) psignal(p, SIGTRAP);     /* a debugger sees the new program before it runs */
    return 0;

fail:
    if (pt) pt_free(pt);
    frame_free((uint)args);
    frame_free((uint)ptrs);
    iunlockput(ip);
    return err;
}
