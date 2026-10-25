/* sys/ptrace.h -- P32W Unix: tracing a child process (debuggers; kernel/syscall.c) */
#ifndef _SYS_PTRACE_H
#define _SYS_PTRACE_H
#define PT_TRACEME  0   /* the child: let my parent trace me */
#define PT_PEEK     1   /* the word at addr -> *(unsigned *)data */
#define PT_GETREGS  3   /* r0..r15, pc -> data (17 words) */
#define PT_POKE     4   /* data -> the word at addr (text too: a private copy) */
#define PT_SETREGS  6   /* data (17 words) -> the registers */
#define PT_CONT     7   /* go on; data: a signal to deliver, or 0 */
#define PT_KILL     8
#define PT_NREGS    17  /* (index 15 is sp, 16 is pc) */
int ptrace(int req, int pid, unsigned addr, unsigned data);
#endif
