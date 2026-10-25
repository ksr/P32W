/* yyerror.c -- liby's yyerror (cc ... -ly): writes yyparse's message
 * ("syntax error") and a newline on the standard error. A member of its
 * own in liby.a, so a program may have its own yyerror and still take
 * liby's main. */
#include <stdio.h>

int yyerror(const char *s)
{
    fprintf(stderr, "%s\n", s);
    return 0;
}
