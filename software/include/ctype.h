/* ctype.h -- P32W C library (M2, D22) */
#ifndef _CTYPE_H
#define _CTYPE_H
int isalpha(int c);
int isdigit(int c);
int isalnum(int c);
int isspace(int c);
int isupper(int c);
int islower(int c);
int isxdigit(int c);
int isprint(int c);
int iscntrl(int c);
int ispunct(int c);
int isgraph(int c);
int toupper(int c);
int tolower(int c);
/* The ones programs call all the time, without a call, as cc and as do for
   each character (D89). The arithmetic ones are the functions' equals for any
   int; the table takes the argument as an unsigned char. Each uses c once. */
extern const unsigned char __ctype[256];
#define isdigit(c) ((unsigned)(c) - '0' < 10)
#define isupper(c) ((unsigned)(c) - 'A' < 26)
#define islower(c) ((unsigned)(c) - 'a' < 26)
#define isalpha(c) (((unsigned)(c) | 32) - 'a' < 26)
#define isalnum(c) (__ctype[(unsigned char)(c)] & 1)
#define isspace(c) (__ctype[(unsigned char)(c)] & 2)
#endif
