#ifndef RUBY_WIN32_SETJMP_H
#define RUBY_WIN32_SETJMP_H

#if defined(__MINGW32__) && defined(_WIN64) && defined(RUBY_JMP_BUF)
/* mingw64 clang or gcc without __builtin_setjmp */
#define __USE_MINGW_SETJMP_NON_SEH
#endif

#include <setjmp.h>

#if defined(_MSC_VER) && defined(_WIN64)
/* msvc */

/* Include it beforehand to avoid the side effects of #define. */
#include <intrin.h>

/* Declare setjmp with hidden arguments */
extern int __intrinsic_setjmp(jmp_buf, void *);

/* Avoid using SEH by passing NULL as the second argument
 * instead of the compiler-provided argument.
 */
#define rb_w32_setjmp(env) __intrinsic_setjmp((env), NULL)

#endif /* _MSC_VER */

#endif
