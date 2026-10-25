/* awklex.c -- awk's lexer (awk.h): the whole program becomes an array of
 * tokens before parsing, so the parser can look ahead (and back) freely.
 * A newline is a token, except after { && || , do else and another newline,
 * and after a backslash. A / starts a regular expression unless the token
 * before it ends an operand (a name, a number, a string, ) ] ++ --), in
 * which case it divides. A name written right against ( is a function
 * call. String escapes are done here; a regular expression keeps its
 * backslashes (but \/ becomes /) for awkre.c. */
#include "awk.h"

Tok *toks;
int ntoks;
static int tcap;

static const char *const keywords[] = {
    "BEGIN", "END", "function", "func", "if", "else", "while", "for", "do", "break", "continue",
    "next", "nextfile", "exit", "return", "delete", "getline", "print", "printf", "in", 0
};
static const int keytok[] = {
    TK_BEGIN, TK_END, TK_FUNCTION, TK_FUNCTION, TK_IF, TK_ELSE, TK_WHILE, TK_FOR, TK_DO, TK_BREAK,
    TK_CONTINUE, TK_NEXT, TK_NEXTFILE, TK_EXIT, TK_RETURN, TK_DELETE, TK_GETLINE, TK_PRINT,
    TK_PRINTF, TK_IN
};
static const char *const builtins[] = {
    "length", "substr", "index", "split", "sub", "gsub", "match", "sprintf", "tolower", "toupper",
    "int", "sqrt", "exp", "log", "sin", "cos", "atan2", "rand", "srand", "close", "system", "fflush", 0
};

static void addtok(int t, int line, int off, int end, char *s, int n)
{
    int prev = ntoks ? toks[ntoks - 1].t : TK_NL;
    if (t == TK_NL && (prev == TK_NL || prev == '{' || prev == TK_AND || prev == TK_OR || prev == ','
                       || prev == TK_DO || prev == TK_ELSE))
        return;
    if (ntoks >= tcap) {
        tcap = tcap * 2 + 256;
        toks = erealloc(toks, tcap * sizeof(Tok));
    }
    toks[ntoks].t = t;
    toks[ntoks].line = line;
    toks[ntoks].off = off;
    toks[ntoks].end = end;
    toks[ntoks].s = s;
    toks[ntoks].n = n;
    ntoks++;
}

/* an error while lexing: the context is the text so far */
static void lexerr(int off, int end, int line, const char *msg)
{
    addtok(TK_EOF, line, off, end, 0, 0);
    synerr(ntoks - 1, msg);
}

/* does the token before end an operand (so / divides)? */
static int operand_before(void)
{
    int t;
    if (!ntoks) return 0;
    t = toks[ntoks - 1].t;
    return t == TK_NAME || t == TK_NUMBER || t == TK_STRING || t == TK_ERE || t == ')' || t == ']'
        || t == TK_INCR || t == TK_DECR || (t == TK_BUILTIN && toks[ntoks - 1].n == B_LENGTH)
        || t == '$';
}

void lex(void)
{
    char *p = progtext, *start;
    int line = 1, c, t, i, n;
    Buf b;

    for (;;) {
        start = p;
        c = (unsigned char)*p;
        if (!c) break;
        if (c == ' ' || c == '\t' || c == '\r') { p++; continue; }
        if (c == '\\' && (p[1] == '\n' || (p[1] == '\r' && p[2] == '\n'))) {
            p += p[1] == '\n' ? 2 : 3;
            line++;
            continue;
        }
        if (c == '#') { while (*p && *p != '\n') p++; continue; }
        if (c == '\n') { addtok(TK_NL, line, p - progtext, p - progtext + 1, 0, 0); line++; p++; continue; }
        if (isdigit(c) || (c == '.' && isdigit((unsigned char)p[1]))) {
            while (isdigit((unsigned char)*p)) p++;
            if (*p == '.') { p++; while (isdigit((unsigned char)*p)) p++; }
            if ((*p == 'e' || *p == 'E') && (isdigit((unsigned char)p[1]) ||
                ((p[1] == '+' || p[1] == '-') && isdigit((unsigned char)p[2])))) {
                p += 2;
                while (isdigit((unsigned char)*p)) p++;
            }
            addtok(TK_NUMBER, line, start - progtext, p - progtext, sdupn(start, p - start), 0);
            continue;
        }
        if (isalpha(c) || c == '_') {
            while (isalnum((unsigned char)*p) || *p == '_') p++;
            n = p - start;
            t = TK_NAME;
            for (i = 0; keywords[i]; i++)
                if ((int)strlen(keywords[i]) == n && !strncmp(keywords[i], start, n)) { t = keytok[i]; break; }
            if (t == TK_NAME)
                for (i = 0; builtins[i]; i++)
                    if ((int)strlen(builtins[i]) == n && !strncmp(builtins[i], start, n)) { t = TK_BUILTIN; break; }
            if (t == TK_NAME && *p == '(') t = TK_FUNCNAME;
            addtok(t, line, start - progtext, p - progtext, t == TK_NAME || t == TK_FUNCNAME ? sdupn(start, n) : 0,
                   t == TK_BUILTIN ? i : 0);
            continue;
        }
        if (c == '"') {
            buf_init(&b);
            p++;
            for (;;) {
                c = (unsigned char)*p;
                if (!c || c == '\n') lexerr(start - progtext, p - progtext, line, "non-terminated string");
                p++;
                if (c == '"') break;
                if (c != '\\') { buf_addc(&b, c); continue; }
                c = (unsigned char)*p++;
                switch (c) {
                case '\n': line++; break;              /* (a continued string) */
                case 'n': buf_addc(&b, '\n'); break;
                case 't': buf_addc(&b, '\t'); break;
                case 'r': buf_addc(&b, '\r'); break;
                case 'f': buf_addc(&b, '\f'); break;
                case 'b': buf_addc(&b, '\b'); break;
                case 'v': buf_addc(&b, '\v'); break;
                case 'a': buf_addc(&b, '\007'); break;
                case '\\': buf_addc(&b, '\\'); break;
                case '"': buf_addc(&b, '"'); break;
                case '/': buf_addc(&b, '/'); break;
                case 0: lexerr(start - progtext, p - progtext, line, "non-terminated string");
                default:
                    if (c >= '0' && c <= '7') {
                        n = c - '0';
                        if (*p >= '0' && *p <= '7') { n = n * 8 + *p++ - '0'; if (*p >= '0' && *p <= '7') n = n * 8 + *p++ - '0'; }
                        buf_addc(&b, n);
                    } else {
                        buf_addc(&b, '\\');
                        buf_addc(&b, c);
                    }
                }
            }
            addtok(TK_STRING, line, start - progtext, p - progtext, buf_take(&b), 0);
            continue;
        }
        if (c == '/' && !operand_before()) {   /* a regular expression */
            int inbr = 0;
            buf_init(&b);
            p++;
            for (;;) {
                c = (unsigned char)*p;
                if (!c || c == '\n') lexerr(start - progtext, p - progtext, line, "newline in regular expression");
                if (c == '/' && !inbr) { p++; break; }
                if (c == '\\' && p[1] == '/') { buf_addc(&b, '/'); p += 2; continue; }
                if (c == '\\' && p[1] && p[1] != '\n') { buf_addc(&b, c); buf_addc(&b, p[1]); p += 2; continue; }
                if (c == '[' && !inbr) {
                    inbr = 1;
                    buf_addc(&b, c);
                    p++;
                    if (*p == '^') buf_addc(&b, *p++);
                    if (*p == ']') buf_addc(&b, *p++);
                    continue;
                }
                if (c == '[' && inbr && p[1] == ':') {
                    char *e = strstr(p + 2, ":]");
                    if (e && !memchr(p, '\n', e - p)) { buf_add(&b, p, e + 2 - p); p = e + 2; continue; }
                }
                if (c == ']' && inbr) inbr = 0;
                buf_addc(&b, c);
                p++;
            }
            addtok(TK_ERE, line, start - progtext, p - progtext, buf_take(&b), 0);
            continue;
        }
        /* operators */
        p++;
        t = c;
        switch (c) {
        case '+': if (*p == '+') { p++; t = TK_INCR; } else if (*p == '=') { p++; t = TK_ADDEQ; } break;
        case '-': if (*p == '-') { p++; t = TK_DECR; } else if (*p == '=') { p++; t = TK_SUBEQ; } break;
        case '*':
            if (*p == '*') {
                p++;
                t = TK_POW;
                if (*p == '=') { p++; t = TK_POWEQ; }
            } else if (*p == '=') { p++; t = TK_MULEQ; }
            break;
        case '/': if (*p == '=') { p++; t = TK_DIVEQ; } break;
        case '%': if (*p == '=') { p++; t = TK_MODEQ; } break;
        case '^': t = TK_POW; if (*p == '=') { p++; t = TK_POWEQ; } break;
        case '!': if (*p == '=') { p++; t = TK_NE; } else if (*p == '~') { p++; t = TK_NOMATCH; } break;
        case '<': if (*p == '=') { p++; t = TK_LE; } break;
        case '>':
            if (*p == '=') { p++; t = TK_GE; }
            else if (*p == '>') { p++; t = TK_APPEND; }
            break;
        case '=': if (*p == '=') { p++; t = TK_EQ; } break;
        case '&':
            if (*p == '&') { p++; t = TK_AND; break; }
            lexerr(start - progtext, p - progtext, line, "syntax error");
        case '|': if (*p == '|') { p++; t = TK_OR; } break;
        case '{': case '}': case '(': case ')': case '[': case ']': case ';': case ',':
        case '?': case ':': case '~': case '$':
            break;
        default:
            lexerr(start - progtext, p - progtext, line, "syntax error");
        }
        addtok(t, line, start - progtext, p - progtext, 0, 0);
    }
    addtok(TK_NL, line, p - progtext, p - progtext, 0, 0);
    addtok(TK_EOF, line, p - progtext, p - progtext, 0, 0);
}

/* syntax error at token ti: the line it is on, the token marked */
void synerr(int ti, const char *msg)
{
    Tok *k = &toks[ti];
    int ls = k->off, i;
    fflush(stdout);
    fprintf(stderr, "awk: %s at source line %d\n context is\n\t", msg, k->line);
    while (ls > 0 && progtext[ls - 1] != '\n') ls--;
    for (i = ls; i < k->off; i++) fputc(progtext[i], stderr);
    fprintf(stderr, " >>> ");
    for (i = k->off; i < k->end && progtext[i] != '\n'; i++) fputc(progtext[i], stderr);
    fprintf(stderr, " <<< \n");
    exit(2);
}
