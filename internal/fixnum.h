#ifndef INTERNAL_FIXNUM_H                                /*-*-C-*-vi:se ft=c:*/
#define INTERNAL_FIXNUM_H
/**
 * @author     Ruby developers <ruby-core@ruby-lang.org>
 * @copyright  This  file  is   a  part  of  the   programming  language  Ruby.
 *             Permission  is hereby  granted,  to  either redistribute  and/or
 *             modify this file, provided that  the conditions mentioned in the
 *             file COPYING are met.  Consult the file for details.
 * @brief      Internal header for Fixnums.
 */
#include "ruby/internal/config.h"      /* for HAVE_LONG_LONG */
#include <limits.h>             /* for CHAR_BIT */
#include "internal/bits.h"      /* for MUL_OVERFLOW_WFIXNUM_P */
#include "internal/compilers.h" /* for __has_builtin */
#include "ruby/internal/stdbool.h"     /* for bool */
#include "ruby/intern.h"        /* for rb_big_mul */
#include "ruby/ruby.h"          /* for RB_FIXABLE */

#if HAVE_LONG_LONG && SIZEOF_VALUE * 2 <= SIZEOF_LONG_LONG
# define DLONG LONG_LONG
# define DL2NUM(x) LL2NUM(x)
#elif defined(HAVE_INT128_T) && !(defined(__OpenBSD__) && defined(__mips64__))
# define DLONG int128_t
# define DL2NUM(x) (RBIMPL_FIXABLE(x) ? RBIMPL_FIXNUM_FROM_VALUE((SIGNED_VALUE)(x)) : rb_int128t2big(x))
VALUE rb_int128t2big(int128_t n); /* in bignum.c */
#endif

static inline VALUE rb_fix_plus_fix(VALUE x, VALUE y);
static inline VALUE rb_fix_minus_fix(VALUE x, VALUE y);
static inline VALUE rb_fix_mul_fix(VALUE x, VALUE y);
static inline void rb_fix_divmod_fix(VALUE x, VALUE y, VALUE *divp, VALUE *modp);
static inline VALUE rb_fix_div_fix(VALUE x, VALUE y);
static inline VALUE rb_fix_mod_fix(VALUE x, VALUE y);
static inline bool FIXNUM_POSITIVE_P(VALUE num);
static inline bool FIXNUM_NEGATIVE_P(VALUE num);
static inline bool FIXNUM_ZERO_P(VALUE num);

static inline VALUE
rb_fix_plus_fix(VALUE x, VALUE y)
{
#if !__has_builtin(__builtin_add_overflow)
    SIGNED_VALUE lx = RBIMPL_FIXNUM_VALUE(x);
    SIGNED_VALUE ly = RBIMPL_FIXNUM_VALUE(y);
    if (ADD_OVERFLOW_SIGNED_INTEGER_P(lx, ly, RBIMPL_FIXNUM_MIN, RBIMPL_FIXNUM_MAX)) {
        return rb_big_plus(rb_int2big(lx), rb_int2big(ly));
    }
    return RBIMPL_FIXNUM_FROM_VALUE(lx + ly);
#else
    SIGNED_VALUE lx = RBIMPL_FIXNUM_VALUE(x);
    SIGNED_VALUE ly = RBIMPL_FIXNUM_VALUE(y);
    SIGNED_VALUE lz;
    if (__builtin_add_overflow(lx, ly, &lz) || !RBIMPL_FIXABLE(lz)) {
        return rb_big_plus(rb_int2big(lx), rb_int2big(ly));
    }
    return RBIMPL_FIXNUM_FROM_VALUE(lz);
#endif
}

static inline VALUE
rb_fix_minus_fix(VALUE x, VALUE y)
{
#if !__has_builtin(__builtin_sub_overflow)
    SIGNED_VALUE lx = RBIMPL_FIXNUM_VALUE(x);
    SIGNED_VALUE ly = RBIMPL_FIXNUM_VALUE(y);
    if (SUB_OVERFLOW_SIGNED_INTEGER_P(lx, ly, RBIMPL_FIXNUM_MIN, RBIMPL_FIXNUM_MAX)) {
        return rb_big_minus(rb_int2big(lx), rb_int2big(ly));
    }
    return RBIMPL_FIXNUM_FROM_VALUE(lx - ly);
#else
    SIGNED_VALUE lx = RBIMPL_FIXNUM_VALUE(x);
    SIGNED_VALUE ly = RBIMPL_FIXNUM_VALUE(y);
    SIGNED_VALUE lz;
    if (__builtin_sub_overflow(lx, ly, &lz) || !RBIMPL_FIXABLE(lz)) {
        return rb_big_minus(rb_int2big(lx), rb_int2big(ly));
    }
    return RBIMPL_FIXNUM_FROM_VALUE(lz);
#endif
}

/* arguments must be Fixnum */
static inline VALUE
rb_fix_mul_fix(VALUE x, VALUE y)
{
    SIGNED_VALUE lx = RBIMPL_FIXNUM_VALUE(x);
    SIGNED_VALUE ly = RBIMPL_FIXNUM_VALUE(y);
#ifdef DLONG
    return DL2NUM((DLONG)lx * (DLONG)ly);
#else
    if (MUL_OVERFLOW_WFIXNUM_P(lx, ly)) {
        return rb_big_mul(rb_int2big(lx), rb_int2big(ly));
    }
    else {
        return RBIMPL_FIXNUM_FROM_VALUE(lx * ly);
    }
#endif
}

/*
 * This behaves different from C99 for negative arguments.
 * Note that div may overflow fixnum.
 */
static inline void
rb_fix_divmod_fix(VALUE a, VALUE b, VALUE *divp, VALUE *modp)
{
    /* assume / and % comply C99.
     * ldiv(3) won't be inlined by GCC and clang.
     * I expect / and % are compiled as single idiv.
     */
    SIGNED_VALUE x = RBIMPL_FIXNUM_VALUE(a);
    SIGNED_VALUE y = RBIMPL_FIXNUM_VALUE(b);
    SIGNED_VALUE div, mod;
    if (x == RBIMPL_FIXNUM_MIN && y == -1) {
        if (divp) *divp = rb_int2big(-x);
        if (modp) *modp = RBIMPL_FIXNUM_FROM_VALUE(0);
        return;
    }
    div = x / y;
    mod = x % y;
    if (y > 0 ? mod < 0 : mod > 0) {
        mod += y;
        div -= 1;
    }
    if (divp) *divp = RBIMPL_FIXNUM_FROM_VALUE(div);
    if (modp) *modp = RBIMPL_FIXNUM_FROM_VALUE(mod);
}

/* div() for Ruby
 * This behaves different from C99 for negative arguments.
 */
static inline VALUE
rb_fix_div_fix(VALUE x, VALUE y)
{
    VALUE div;
    rb_fix_divmod_fix(x, y, &div, NULL);
    return div;
}

/* mod() for Ruby
 * This behaves different from C99 for negative arguments.
 */
static inline VALUE
rb_fix_mod_fix(VALUE x, VALUE y)
{
    VALUE mod;
    rb_fix_divmod_fix(x, y, NULL, &mod);
    return mod;
}

static inline bool
FIXNUM_POSITIVE_P(VALUE num)
{
    return RBIMPL_FIXNUM_VALUE(num) > 0;
}

static inline bool
FIXNUM_NEGATIVE_P(VALUE num)
{
    return RBIMPL_FIXNUM_VALUE(num) < 0;
}

static inline bool
FIXNUM_ZERO_P(VALUE num)
{
    return num == INT2FIX(0);
}
#endif /* INTERNAL_FIXNUM_H */
