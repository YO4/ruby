/*
 * Console reader thread for Windows
 *
 * Ruby on Windows uses ReadFile for console input in rb_w32_read_internal.
 * In cooked mode (ENABLE_LINE_INPUT), ReadConsoleW/ReadFile blocks until
 * the user presses Enter. This is problematic for select(), which checks
 * is_readable_console() based on individual key events, so select() can
 * report readable while the following read() still blocks.
 *
 * This module solves the problem by introducing a dedicated reader thread
 * that owns the console input handle (CONIN$). The thread blocks in
 * ReadConsoleW on behalf of Ruby; when input is available it converts the
 * UTF-16 characters to UTF-8 and writes them into a named pipe.
 * Ruby's read/select operations then perform non-blocking I/O on the pipe,
 * so they never block on console line editing.
 *
 * The thread reads only while there are waiters (threads in select() or
 * blocking read()). When the last waiter leaves, the thread is interrupted
 * with a synthetic control character so it can save any partially edited
 * line for the next read. Reference counting makes the API safe for
 * concurrent select()/read() calls from multiple Ruby threads.
 */

#include "ruby/ruby.h"
#include "ruby/encoding.h"
#include "ruby/win32.h"
#include "win32/console.h"
#include <windows.h>
#include <io.h>
#include <wchar.h>
#include <errno.h>

/*
 * Console reader singleton.
 * There is at most one console per process, so a single instance is enough.
 */
typedef struct console_reader {
    /* Handle to CONIN$, independent of the fd held by Ruby */
    HANDLE conin_handle;
    DWORD original_mode;   /* Mode at initialization; restored on cleanup */
    DWORD current_mode;    /* Last mode set by apply_termios */

    /* Named pipe used to transport input from reader thread to callers */
    HANDLE pipe_read;
    HANDLE pipe_write;
    WCHAR pipe_name[64];

    /* Reader thread */
    HANDLE thread_handle;
    volatile BOOL running;
    HANDLE shutdown_event;

    /*
     * Synchronization:
     *   - waiters is an atomic counter of threads expecting input.
     *   - read_event is signaled when waiters transitions 0->1.
     *   - state_lock protects reading, interrupt_requested and the
     *     preserved input buffer.
     */
    volatile LONG waiters;
    HANDLE read_event;
    CRITICAL_SECTION state_lock;
    BOOL reading;
    BOOL interrupt_requested;

    /* Preserved partial input used with nInitialChars across interruptions */
    WCHAR *preserved_buffer;
    DWORD preserved_len;
    DWORD preserved_capacity;

    /* Settings for the synthetic interrupt character */
    WCHAR interrupt_char;           /* character code 24 (Ctrl+X) */
    DWORD interrupt_wakeup_mask;    /* (1 << 24) */
    DWORD interrupt_control_state;  /* 0x07: ALT+CTRL states not seen from keyboard */

    /* Emulated termios state */
    struct termios termios;
    BOOL is_raw_mode;

    /* UTF-16 -> UTF-8 conversion state (orphan high surrogate) */
    WCHAR pending_surrogate;
    BOOL has_pending_surrogate;
} console_reader_t;

/* Singleton instance */
static console_reader_t *g_console = NULL;

/*
 * Create the named pipe used between the reader thread and callers.
 * PIPE_TYPE_MESSAGE is used so that each ReadConsoleW completion
 * (typically one input line in cooked mode) becomes one message;
 * Ruby then reads complete logical units through the pipe.
 */
static int
create_console_pipe(console_reader_t *ctx)
{
    SECURITY_ATTRIBUTES sec = {
        sizeof(sec),
        NULL,
        FALSE
    };

    _snwprintf(ctx->pipe_name, sizeof(ctx->pipe_name),
               L"\\\\.\\pipe\\ruby_console_%lu", GetCurrentProcessId());

    /*
     * PIPE_NOWAIT is intentionally omitted. With PIPE_NOWAIT, ReadFile
     * returns ERROR_NO_DATA immediately even for overlapped I/O, which
     * short-circuits the OVERLAPPED wait path in rb_w32_read_internal
     * and forces every read to bounce through EWOULDBLOCK -> select().
     * Instead we rely on FILE_FLAG_OVERLAPPED alone: ReadFile returns
     * ERROR_IO_PENDING, then completes when the reader thread writes
     * to the pipe. This makes the read path truly event-driven and
     * matches the way rb_w32_read_internal already handles pipes.
     */
    ctx->pipe_read = CreateNamedPipeW(
        ctx->pipe_name,
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE,
        1,
        65536,
        65536,
        0,
        &sec);
    if (ctx->pipe_read == INVALID_HANDLE_VALUE) {
        return -1;
    }

    ctx->pipe_write = CreateFileW(
        ctx->pipe_name,
        GENERIC_WRITE,
        0,
        &sec,
        OPEN_EXISTING,
        0,
        NULL);
    if (ctx->pipe_write == INVALID_HANDLE_VALUE) {
        CloseHandle(ctx->pipe_read);
        ctx->pipe_read = INVALID_HANDLE_VALUE;
        return -1;
    }

    return 0;
}

/*
 * Convert UTF-16 data to UTF-8 and write it into the pipe.
 * Handles surrogate pairs across ReadConsoleW calls: a lone high surrogate
 * is buffered until the matching low surrogate arrives.
 */
static void
write_utf16_to_pipe(console_reader_t *ctx, const WCHAR *wbuf, DWORD wlen)
{
    char utf8_buf[4096];
    DWORD utf8_pos = 0;
    DWORD i;

    /* Ensure space for one worst-case UTF-8 character (4 bytes). */
#define UTF8_BUF_AVAIL (sizeof(utf8_buf) - utf8_pos)
#define FLUSH_UTF8() do { \
    if (utf8_pos > 0) { \
        DWORD written; \
        WriteFile(ctx->pipe_write, utf8_buf, utf8_pos, &written, NULL); \
        utf8_pos = 0; \
    } \
} while (0)

    for (i = 0; i < wlen; i++) {
        WCHAR wc = wbuf[i];

        if (wc >= 0xD800 && wc <= 0xDBFF) {
            /* High surrogate: flush first, then buffer until low arrives */
            FLUSH_UTF8();
            ctx->pending_surrogate = wc;
            ctx->has_pending_surrogate = TRUE;
            continue;
        }

        if (wc >= 0xDC00 && wc <= 0xDFFF) {
            if (ctx->has_pending_surrogate) {
                WCHAR pair[2] = { ctx->pending_surrogate, wc };
                if (UTF8_BUF_AVAIL < 4) FLUSH_UTF8();
                int converted = WideCharToMultiByte(CP_UTF8, 0, pair, 2,
                    utf8_buf + utf8_pos, (int)UTF8_BUF_AVAIL,
                    NULL, NULL);
                utf8_pos += converted > 0 ? (DWORD)converted : 0;
                ctx->has_pending_surrogate = FALSE;
            } else {
                /* Orphan low surrogate: replace with U+FFFD */
                WCHAR replacement = 0xFFFD;
                if (UTF8_BUF_AVAIL < 3) FLUSH_UTF8();
                int converted = WideCharToMultiByte(CP_UTF8, 0, &replacement, 1,
                    utf8_buf + utf8_pos, (int)UTF8_BUF_AVAIL,
                    NULL, NULL);
                utf8_pos += converted > 0 ? (DWORD)converted : 0;
            }
            continue;
        }

        /* Regular BMP character; emit any orphan high surrogate first */
        if (ctx->has_pending_surrogate) {
            WCHAR replacement = 0xFFFD;
            if (UTF8_BUF_AVAIL < 3) FLUSH_UTF8();
            int converted = WideCharToMultiByte(CP_UTF8, 0, &replacement, 1,
                utf8_buf + utf8_pos, (int)UTF8_BUF_AVAIL,
                NULL, NULL);
            utf8_pos += converted > 0 ? (DWORD)converted : 0;
            ctx->has_pending_surrogate = FALSE;
        }

        if (UTF8_BUF_AVAIL < 4) FLUSH_UTF8();
        {
            int converted = WideCharToMultiByte(CP_UTF8, 0, &wc, 1,
                utf8_buf + utf8_pos, (int)UTF8_BUF_AVAIL,
                NULL, NULL);
            utf8_pos += converted > 0 ? (DWORD)converted : 0;
        }
    }

    FLUSH_UTF8();

#undef UTF8_BUF_AVAIL
#undef FLUSH_UTF8
}

/*
 * Map termios flags to Win32 console modes.
 *
 * ICANON corresponds to ENABLE_LINE_INPUT (cooked line editing).
 * ECHO maps to ENABLE_ECHO_INPUT.
 * ISIG maps to ENABLE_PROCESSED_INPUT so that Ctrl+C is processed.
 * IGNBRK/BRKINT are intentionally ignored per design.
 */
static void
apply_termios(console_reader_t *ctx, const struct termios *t)
{
    DWORD mode = ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT;

    if (t->c_lflag & ICANON) {
        mode |= ENABLE_LINE_INPUT;
    }
    if (t->c_lflag & ECHO) {
        mode |= ENABLE_ECHO_INPUT;
    }
    if (t->c_lflag & ISIG) {
        mode |= ENABLE_PROCESSED_INPUT;
    }

    EnterCriticalSection(&ctx->state_lock);
    ctx->termios = *t;
    ctx->is_raw_mode = !(t->c_lflag & ICANON);
    ctx->current_mode = mode;
    LeaveCriticalSection(&ctx->state_lock);

    SetConsoleMode(ctx->conin_handle, mode);
}

/*
 * Initialize default termios state that matches the original console mode.
 */
static void
init_termios(console_reader_t *ctx)
{
    struct termios t;
    memset(&t, 0, sizeof(t));

    t.c_iflag = ICRNL | IXON;
    t.c_oflag = OPOST | ONLCR;
    t.c_cflag = CREAD | CS8;
    //
    // Derive the termios c_lflag bits from the actual console mode
    // at initialization time, rather than assuming cooked mode.
    //
    t.c_lflag = IEXTEN;
    if (ctx->original_mode & ENABLE_LINE_INPUT) t.c_lflag |= ICANON | ECHOE | ECHOK;
    if (ctx->original_mode & ENABLE_ECHO_INPUT) t.c_lflag |= ECHO;
    if (ctx->original_mode & ENABLE_PROCESSED_INPUT) t.c_lflag |= ISIG;
    t.c_ispeed = B9600;
    t.c_ospeed = B9600;

    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;

    ctx->termios = t;
    ctx->is_raw_mode = !(t.c_lflag & ICANON);
}

/*
 * Interrupt the current ReadConsoleW call by injecting a synthetic
 * control character. The character is distinguished from real keyboard
 * input by a control key state (0x07) that never occurs naturally.
 *
 * Skips the injection if the reader thread is not currently inside
 * ReadConsoleW (ctx->reading == FALSE). In that case there is no
 * pending read to interrupt; the previous ReadConsoleW has already
 * returned and the thread is preparing to wait for the next waiter.
 *
 * The state_lock is held across the WriteConsoleInputW and the
 * interrupt_requested flag update so that the reader thread, if it
 * observes interrupt_requested == TRUE, can rely on the corresponding
 * INPUT_RECORD already being in the console input buffer.
 */
static void
inject_interrupt_key(console_reader_t *ctx)
{
    INPUT_RECORD ir;
    DWORD written;
    BOOL ok;
    BOOL was_reading;

    memset(&ir, 0, sizeof(ir));
    ir.EventType = KEY_EVENT;
    ir.Event.KeyEvent.bKeyDown = TRUE;
    ir.Event.KeyEvent.wRepeatCount = 1;
    ir.Event.KeyEvent.wVirtualKeyCode = (WORD)ctx->interrupt_char;
    ir.Event.KeyEvent.uChar.UnicodeChar = ctx->interrupt_char;
    ir.Event.KeyEvent.dwControlKeyState = ctx->interrupt_control_state;

    EnterCriticalSection(&ctx->state_lock);
    was_reading = ctx->reading;
    if (was_reading) {
        ok = WriteConsoleInputW(ctx->conin_handle, &ir, 1, &written);
        if (ok && written == 1) {
            ctx->interrupt_requested = TRUE;
        }
    } else {
        ok = TRUE;
        written = 1;
    }
    LeaveCriticalSection(&ctx->state_lock);

    if (was_reading && (!ok || written != 1)) {
        /* WriteConsoleInputW failed; the interrupt could not be delivered. */
    }
}

/*
 * Reader thread entry point. It waits until at least one thread is
 * expecting input, then performs a blocking ReadConsoleW. When input
 * is available it writes to the pipe; when the last waiter leaves it
 * saves the partial buffer and goes back to sleep.
 */
static DWORD WINAPI
console_thread(LPVOID arg)
{
    console_reader_t *ctx = arg;
    WCHAR buffer[4096];
    CONSOLE_READCONSOLE_CONTROL control;
    HANDLE wait_events[2];

    /*
     * Watch both the read_event (a new waiter has appeared) and the
     * shutdown_event (the thread is being torn down). Either event
     * wakes us; the manual-reset shutdown_event remains set so a
     * subsequent WaitForMultipleObjects will return immediately.
     */
    wait_events[0] = ctx->read_event;
    wait_events[1] = ctx->shutdown_event;

    while (ctx->running) {
        DWORD wait_result;
        DWORD chars_read = 0;
        BOOL success;

        /* Wait for either new input demand or shutdown */
        wait_result = WaitForMultipleObjects(2, wait_events, FALSE, INFINITE);

        if (wait_result == WAIT_OBJECT_0 + 1) {
            break;
        }
        if (wait_result != WAIT_OBJECT_0) {
            continue;
        }
        if (!ctx->running) {
            break;
        }

        EnterCriticalSection(&ctx->state_lock);
        if (ctx->waiters == 0) {
            /* A stop request arrived before we could check */
            ctx->reading = FALSE;
            LeaveCriticalSection(&ctx->state_lock);
            continue;
        }
        ctx->reading = TRUE;
        LeaveCriticalSection(&ctx->state_lock);

        /* Initialize CONSOLE_READCONSOLE_CONTROL for this iteration. */
        memset(&control, 0, sizeof(control));
        control.dwCtrlWakeupMask = ctx->interrupt_wakeup_mask;

        /* Resume from previously preserved partial input if any */
        control.nInitialChars = ctx->preserved_len;
        if (ctx->preserved_len > 0) {
            memcpy(buffer, ctx->preserved_buffer,
                   ctx->preserved_len * sizeof(WCHAR));
        }

        /*
         * Block until:
         *   - A line is completed (cooked mode)
         *   - A key is pressed (raw mode)
         *   - The synthetic interrupt character is injected
         */
        success = ReadConsoleW(ctx->conin_handle, buffer,
                               sizeof(buffer) / sizeof(WCHAR), &chars_read, &control);

        if (success && chars_read > 0) {
            /*
             * Distinguish the injected interrupt key from genuine input
             * by checking the control key state. The injected key uses
             * 0x07 (RIGHT_ALT|LEFT_ALT|RIGHT_CTRL) which never occurs
             * from real keyboard input, so this is unambiguous.
             *
             * If the injection happened while we were inside ReadConsoleW
             * (inject_interrupt_key saw ctx->reading == TRUE and wrote
             * the INPUT_RECORD), the returned dwControlKeyState will
             * match interrupt_control_state. If the read completed
             * normally, dwControlKeyState is the control state of the
             * actual user keypress.
             */
            BOOL injected_key_read =
                (control.dwControlKeyState == ctx->interrupt_control_state);

            if (injected_key_read) {
                EnterCriticalSection(&ctx->state_lock);
                ctx->interrupt_requested = FALSE;
                /*
                 * Preserve any characters that preceded the interrupt
                 * key as the initial buffer for the next ReadConsoleW.
                 * The interrupt key itself is discarded.
                 */
                if (chars_read > 1) {
                    DWORD effective_len = chars_read - 1;
                    if (effective_len > ctx->preserved_capacity) {
                        WCHAR *newbuf = realloc(ctx->preserved_buffer,
                            effective_len * sizeof(WCHAR));
                        if (newbuf) {
                            ctx->preserved_buffer = newbuf;
                            ctx->preserved_capacity = effective_len;
                        } else {
                            effective_len = 0;
                        }
                    }
                    if (effective_len > 0) {
                        memcpy(ctx->preserved_buffer, buffer,
                               effective_len * sizeof(WCHAR));
                    }
                    ctx->preserved_len = effective_len;
                } else {
                    ctx->preserved_len = 0;
                }
                ctx->reading = FALSE;
                LeaveCriticalSection(&ctx->state_lock);
            } else {
                /* Normal completion: write UTF-8 to the pipe */
                write_utf16_to_pipe(ctx, buffer, chars_read);

                EnterCriticalSection(&ctx->state_lock);
                ctx->preserved_len = 0;
                if (ctx->interrupt_requested == FALSE) {
                    ctx->reading = FALSE;
                } else {
                    ctx->interrupt_requested = FALSE;
                    /* There is pending interrupt charanter to read next reading */
                }
                LeaveCriticalSection(&ctx->state_lock);
            }
        } else {
            EnterCriticalSection(&ctx->state_lock);
            ctx->reading = FALSE;
            LeaveCriticalSection(&ctx->state_lock);
        }
    }

    return 0;
}

/*
 * Free the singleton instance. Called from rb_w32_console_cleanup and after
 * failed initialization.
 */
static void
free_console_reader(console_reader_t *ctx)
{
    if (!ctx) return;

    if (ctx->thread_handle) {
        ctx->running = FALSE;
        SetEvent(ctx->shutdown_event);
        SetEvent(ctx->read_event);

        inject_interrupt_key(ctx);

        /*
         * Wait for the reader thread to exit. If there is some reason
         * (e.g., a kernel-mode lock) that prevents the thread from
         * shutting down on time, proceed anyway; the process is about
         * to terminate and the OS will release resources.
         */
        DWORD wait_result = WaitForSingleObject(ctx->thread_handle, 5000);
        if (wait_result != WAIT_OBJECT_0) {
            fprintf(stderr, "ruby: warning: console reader thread did not"
                    " exit gracefully\n");
        }
        CloseHandle(ctx->thread_handle);
    }

    if (ctx->conin_handle != INVALID_HANDLE_VALUE) {
        /* Restore original console mode */
        if (ctx->original_mode != (DWORD)-1) {
            SetConsoleMode(ctx->conin_handle, ctx->original_mode);
        }
        CloseHandle(ctx->conin_handle);
    }

    if (ctx->pipe_read != INVALID_HANDLE_VALUE) CloseHandle(ctx->pipe_read);
    if (ctx->pipe_write != INVALID_HANDLE_VALUE) CloseHandle(ctx->pipe_write);
    if (ctx->read_event) CloseHandle(ctx->read_event);
    if (ctx->shutdown_event) CloseHandle(ctx->shutdown_event);

    DeleteCriticalSection(&ctx->state_lock);

    if (ctx->preserved_buffer) free(ctx->preserved_buffer);

    free(ctx);
}

/*
 * Public API: initialize the console reader.
 * Open CONIN$, create the pipe, and start the reader thread.
 */
int
rb_w32_console_init(void)
{
    console_reader_t *ctx;
    DWORD tid;

    if (g_console) return 0;

    ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        errno = ENOMEM;
        return -1;
    }

    ctx->conin_handle = INVALID_HANDLE_VALUE;
    ctx->pipe_read = INVALID_HANDLE_VALUE;
    ctx->pipe_write = INVALID_HANDLE_VALUE;
    ctx->original_mode = (DWORD)-1;
    ctx->interrupt_char = (WCHAR)0x18;
    ctx->interrupt_wakeup_mask = (1 << ctx->interrupt_char);
    ctx->interrupt_control_state = 0x07;  /* RIGHT_ALT|LEFT_ALT|RIGHT_CTRL */

    InitializeCriticalSection(&ctx->state_lock);

    /* Open a private handle to the console input device */
    ctx->conin_handle = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
        OPEN_EXISTING, 0, NULL);
    if (ctx->conin_handle == INVALID_HANDLE_VALUE) {
        goto fail;
    }

    if (!GetConsoleMode(ctx->conin_handle, &ctx->original_mode)) {
        goto fail;
    }
    ctx->current_mode = ctx->original_mode;

    if (create_console_pipe(ctx) < 0) goto fail;

    ctx->shutdown_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!ctx->shutdown_event) goto fail;

    ctx->read_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!ctx->read_event) goto fail;

    init_termios(ctx);

    ctx->running = TRUE;
    ctx->thread_handle = CreateThread(NULL, 0, console_thread, ctx, 0, &tid);
    if (!ctx->thread_handle) {
        ctx->running = FALSE;
        goto fail;
    }

    g_console = ctx;
    return 0;

  fail:
    free_console_reader(ctx);
    errno = EINVAL;
    return -1;
}

/*
 * Public API: cleanup the console reader.
 * Called from exit_handler and vm_exit_handler.
 */
void
rb_w32_console_cleanup(void)
{
    console_reader_t *ctx = g_console;
    if (!ctx) return;
    g_console = NULL;
    free_console_reader(ctx);
}

/*
 * Public API: return the read handle of the named pipe used by the
 * console reader thread. rb_w32_read_internal uses this handle directly
 * with ReadFile so that the existing overlapped-I/O and PIPE_NOWAIT
 * handling (EINTR, blocking wait, retry) applies to console reads.
 */
HANDLE
rb_w32_console_get_pipe_read(void)
{
    return g_console ? g_console->pipe_read : INVALID_HANDLE_VALUE;
}

/*
 * Public API: non-blocking check for console input availability.
 * Used by is_readable_console() during select() polling; fd-independent.
 */
int
rb_w32_console_readable(void)
{
    console_reader_t *ctx = g_console;
    DWORD available = 0;

    if (!ctx) return 0;

    if (PeekNamedPipe(ctx->pipe_read, NULL, 0, NULL, &available, NULL)) {
        return available > 0 ? 1 : 0;
    }
    return 0;
}

/*
 * Increase the number of threads expecting console input.
 * If this is the first waiter, wake the reader thread.
 */
static void
console_waiter_add(console_reader_t *ctx)
{
    LONG waiters = InterlockedIncrement(&ctx->waiters);
    if (waiters == 1) {
        /* Transition 0->1: the reader may be idle, wake it */
        SetEvent(ctx->read_event);
    }
}

/*
 * Decrease the number of threads expecting console input.
 * If this was the last waiter, interrupt the current ReadConsoleW.
 */
static void
console_waiter_remove(console_reader_t *ctx)
{
    LONG waiters = InterlockedDecrement(&ctx->waiters);
    if (waiters == 0) {
        /* No one is waiting anymore; stop the reader */
        EnterCriticalSection(&ctx->state_lock);
        if (ctx->reading) {
            inject_interrupt_key(ctx);
        }
        LeaveCriticalSection(&ctx->state_lock);
    }
}

void
rb_w32_console_select_start(void)
{
    if (g_console) console_waiter_add(g_console);
}

void
rb_w32_console_select_stop(void)
{
    if (g_console) console_waiter_remove(g_console);
}

void
rb_w32_console_read_start(void)
{
    if (g_console) console_waiter_add(g_console);
}

void
rb_w32_console_read_stop(void)
{
    if (g_console) console_waiter_remove(g_console);
}

/*
 * Termios emulation API.
 * These functions are declared publicly via #define mappings in
 * include/ruby/win32.h, allowing code like ext/io/console/console.c
 * to use the standard POSIX names while forwarding to our Win32
 * implementation.
 */

int
rb_w32_console_tcgetattr(int fd, struct termios *t)
{
    console_reader_t *ctx = g_console;

    if (!ctx || !rb_w32_isatty(fd)) {
        errno = ENOTTY;
        return -1;
    }

    EnterCriticalSection(&ctx->state_lock);
    *t = ctx->termios;
    LeaveCriticalSection(&ctx->state_lock);
    return 0;
}

int
rb_w32_console_tcsetattr(int fd, int optional_actions, const struct termios *t)
{
    console_reader_t *ctx = g_console;

    if (!ctx || !rb_w32_isatty(fd)) {
        errno = ENOTTY;
        return -1;
    }

    (void)optional_actions;  /* TCSANOW/TCSADRAIN/TCSAFLUSH are equivalent here */
    apply_termios(ctx, t);
    return 0;
}

void
rb_w32_console_cfmakeraw(struct termios *t)
{
    t->c_iflag &= ~(IGNBRK | BRKINT | IGNPAR | PARMRK | INPCK |
                    ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXOFF |
                    IXANY | IMAXBEL);
    t->c_oflag &= ~OPOST;
    t->c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL | ICANON | ISIG |
                    IEXTEN /* | XCASE */);
    t->c_cflag &= ~(CSIZE | PARENB);
    t->c_cflag |= CS8;
    t->c_cc[VMIN] = 1;
    t->c_cc[VTIME] = 0;
}

int
rb_w32_console_tcflush(int fd, int queue_selector)
{
    console_reader_t *ctx = g_console;

    if (!ctx || !rb_w32_isatty(fd)) {
        errno = ENOTTY;
        return -1;
    }

    if (queue_selector == TCIFLUSH || queue_selector == TCIOFLUSH) {
        /* Flush the pipe's input side */
        char buf[1024];
        DWORD read;

        while (PeekNamedPipe(ctx->pipe_read, NULL, 0, NULL, &read, NULL) &&
               read > 0) {
            ReadFile(ctx->pipe_read, buf, sizeof(buf), &read, NULL);
        }

        /* Also flush any pending console input records */
        FlushConsoleInputBuffer(ctx->conin_handle);

        /* Discard our preserved partial input */
        EnterCriticalSection(&ctx->state_lock);
        ctx->preserved_len = 0;
        LeaveCriticalSection(&ctx->state_lock);
    }

    /* Output flush is a no-op for console input */

    return 0;
}

/*
 * Speed-related functions are stubs because Windows console has no
 * serial-line speed concept. They are provided for API compatibility.
 */
speed_t
rb_w32_console_cfgetispeed(const struct termios *t)
{
    return t->c_ispeed;
}

speed_t
rb_w32_console_cfgetospeed(const struct termios *t)
{
    return t->c_ospeed;
}

int
rb_w32_console_cfsetispeed(struct termios *t, speed_t s)
{
    t->c_ispeed = s;
    return 0;
}

int
rb_w32_console_cfsetospeed(struct termios *t, speed_t s)
{
    t->c_ospeed = s;
    return 0;
}

int
rb_w32_console_tcdrain(int fd)
{
    (void)fd;
    return 0;
}

int
rb_w32_console_tcflow(int fd, int action)
{
    (void)fd;
    (void)action;
    return 0;
}

int
rb_w32_console_tcsendbreak(int fd, int duration)
{
    (void)fd;
    (void)duration;
    return 0;
}

int
rb_w32_console_cfsetspeed(struct termios *t, speed_t s)
{
    t->c_ispeed = s;
    t->c_ospeed = s;
    return 0;
}
