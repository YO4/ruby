#ifndef RUBY_WIN32_SETJMP_H
#define RUBY_WIN32_SETJMP_H

#if defined(__MINGW32__) && defined(_WIN64) && defined(RUBY_JMP_BUF)
/* mingw64 clang or gcc without __builtin_setjmp */
#define __USE_MINGW_SETJMP_NON_SEH
#endif

#include <setjmp.h>

#if defined(_MSC_VER)
/* msvc */
#if defined(_M_AMD64)
#define rb_w32_setjmp_func __intrinsic_setjmp
#elif defined(_M_ARM64)
#define rb_w32_setjmp_func __intrinsic_setjmpex
#endif

#if defined(rb_w32_setjmp_func)
/* Include it beforehand to avoid the side effects of #define. */
#include <intrin.h>


/* Declare setjmp with hidden arguments */
extern int rb_w32_setjmp_func(jmp_buf, void *);

/* Avoid using SEH by passing NULL as the second argument
 * instead of the compiler-provided argument.
 */
#define rb_w32_setjmp(env) rb_w32_setjmp_func((env), NULL)

#endif /* rb_w32_setjmp_func */
#endif /* _MSC_VER */

#endif
