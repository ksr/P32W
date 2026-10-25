/* p32.h -- the machine (isa-system.md, programmers-guide.md) */
/* control registers */
#define CR_STATUS  0
#define CR_EPC     1
#define CR_CAUSE   2
#define CR_BADADDR 3
#define CR_VBR     4
#define CR_USP     5
#define CR_IPEND   6
#define CR_IMASK   7
#define CR_ASID    8
#define CR_TLBHI   9
#define CR_TLBLO   10
/* STATUS bits */
#define ST_S   1
#define ST_IE  2
#define ST_PS  4
#define ST_PIE 8
/* exception causes */
#define EXC_ILLEGAL    1
#define EXC_PRIVILEGE  2
#define EXC_MISALIGNED 3
#define EXC_BUSERROR   4
#define EXC_SYSCALL    5
#define EXC_BREAK      6
#define EXC_TLBMISS_X  8
#define EXC_TLBMISS_R  9
#define EXC_TLBMISS_W  10
#define EXC_PROTECT    11
#define CAUSE_IRQ      0x80000000
/* interrupt lines */
#define IRQ_TIMER 0
#define IRQ_DISK  1
#define IRQ_UART0 2
#define IRQ_UART1 3
#define IRQ_PS2   4               /* the video card: the PS/2 keyboard and mouse (D50, D52) */
#define IRQ_NET   7               /* the I/O card's network coprocessor: ATTN (D45) */
/* interrupt vectors (D57): CAUSE bits 15:8 = the answering card's window * 16
   + its device's interrupt status bit; VEC_NONE when no card answered */
#define WIN_VIDEO 5               /* the default jumpers (D52) */
#define WIN_DISK  6
#define WIN_IO    7
#define VECTOR(win, dev) ((win) * 16 + (dev))
#define VEC_NONE  0xFF
/* page-table entries = TLBLO */
#define PTE_V 1
#define PTE_W 2
#define PTE_X 4
#define PTE_COW 8               /* software: shared until written (the TLB ignores bits 11:3) */
#define PGSIZE 4096
#define PGROUND(a) (((a) + PGSIZE - 1) & ~(PGSIZE - 1))
/* the I/O page */
#define IOB(a) (*(volatile unsigned char *)(a))
#define IOW(a) (*(volatile unsigned *)(a))
#define UART0 0xFFFFFF00
#define UART1 0xFFFFFF08
#define UART2 0xFFFFFF30          /* (the I/O card's four UARTs, D50, D52) */
#define UART3 0xFFFFFF38
#define TCOUNT  0xFFFFFF10
#define TRELOAD 0xFFFFFF14
#define TCTRL   0xFFFFFF18
#define PORTIN  0xFFFFFF20
#define PORTOUT 0xFFFFFF21
#define CFBASE  0xFFFFEF00          /* the disk card, window 6 (D52): port 0, the CF card */
#define USBBASE 0xFFFFFF50          /* the I/O card's first CH376S (D43) */
#define USBBASE1 0xFFFFFF54         /* ... and its second (D50, D52) */
#define VIDREG  0xFFFFDF00          /* the video card, window 5 (D52): its registers ... */
#define VIDMEM  0xFE050000          /* ... and its RAM (the text at +0) */
#define NETBASE 0xFFFFFF52          /* ... its network coprocessor's mailbox (D45) */
#define RAMBASE 0x00100000
#define CARD_RAM 0x01000000         /* a memory card: 16 MiB (D50, D52) */
#define RAMTOP  0x04000000          /* at most four cards; ramtop (vm.c) is what is fitted */

/* start.s */
uint mfcr_status(void);
void mtcr_status(uint v);
uint mfcr_ipend(void);
void mtcr_imask(uint v);
uint mfcr_imask(void);
void mtcr_asid(uint v);
void tlbwrite(uint hi, uint lo);
void tlbflush(void);
uint mfcr_tlbhi(void);
void waitirq(void);
void halt(int code);
int splhi(void);            /* interrupts off; returns the old STATUS */
void splx(int s);           /* restore STATUS.IE */
void spl0(void);            /* interrupts on */
