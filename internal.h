#ifndef RUBY_INTERNAL_H                                  /*-*-C-*-vi:se ft=c:*/
#define RUBY_INTERNAL_H 1
/**
 * @author     $Author$
 * @date       Tue May 17 11:42:20 JST 2011
 * @copyright  Copyright (C) 2011 Yukihiro Matsumoto
 * @copyright  This  file  is   a  part  of  the   programming  language  Ruby.
 *             Permission  is hereby  granted,  to  either redistribute  and/or
 *             modify this file, provided that  the conditions mentioned in the
 *             file COPYING are met.  Consult the file for details.
 */
#include "ruby/internal/config.h"

#ifdef __cplusplus
# error not for C++
#endif

#define LIKELY(x) RB_LIKELY(x)
#define UNLIKELY(x) RB_UNLIKELY(x)

#define numberof(array) ((int)(sizeof(array) / sizeof((array)[0])))
#define roomof(x, y) (((x) + (y) - 1) / (y))
#define type_roomof(x, y) roomof(sizeof(x), sizeof(y))

/* Prevent compiler from reordering access */
#define ACCESS_ONCE(type,x) (*((volatile type *)&(x)))

#define UNDEF_P         RB_UNDEF_P
#define NIL_OR_UNDEF_P  RB_NIL_OR_UNDEF_P

#include "ruby/ruby.h"

/* The core uses the full VALUE-sized immediate integer representation and
 * physical special-const checks.  The public aliases above intentionally keep
 * their long-sized compatibility view for extension libraries.
 *
 * Core code that can observe the wide (VALUE-sized) Fixnum range must test with
 * WFIXNUM_P and decode with FIX2SV/FIX2UV.  Plain FIXNUM_P keeps the historical
 * long-sized contract, so it pairs with FIX2LONG (and must not be combined with
 * FIX2SV).  This explicit split replaces the previous silent redefinition of
 * FIXNUM_P inside the core.
 *
 * Likewise, the wide/physical special-const check is WFIXNUM_P's sibling
 * WSPECIAL_CONST_P (== rbimm_special_const_p, which includes wide immediates).
 * Plain SPECIAL_CONST_P / RB_SPECIAL_CONST_P keep the historical long-sized
 * view (wide immediates are hidden, as extension libraries expect), so they
 * must be used only at extension boundaries.
 *
 * Wide helpers are provided under explicit new names (WFIXNUM_P,
 * WSPECIAL_CONST_P, FIX2SV/FIX2UV, RBIMPL_FIXNUM_*).  Legacy names
 * (FIXNUM_P, FIX2LONG, FIXNUM_MAX, FIXABLE, RUBY_FIXNUM_MAX, etc.) are NOT
 * redefined here and keep their public long-sized semantics.  This avoids
 * macro redefinition and makes wide vs legacy use explicit. */
#define WFIXNUM_P rbimpl_fixnum_p
#define WSPECIAL_CONST_P rbimm_special_const_p
/* Full-width decoders under explicit new names.  FIX2LONG/FIX2ULONG keep
 * their historical `long' return type (see ruby/internal/arithmetic/long.h);
 * core code that handles wide immediates must use FIX2SV/FIX2UV. */
#define FIX2SV(x)    rbimpl_fixnum_value(x)
#define FIX2UV(x)    ((uintptr_t)rbimpl_fixnum_value(x))
/* True if the absolute value of a SIGNED_VALUE (a fixnum) fits in unsigned
 * long.  On LLP64 `unsigned long` is 32-bit, so this routes shifts/powers with
 * widths wider than that to the Bignum path; on LP64 `unsigned long` is as wide
 * as the fixnum range and this is always true.  Comparing the absolute value as
 * uintptr_t avoids the overflow that `(SIGNED_VALUE)ULONG_MAX` suffers on LP64
 * (where ULONG_MAX exceeds SIGNED_VALUE's maximum and casts to -1). */
#define RBIMPL_FIXNUM_ABS_FITS_ULONG(_) \
    ((uintptr_t)((_) < 0 ? (uintptr_t)(-((_) + 1)) + 1 : (uintptr_t)(_)) <= (uintptr_t)ULONG_MAX)
#undef LL2NUM
#define LL2NUM RB_LL2NUM
#undef ULL2NUM
#define ULL2NUM RB_ULL2NUM
#undef TYPE
#define TYPE(_) RBIMPL_CAST((int)rb_type(_))

/* Following macros were formerly defined in this header but moved to somewhere
 * else.  In order to detect them we undef here. */

/* internal/array.h */
#undef RARRAY_AREF

/* internal/class.h */
#undef RClass
#undef RCLASS_SUPER

/* internal/hash.h */
#undef RHASH_IFNONE
#undef RHASH_SIZE
#undef RHASH_TBL
#undef RHASH_EMPTY_P

/* internal/struct.h */
#undef RSTRUCT_LEN
#undef RSTRUCT_PTR
#undef RSTRUCT_SET
#undef RSTRUCT_GET

/* Also,  we  keep  the  following  macros  here.   They  are  expected  to  be
 * overridden in each headers. */

/* internal/array.h */
#define rb_ary_new_from_args(...) rb_nonexistent_symbol(__VA_ARGS__)

/* internal/string.h */
#define rb_fstring_cstr(...) rb_nonexistent_symbol(__VA_ARGS__)

/* internal/symbol.h */
#define rb_sym_intern_ascii_cstr(...) rb_nonexistent_symbol(__VA_ARGS__)


/* MRI debug support */

/* gc.c */
void rb_obj_info_dump(VALUE obj);
void rb_obj_info_dump_loc(VALUE obj, const char *file, int line, const char *func);

/* debug.c */

RUBY_SYMBOL_EXPORT_BEGIN
void ruby_debug_breakpoint(void);
PRINTF_ARGS(void ruby_debug_printf(const char*, ...), 1, 2);
RUBY_SYMBOL_EXPORT_END

// show obj data structure without any side-effect
#define rp(obj) rb_obj_info_dump_loc((VALUE)(obj), __FILE__, __LINE__, RUBY_FUNCTION_NAME_STRING)

// same as rp, but add message header
#define rp_m(msg, obj) do { \
    fputs((msg), stderr); \
    rb_obj_info_dump((VALUE)(obj)); \
} while (0)

// `ruby_debug_breakpoint()` does nothing,
// but breakpoint is set in run.gdb, so `make gdb` can stop here.
#define bp() ruby_debug_breakpoint()

#define RBOOL(v) ((v) ? Qtrue : Qfalse)
#define RB_BIGNUM_TYPE_P(x) RB_TYPE_P((x), T_BIGNUM)

#ifndef __MINGW32__
#undef memcpy
#define memcpy ruby_nonempty_memcpy
#endif
#endif /* RUBY_INTERNAL_H */
