# lex: a tokenizer for a small C-like language (keywords, names, numbers,
# strings, comments in an exclusive start condition, operators, yylineno,
# ^ and $, trailing context, yymore, yyless, input, unput, REJECT, an
# inclusive start condition, a definition, a class), with libl's main (cc
# -ll) and a yywrap of its own; lex -t; errors and statuses. (Compiling
# the scanner is most of the time: some 80 seconds of P32W time.)
echo "== a tokenizer"
cat > tok.l <<'EOF'
%{
/* tokens of a small C-like language, one per line */
int ntok, nshe, nhe;
static void tok(char *kind)
{
    printf("%d %s '%s'\n", yylineno, kind, yytext);
    ntok++;
}
%}
%option yylineno
%x C
%s UP
D       [0-9]
ID      [[:alpha:]_][[:alnum:]_]*
%%
she                 { nshe++; REJECT; }
he                  { nhe++; REJECT; }
<UP>[a-z]+          { int i; for (i = 0; i < yyleng; i++) yytext[i] -= 32; tok("upper"); }
"^^"                BEGIN(UP);
"^."                BEGIN(INITIAL);
"/*"                BEGIN(C);
<C>"*/"             BEGIN(INITIAL);
<C>[^*\n]+          ;
<C>"*"|\n           ;
"//".*              ;
^"#".*$             tok("directive");
if|else|while|int|return   tok("keyword");
{ID}/[ \t]*"("      tok("call");
{ID}                tok("name");
0[xX][[:xdigit:]]+  |
{D}+                tok("number");
\"(\\.|[^\\"\n])*\" tok("string");
"=="|"!="|"<="|">="|"&&"|"||"   tok("op2");
[-+*/%=<>!;,(){}]   tok("op");
"@"[a-z]+           { yymore(); }
"!"[a-z]+           tok("more");
"$"[0-9]+           { yyless(1); tok("dollar"); }
"`"                 { int c; while ((c = input()) != '`' && c != EOF) ; tok("quoted"); }
"~"                 { unput('3'); unput('2'); unput('1'); }
[ \t\n]+            ;
.                   tok("other");
%%
int yywrap(void)
{
    printf("%d tokens, %d lines, she %d, he %d\n", ntok, yylineno, nshe, nhe);
    return 1;
}
EOF
cat > prog <<'EOF'
#include <stdio.h>
int main(int argc) /* a comment
over two lines */ {
    int x = 0x1F + 42; // a comment
    if (x >= 10 && argc != 2)
        printf("a \"string\"\n", x);
    while(x) x = x-1;
    return @ab!cd $123 `skip this` ~ ;
}
#end
she sells shells; he says ^^ up and up ^. down
EOF
lex tok.l
echo "status $?"
lex -t tok.l > t.c
cmp t.c lex.yy.c && echo "lex -t: the same"
cc -o tok lex.yy.c -ll
echo "cc status $?"
./tok < prog
./tok < /dev/null
echo "== errors"
cat > bad1.l <<'EOF'
%%
a(b	ECHO;
EOF
lex bad1.l
echo "status $?"
cat > bad2.l <<'EOF'
%%
{nope}	ECHO;
EOF
lex bad2.l
echo "status $?"
cat > bad3.l <<'EOF'
%%
<X>a	ECHO;
EOF
lex bad3.l
echo "status $?"
cat > bad4.l <<'EOF'
%%
abc { if (x) {
EOF
lex bad4.l
echo "status $?"
cat > bad5.l <<'EOF'
%%
x |
EOF
lex bad5.l
echo "status $?"
cat > warn.l <<'EOF'
%%
x ECHO;
x ECHO;
EOF
lex warn.l
echo "status $?"
lex nosuch.l
echo "status $?"
lex -q
echo "status $?"
