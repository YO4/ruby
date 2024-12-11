/**********************************************************************

  main.c -

  $Author$
  created at: Fri Aug 19 13:19:58 JST 1994

  Copyright (C) 1993-2007 Yukihiro Matsumoto

**********************************************************************/

/*!
 * \mainpage Developers' documentation for Ruby
 *
 * This documentation is produced by applying Doxygen to
 * <a href="https://github.com/ruby/ruby">Ruby's source code</a>.
 * It is still under construction (and even not well-maintained).
 * If you are familiar with Ruby's source code, please improve the doc.
 */
#undef RUBY_EXPORT
#include "ruby.h"
#include "vm_debug.h"
#include "internal/sanitizers.h"
#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif
#if USE_MODULAR_GC
#include "internal/gc.h"
#endif

#if defined RUBY_DEVEL && !defined RUBY_DEBUG_ENV
# define RUBY_DEBUG_ENV 1
#endif
#if defined RUBY_DEBUG_ENV && !RUBY_DEBUG_ENV
# undef RUBY_DEBUG_ENV
#endif

static int
rb_main(int argc, char **argv)
{
    RUBY_INIT_STACK;
    ruby_init();
    return ruby_run_node(ruby_options(argc, argv));
}

#if defined(__wasm__) && !defined(__EMSCRIPTEN__)
int rb_wasm_rt_start(int (main)(int argc, char **argv), int argc, char **argv);
#define rb_main(argc, argv) rb_wasm_rt_start(rb_main, argc, argv)
#endif

#ifdef _WIN32
#define main(argc, argv) w32_main(argc, argv)
static int main(int argc, char **argv);
int wmain(void) {return main(0, NULL);}
#if defined(RUBY_MSVCRT_VERSION) && RUBY_MSVCRT_VERSION >= 140
#define W32_NEED_LOCALE_TWEAK 1
static void w32_tweak_locale(void);
#define W32_LOCALE_TWEAK() w32_tweak_locale()
#endif
#endif
#ifndef W32_LOCALE_TWEAK
#define W32_LOCALE_TWEAK() (0)
#endif

int
main(int argc, char **argv)
{
#if defined(RUBY_DEBUG_ENV) || USE_RUBY_DEBUG_LOG
    ruby_set_debug_option(getenv("RUBY_DEBUG"));
#endif
#ifdef HAVE_LOCALE_H
    setlocale(LC_CTYPE, "");
    W32_LOCALE_TWEAK();
#endif

    ruby_sysinit(&argc, &argv);
    return rb_main(argc, argv);
}

#ifdef W32_NEED_LOCALE_TWEAK
static void
w32_tweak_locale(void)
{
    char *original_lc_ctype = setlocale(LC_CTYPE, NULL);

    if (original_lc_ctype && strchr(original_lc_ctype, '.')) {
        char *p, *q;
        char *tweaked_lc_ctype;

        p = original_lc_ctype;
        q = tweaked_lc_ctype = alloca(strlen(original_lc_ctype) + 1);
        while (*p != '.') {
            *q++ = *p++;
        }
        /* return if locale codepage == UTF-8 */
        if (strlen(p) >= 5) {
            if (TOLOWER(p[1]) == 'u' &&
                TOLOWER(p[2]) == 't' &&
                TOLOWER(p[3]) == 'f' &&
                ((p[4] == '8' && p[5] == '\0') ||
                 (p[4] == '-' && p[5] == '8' && p[6] == '\0'))) {
                return;
            }
        }
        *q = '\0';
        /* Set locale codepage to locale language's default ANSI codepage.
         * This avoids different language-codepage combination
         * like "Japanese_Japan.1252". Setting Default User Interface Language
         * to different from Windows System Default Language causes this. */
        setlocale(LC_CTYPE, tweaked_lc_ctype);
    }
}
#endif

#ifdef RUBY_ASAN_ENABLED
/* Compile in the ASAN options Ruby needs, rather than relying on environment variables, so
 * that even tests which fork ruby with a clean environment will run ASAN with the right
 * settings */
const char *
__asan_default_options(void)
{
    return "use_sigaltstack=0:detect_leaks=0";
}
#endif
