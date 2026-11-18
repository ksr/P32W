/* core.h -- the debug card's firmware, apart from its hardware (docs/debug-card-theory.md; D135).
 *
 * The host (the Mac's tools/dbgcard.py, or a terminal) sends one command a line; each reply is any number of
 * data lines and then one line beginning OK or ERR. The commands are listed by HELP and in
 * hardware/debug-card/firmware/README.md. Numbers are hex.
 */
#ifndef DBG_CORE_H
#define DBG_CORE_H

#define DBG_VERSION "1"

/* Power-on: the local bus let go, CRESN low; then, if the card answers, its control logic out of reset. */
void dbg_init(void);
/* One command line (no line end; the text may be changed). The reply goes out through hal_put(). */
void dbg_line(char *line);

#endif
