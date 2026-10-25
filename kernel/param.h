/* param.h -- P32W Unix kernel limits (M3, D22) */
#define NPROC     64        /* processes; slot k uses ASID k + 1 */
#define NOFILE    20        /* open files per process */
#define NFILE     128       /* open files in the system */
#define NINODE    96        /* in-memory inodes */
#define NBUF      48        /* buffer cache blocks (48 KiB) */
#define HZ        50        /* clock ticks a second */
#define TICK      20000     /* timer ticks per clock tick (1 MHz timer; 1 per instruction in the emulators) */
#define QUANTUM   2         /* clock ticks a process runs before it must yield */
#define MAXARG    4096      /* bytes of exec arguments */
#define NCARGS    250       /* exec arguments (and as many environment strings); 4 NCARGS + 3 words fit a page */
#define USERTOP   0x00800000 /* top of a user address space (8 MiB, D34) */
#define NPTE      (USERTOP / 4096)   /* a page table's entries: 2048, in two contiguous frames */
#define PTFRAMES  (NPTE / 1024)
#define USERTEXT  0x00001000 /* where a.out text starts */
#define MAXSTACK  (1024 * 1024) /* how far the stack may grow (1 MiB, D41; 512 KiB before) */
#define ROOTDEV   0         /* block device 0, minor 0: the CF card's filesystem area */
