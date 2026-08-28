#include "internal/bignum.h"
#include "internal/numeric.h" /* for RB_INTEGER_TYPE_P, rb_num2ll */

static VALUE
big(VALUE x)
{
    /* A Fixnum can exceed C's `long'; decode at full width. */
    if (!RB_BIGNUM_TYPE_P(x) && RB_INTEGER_TYPE_P(x))
        return rb_int2big((intptr_t)rb_num2ll(x));
    if (RB_BIGNUM_TYPE_P(x))
        return x;
    rb_raise(rb_eTypeError, "can't convert %s to Bignum",
            rb_obj_classname(x));
}

static VALUE
divrem_normal(VALUE klass, VALUE x, VALUE y)
{
    return rb_big_norm(rb_big_divrem_normal(big(x), big(y)));
}

#if defined(HAVE_LIBGMP) && defined(HAVE_GMP_H)
static VALUE
divrem_gmp(VALUE klass, VALUE x, VALUE y)
{
    return rb_big_norm(rb_big_divrem_gmp(big(x), big(y)));
}
#else
#define divrem_gmp rb_f_notimplement
#endif

void
Init_div(VALUE klass)
{
    rb_define_singleton_method(klass, "big_divrem_normal", divrem_normal, 2);
    rb_define_singleton_method(klass, "big_divrem_gmp", divrem_gmp, 2);
}
