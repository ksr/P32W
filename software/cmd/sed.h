/* sed.h -- the stream editor's regular expressions (sedre.c), used by sed.c.
 * recomp compiles a POSIX basic (or, with RE_EXT, extended) regular
 * expression; reexec finds its leftmost-longest match in s[start..len). */
typedef struct re Re;
#define RE_EXT   1              /* extended syntax: + ? | ( ) { } */
#define RE_ICASE 2              /* ignore the case of letters */
#define RE_NSUB  10             /* \0 (the whole match) to \9 */
Re *recomp(const char *pat, int flags, char **err);
int reexec(Re *re, const char *s, int len, int start, int *sub);
int rensub(Re *re);
