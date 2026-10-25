/* assert.h -- P32W C library (M2, D22) */
#undef assert
#ifdef NDEBUG
#define assert(e) ((void)0)
#else
void __assert(const char *expr, const char *file, int line);
#define assert(e) ((e) ? (void)0 : __assert(#e, __FILE__, __LINE__))
#endif
