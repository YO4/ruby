#ifndef RBIMPL_ARITHMETIC_FIXNUM_H                   /*-*-C++-*-vi:se ft=cpp:*/
#define RBIMPL_ARITHMETIC_FIXNUM_H
/**
 * @file
 * @author     Ruby developers <ruby-core@ruby-lang.org>
 * @copyright  This  file  is   a  part  of  the   programming  language  Ruby.
 *             Permission  is hereby  granted,  to  either redistribute  and/or
 *             modify this file, provided that  the conditions mentioned in the
 *             file COPYING are met.  Consult the file for details.
 * @warning    Symbols   prefixed  with   either  `RBIMPL`   or  `rbimpl`   are
 *             implementation details.   Don't take  them as canon.  They could
 *             rapidly appear then vanish.  The name (path) of this header file
 *             is also an  implementation detail.  Do not expect  it to persist
 *             at the place it is now.  Developers are free to move it anywhere
 *             anytime at will.
 * @note       To  ruby-core:  remember  that   this  header  can  be  possibly
 *             recursively included  from extension  libraries written  in C++.
 *             Do not  expect for  instance `__VA_ARGS__` is  always available.
 *             We assume C99  for ruby itself but we don't  assume languages of
 *             extension libraries.  They could be written in C++98.
 * @brief      Handling of integers formerly known as Fixnums.
 */
#include "ruby/backward/2/limits.h"
#include "ruby/internal/assume.h"
#include "ruby/internal/special_consts.h"
#include "ruby/internal/value.h"

#define FIXABLE    RBIMPL_LEGACY_FIXABLE /**< @old{RB_FIXABLE} */
#define FIXNUM_MAX RUBY_FIXNUM_MAX /**< @old{RUBY_FIXNUM_MAX} */
#define FIXNUM_MIN RUBY_FIXNUM_MIN /**< @old{RUBY_FIXNUM_MIN} */
#define NEGFIXABLE RBIMPL_LEGACY_NEGFIXABLE /**< @old{RB_NEGFIXABLE} */
#define POSFIXABLE RBIMPL_LEGACY_POSFIXABLE /**< @old{RB_POSFIXABLE} */

/**
 * Checks if the passed value is in  range of fixnum, assuming it is a positive
 * number.  Can sometimes be useful for C's unsigned integer types.
 *
 * @internal
 *
 * FIXABLE can be applied to anything, from double to intmax_t.  The problem is
 * double.   On a  64bit system  RUBY_FIXNUM_MAX is  4,611,686,018,427,387,903,
 * which is not representable by a double.  The nearest value that a double can
 * represent  is   4,611,686,018,427,387,904,  which   is  not   fixable.   The
 * seemingly-strange "< FIXNUM_MAX + 1" expression below is due to this.
 */
#define RB_POSFIXABLE(_) ((_) <  RUBY_FIXNUM_MAX + 1)

/**
 * Checks if the passed value is in  range of fixnum, assuming it is a negative
 * number.  This is an implementation of #RB_FIXABLE.  Rarely used stand alone.
 */
#define RB_NEGFIXABLE(_) ((_) >= RUBY_FIXNUM_MIN)

/** Checks if the passed value is in  range of fixnum */
#define RB_FIXABLE(_)    (RB_POSFIXABLE(_) && RB_NEGFIXABLE(_))

/** Maximum possible value that a fixnum can represent. */
#define RUBY_FIXNUM_MAX  (LONG_MAX / 2)

/** Minimum possible value that a fixnum can represent. */
#define RUBY_FIXNUM_MIN  (LONG_MIN / 2)

/*
 * The public Fixnum range is deliberately based on long.  It is part of the
 * old extension API, while the interpreter can use every payload bit in a
 * VALUE.  Keep the latter range private to the interpreter.
 */
#define RBIMPL_FIXNUM_MAX ((SIGNED_VALUE)(RBIMPL_VALUE_FULL >> 2))
#define RBIMPL_FIXNUM_MIN (-RBIMPL_FIXNUM_MAX - 1)

#define RBIMPL_POSFIXABLE(_) ((_) < RBIMPL_FIXNUM_MAX + 1)
#define RBIMPL_NEGFIXABLE(_) ((_) >= RBIMPL_FIXNUM_MIN)
#define RBIMPL_FIXABLE(_)    (RBIMPL_POSFIXABLE(_) && RBIMPL_NEGFIXABLE(_))

#define RBIMPL_LEGACY_FIXNUM_MAX (LONG_MAX / 2)
#define RBIMPL_LEGACY_FIXNUM_MIN (LONG_MIN / 2)
#define RBIMPL_LEGACY_POSFIXABLE(_) ((_) < RBIMPL_LEGACY_FIXNUM_MAX + 1)
#define RBIMPL_LEGACY_NEGFIXABLE(_) ((_) >= RBIMPL_LEGACY_FIXNUM_MIN)
#define RBIMPL_LEGACY_FIXABLE(_)    \
    (RBIMPL_LEGACY_POSFIXABLE(_) && RBIMPL_LEGACY_NEGFIXABLE(_))

/* Note: RB_FIXABLE()/RB_POSFIXABLE()/RB_NEGFIXABLE() keep their historical
 * long-sized semantics.  The interpreter's wider immediate range is spelled
 * RBIMPL_FIXABLE() (or the unprefixed FIXABLE(), which internal.h points at
 * the wide variant). */

static inline SIGNED_VALUE
rbimpl_fixnum_value(VALUE x)
{
    /* Subtracting the tag before the signed conversion avoids shifting a
     * negative value and is valid for the complete encoded range. */
    return (SIGNED_VALUE)(x - RUBY_FIXNUM_FLAG) / 2;
}

static inline VALUE
rbimpl_fixnum_from_value(SIGNED_VALUE x)
{
    RBIMPL_ASSERT_OR_ASSUME(RBIMPL_FIXABLE(x));
    /* Shifting the unsigned counterpart keeps every step well defined while
     * preserving two's complement bit patterns. */
    return (VALUE)(((uintptr_t)x << 1) | RUBY_FIXNUM_FLAG);
}

#define RBIMPL_FIXNUM_VALUE(_)       rbimpl_fixnum_value(_)
#define RBIMPL_FIXNUM_FROM_VALUE(_)  rbimpl_fixnum_from_value(_)

#endif /* RBIMPL_ARITHMETIC_FIXNUM_H */
