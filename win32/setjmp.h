#ifndef RUBY_WIN32_SETJMP_H
#define RUBY_WIN32_SETJMP_H

#if defined(__MINGW32__) && defined(_WIN64) && defined(RUBY_JMP_BUF)
/* note: __builtin_setjmp is used unless RUBY_JMP_BUF */
#if __MINGW64_VERSION_MAJOR >= 11
/* mingw64 clang or gcc without __builtin_setjmp */
/* mingw-w64 v11.0.0 feature */
#define __USE_MINGW_SETJMP_NON_SEH
#define RUBY_SETJMP_NON_SEH 1
#endif
#endif

#include <setjmp.h>

#if defined(_MSC_VER)
/* msvc */
#if defined(_M_AMD64)
#define RUBY_SETJMP_REQUIRE_CLEANUP_SEH 1
#endif

#endif /* _MSC_VER */

#endif
