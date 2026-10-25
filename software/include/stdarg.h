/* stdarg.h -- P32W C library (M2, D22). A variadic function receives every
 * argument on the stack, in words: one each, or as many as a struct takes
 * (os-design.md 3.3, D30), so a va_list is a pointer that walks them. */
#ifndef _STDARG_H
#define _STDARG_H
typedef char *va_list;
#define __va_words(n)      (((n) + 3) & ~3)
#define va_start(ap, last) ((ap) = (char *)&(last) + __va_words(sizeof(last)))
#define va_arg(ap, type)   (*(type *)(((ap) += __va_words(sizeof(type))) - __va_words(sizeof(type))))
#define va_end(ap)         ((void)0)
#define va_copy(d, s)      ((d) = (s))
#endif
