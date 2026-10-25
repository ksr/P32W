/* yywrap.c -- libl's yywrap (cc ... -ll): called by yylex at the end of
 * its input; returning 1 says there is no more (a yywrap of the
 * program's own may open the next file as yyin and return 0). A member of
 * its own in libl.a, so a program may have its own yywrap and still take
 * libl's main. */
int yywrap(void)
{
    return 1;
}
