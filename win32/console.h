#ifndef RUBY_WIN32_CONSOLE_H
#define RUBY_WIN32_CONSOLE_H

#include "ruby/win32.h"

/*
 * Console reader thread API (internal use only)
 *
 * This module provides a dedicated thread for console input reading.
 * The thread reads from CONIN$ using ReadConsoleW (which handles line
 * editing in cooked mode) and writes to a named pipe. Ruby's read/select
 * operations then use the pipe for non-blocking I/O.
 *
 * Key design points:
 * - Single thread handles all console input (stdin, CONIN$, etc.)
 * - Reference counting (waiters) manages when to read vs interrupt
 * - PIPE_TYPE_MESSAGE preserves line boundaries in cooked mode
 * - ReadConsoleW interruption via CONSOLE_READCONSOLE_CONTROL + WriteConsoleInputW
 * - Buffer preservation across interruptions using nInitialChars
 */

int rb_w32_console_init(void);
void rb_w32_console_cleanup(void);

/* Returns the read end of the named pipe used by the console reader thread.
 * Callers like rb_w32_read_internal use this handle with ReadFile to get
 * console input without blocking on console line editing. */
HANDLE rb_w32_console_get_pipe_read(void);

/* Check if data is available in console pipe (for select) */
int rb_w32_console_readable(void);

/* Waiter management for select/read */
void rb_w32_console_select_start(void);
void rb_w32_console_select_stop(void);
void rb_w32_console_read_start(void);
void rb_w32_console_read_stop(void);

/* Termios emulation API */
int rb_w32_console_tcgetattr(int fd, struct termios *t);
int rb_w32_console_tcsetattr(int fd, int optional_actions, const struct termios *t);
void rb_w32_console_cfmakeraw(struct termios *t);
int rb_w32_console_tcflush(int fd, int queue_selector);

#endif /* RUBY_WIN32_CONSOLE_H */