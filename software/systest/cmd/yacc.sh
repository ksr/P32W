# yacc: a calculator (%union and %type, precedence and %prec, %nonassoc,
# a mid-rule action, $<type>$, error recovery with yyerrok, YYERROR and
# YYACCEPT) made with -d, compiled with cc and liby (-ly: main and yyerror),
# run on good and bad input; y.tab.h; -b and -p, -l, -v; the conflicts of
# ambiguous grammars (shift/reduce, reduce/reduce) and what y.output says
# of them; errors in grammars; usage
cat > calc.y <<'EOF'
%{
#include <stdio.h>
#include <ctype.h>
int yylex(void);
int yyerror(char *s);
int vars[26];
%}
%union {
    int n;
    char c;
}
%token <n> NUM
%token <c> VAR
%token QUIT
%type <n> expr
%nonassoc '<'
%left '+' '-'
%left '*' '/' '%'
%right NEG
%%
lines : /* empty */
      | lines line
      ;
line  : '\n'
      | expr '\n'              { printf("%d\n", $1); }
      | VAR '=' { $<n>$ = vars[$1 - 'a']; } expr '\n'
                               { vars[$1 - 'a'] = $4; printf("%c: %d -> %d\n", $1, $<n>3, $4); }
      | QUIT '\n'              { printf("bye\n"); YYACCEPT; }
      | error '\n'             { yyerrok; printf("try again\n"); }
      ;
expr  : expr '+' expr          { $$ = $1 + $3; }
      | expr '-' expr          { $$ = $1 - $3; }
      | expr '*' expr          { $$ = $1 * $3; }
      | expr '/' expr          { if ($3 == 0) { printf("divide by zero\n"); YYERROR; } $$ = $1 / $3; }
      | expr '%' expr          { if ($3 == 0) YYERROR; $$ = $1 % $3; }
      | expr '<' expr          { $$ = $1 < $3; }
      | '-' expr %prec NEG     { $$ = -$2; }
      | '(' expr ')'           { $$ = $2; }
      | NUM
      | VAR                    { $$ = vars[$1 - 'a']; }
      ;
%%
int yylex(void)
{
    int c = getchar();
    while (c == ' ' || c == '\t')
        c = getchar();
    if (c == EOF)
        return 0;
    if (isdigit(c)) {
        yylval.n = 0;
        for (; isdigit(c); c = getchar())
            yylval.n = yylval.n * 10 + c - '0';
        ungetc(c, stdin);
        return NUM;
    }
    if (c == 'q')
        return QUIT;
    if (islower(c)) {
        yylval.c = c;
        return VAR;
    }
    return c;
}
EOF
yacc -d calc.y
echo "status $?"
cat y.tab.h
cc -o calc y.tab.c -ly
echo "status $?"
./calc > out 2> err <<'EOF'
1 + 2 * 3
(1 + 2) * 3
-2 * -3 - 10 % 4
7 - 2 - 1
x = 6 * 7
x = x + 1
x / 5 + y

1 + * 2
3 < 4
1 < 2 < 3
8 / (4 - 4)
2 +
x % 0
100
q
1 + 1
EOF
echo "status $?"
cat out
echo "-- the standard error"
cat err
echo "1 2" | ./calc 2>&1
echo "status $?"
echo "== -b, -p, -l, -v"
yacc -b c -p calc -l -v calc.y
echo "status $?"
ls c.*
grep -c calcparse c.tab.c
grep -c '#line' y.tab.c c.tab.c
grep states c.output
grep terminals c.output
echo "== conflicts"
cat > amb.y <<'EOF'
%token NUM IF ELSE X
%%
s : e | st ;
e : e '+' e | e '*' e | '(' e ')' | NUM ;
st : IF '(' e ')' st | IF '(' e ')' st ELSE st | X ';' ;
EOF
yacc -v amb.y
echo "status $?"
grep conflict y.output
cat > rr.y <<'EOF'
%%
s : a 'x' | b 'x' | c 'y' ;
a : 'p' ;
b : 'p' ;
c : 'p' | 'q' ;
EOF
yacc -v rr.y
grep conflict y.output
grep -c reduced y.output
echo "== %left resolves it"
(echo "%left '+'"; echo "%left '*'"; cat amb.y) > prec.y
yacc -v prec.y
echo "status $?"
grep -c resolved y.output
echo "== errors"
echo "%token A" > e1.y
yacc e1.y
echo "status $?"
cat > e2.y <<'EOF'
%token A
%%
s : A b ;
EOF
yacc e2.y
cat > e3.y <<'EOF'
%union { int i; }
%token <i> A
%%
s : A { $$ = $2; } ;
EOF
yacc e3.y
cat > e4.y <<'EOF'
%token A
%%
s : A { x = 1; ;
EOF
yacc e4.y
cat > e5.y <<'EOF'
%token A
%foo
%%
s : A ;
EOF
yacc e5.y
cat > e6.y <<'EOF'
%token A
%%
A : s ;
s : A ;
EOF
yacc e6.y
cat > e7.y <<'EOF'
%%
s : s 'x' ;
EOF
yacc e7.y
echo "status $?"
ls
yacc
echo "status $?"
yacc -x calc.y
echo "status $?"
yacc nosuch.y
echo "status $?"
