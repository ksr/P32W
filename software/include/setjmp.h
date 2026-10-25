/* setjmp.h -- P32W C library (M2, D22): r8-r14, sp and the return address. */
#ifndef _SETJMP_H
#define _SETJMP_H
typedef int jmp_buf[10];
int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);
#endif
