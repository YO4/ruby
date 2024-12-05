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
#if USE_SHARED_GC
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
#endif
#endif

int
main(int argc, char **argv)
{
#if defined(RUBY_DEBUG_ENV) || USE_RUBY_DEBUG_LOG
    ruby_set_debug_option(getenv("RUBY_DEBUG"));
#endif
#ifdef HAVE_LOCALE_H
    setlocale(LC_CTYPE, "");
#ifdef W32_NEED_LOCALE_TWEAK
    w32_tweak_locale();
#endif
#endif

    ruby_sysinit(&argc, &argv);
    return rb_main(argc, argv);
}

#ifdef W32_NEED_LOCALE_TWEAK
/* Windows Universal CRT has inconsistent language / codepage behavior.
 * If an user ui language setting differs to a system ui language,
 * setlocale(any, "") combines that user ui language with a system default
 * code page. This is different from CRTs prior to Universal CRT.
 * On the other hand, CRT holds time zone name as narrow char string if 
 * Universal CRT build is less than 10.0.19041.0. 
 * Time zone names are selected by an user ui language, CRT converts it to
 * narrow char using locale code page.
 * This may cause destruction of time zone names.
 * It seems that a call to setlocale() without a code page will select
 * the default code page for the specified locale.
 * This seems to work for CRTs prior to universal CRT. */
static void
w32_tweak_locale(void)
{
    char *original_lc_ctype = setlocale(LC_CTYPE, NULL);

    if (original_lc_ctype) {
        char *tweaked_lc_ctype = NULL;
        char *p;

        tweaked_lc_ctype = alloca(strlen(original_lc_ctype) + 1);
        for (p = tweaked_lc_ctype; *original_lc_ctype; original_lc_ctype++, p++) {
            if (*original_lc_ctype == '.') {
                *p = '\0';
                break;
            }
            *p = *original_lc_ctype;
        }
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
