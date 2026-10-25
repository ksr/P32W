/* main.c -- liby's main (cc ... -ly): POSIX's library for yacc programs.
 * A parser needs only yylex and yyparse of its own: this main calls
 * yyparse once and exits with its result (0 accepted, 1 not). It is a
 * member of its own in liby.a, so a program with its own main never
 * takes it. */
int yyparse(void);

int main(void)
{
    return yyparse();
}
