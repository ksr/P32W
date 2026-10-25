/* stdbool.h -- P32W C library (C99): bool is _Bool, which holds 0 or 1 --
 * any non-zero value converted to it becomes 1 (D35). */
#ifndef _STDBOOL_H
#define _STDBOOL_H
#define bool _Bool
#define true 1
#define false 0
#define __bool_true_false_are_defined 1
#endif
