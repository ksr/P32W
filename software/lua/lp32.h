/*
** lp32.h -- Lua on P32W Unix: the configuration that luaconf.h leaves to
** the platform (see README.P32W). Included at the end of luaconf.h.
**
** With LUA_P32HOST instead of __P32__ (a test build on another machine)
** only the single-precision conversions are used.
*/
#ifndef lp32_h
#define lp32_h

#include <stddef.h>
#include <stdio.h>


/* IEEE single precision for string.pack's "f" (lstrlib.c), when the
   compiler's float is a double */
unsigned lp32_tof32 (double x);
double lp32_fromf32 (unsigned f);


#if defined(__P32__)	/* { */

/* string.pack's "f" is 4 bytes (P32W's float is a double) */
#define LP32_FLOAT32

/* no locales: the decimal point is always '.' */
#undef lua_getlocaledecpoint
#define lua_getlocaledecpoint()	'.'

/* a process has 8 MiB: at most 16000 stack slots (12 bytes each, and a
   36-byte call record per call), so that a runaway recursion is a "stack
   overflow", not out of memory */
#undef LUAI_MAXSTACK
#define LUAI_MAXSTACK		16000

/* where 'require' looks: /usr/lib/lua/5.4, then the current directory
   (there are no C modules: P32W Unix has no dynamic linking; the C path
   only shows in 'module not found' messages) */
#undef LUA_PATH_DEFAULT
#define LUA_PATH_DEFAULT  \
		"/usr/lib/lua/" LUA_VDIR "/?.lua;" "/usr/lib/lua/" LUA_VDIR "/?/init.lua;" \
		"./?.lua;" "./?/init.lua"
#undef LUA_CPATH_DEFAULT
#define LUA_CPATH_DEFAULT	"/usr/lib/lua/" LUA_VDIR "/?.so"

/* io.popen (libc's popen and pclose) */
#define l_popen(L,c,m)		(fflush(NULL), popen(c,m))
#define l_pclose(L,file)	(pclose(file))
#include <sys/wait.h>
#define l_inspectstat(stat,what)  \
   if (WIFEXITED(stat)) { stat = WEXITSTATUS(stat); } \
   else if (WIFSIGNALED(stat)) { stat = WTERMSIG(stat); what = "signal"; }

/* os.tmpname: a new file in /tmp, made by mkstemp (as luaconf.h does
   with LUA_USE_POSIX, which needs more than P32W has) */
#include <stdlib.h>
#include <string.h>
#define LUA_TMPNAMBUFSIZE	32
#define lua_tmpnam(b,e) { \
        strcpy(b, "/tmp/lua_XXXXXX"); \
        e = mkstemp(b); \
        if (e != -1) close(e); \
        e = (e == -1); }

/* lua.c: interactive when the input is a terminal */
#include <unistd.h>
#define lua_stdin_is_tty()	isatty(0)

#endif			/* } */

#endif
