/* main.c -- libl's main (cc ... -ll): POSIX's library for lex programs.
 * A scanner needs only its rules: this main calls yylex until it returns
 * 0 (the end of the input, or a rule's return 0) and exits with status 0.
 * It is a member of its own in libl.a, so a program with its own main
 * never takes it. */
int yylex(void);

int main(void)
{
    while (yylex() != 0)
        ;
    return 0;
}
