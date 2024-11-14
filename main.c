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
static void init_win32_locales(void);
#endif

int
main(int argc, char **argv)
{
#if defined(RUBY_DEBUG_ENV) || USE_RUBY_DEBUG_LOG
    ruby_set_debug_option(getenv("RUBY_DEBUG"));
#endif
#ifdef HAVE_LOCALE_H
    setlocale(LC_CTYPE, "");
#endif
#ifdef _WIN32
    init_win32_locales();
#endif

    ruby_sysinit(&argc, &argv);
    return rb_main(argc, argv);
}

#ifdef _WIN32
static void
init_win32_lc_messages(const char *lang)
{
    WCHAR *lang_list;
    char *pcodepage_mark;
    int lang_chars;

    if (lang[0] == 'C') {
        if (lang[1] == '\0' || lang[1] == '.') {
            lang = "en-US";
        }
    }
    pcodepage_mark = strchr(lang, '.');
    lang_chars = MultiByteToWideChar(CP_ACP, 0, lang, -1, NULL, 0);
    lang_list = _alloca((lang_chars + 6 + 1) * sizeof(WCHAR));
    if (pcodepage_mark) {
        lang_chars = pcodepage_mark - lang + 1;
    }
    MultiByteToWideChar(CP_ACP, 0, lang, -1, lang_list, lang_chars);
    lang_list[lang_chars - 1] = L'\0';
    lang_list[lang_chars] = L'\0';
    MultiByteToWideChar(CP_ACP, 0, "en-US", -1, lang_list + lang_chars, 6);
    lang_list[lang_chars + 6] = L'\0';
    if (IsValidLocaleName(lang_list)) {
        SetProcessPreferredUILanguages(MUI_LANGUAGE_NAME, lang_list, NULL);
    } else {
        const char *punder_line = lang;
        while ((punder_line = strchr(punder_line, '_'))) {
            lang_list[punder_line++ - lang] = L'-';
        }
        if (IsValidLocaleName(lang_list)) {
            SetProcessPreferredUILanguages(MUI_LANGUAGE_NAME, lang_list, NULL);
        }
    }
}

static void
init_win32_locales(void)
{
    char *lang = getenv("LANG");
    char *lc_all = getenv("RUBY_LC_ALL");
    char *lc_ctype = getenv("RUBY_LC_CTYPE");
    char *lc_messages = getenv("RUBY_LC_MESSAGES");
    char *msg;
    char *ctype;

    if (!lc_all)      lc_all      = getenv("LC_ALL");
    if (!lc_messages) lc_messages = getenv("LC_MESSAGES");
    if (!lc_ctype)    lc_ctype    = getenv("LC_CTYPE");
    msg = lc_all ? lc_all : (lc_messages ? lc_messages : lang);
    if (msg)
      init_win32_lc_messages(msg);
    ctype = lc_all ? lc_all : (lc_ctype ? lc_ctype : lang);
    if (ctype)
        setlocale(LC_CTYPE, ctype);
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
