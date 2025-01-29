#ifndef RUBY_THREAD_WIN32_H
#define RUBY_THREAD_WIN32_H
/**********************************************************************

  thread_win32.h -

  $Author$

  Copyright (C) 2004-2007 Koichi Sasada

**********************************************************************/

/* interface */

# ifdef __CYGWIN__
# undef _WIN32
# endif

#define USE_VM_CLOCK 1

WINBASEAPI BOOL WINAPI
TryEnterCriticalSection(IN OUT LPCRITICAL_SECTION lpCriticalSection);

struct rb_native_thread {
    HANDLE thread_id;
    HANDLE interrupt_event;
};

struct rb_thread_sched_item {
    void *vm_stack;
};

struct rb_thread_sched {
    HANDLE lock;
};

#ifdef RB_THREAD_LOCAL_SPECIFIER
  NOINLINE(void rb_current_ec_set(struct rb_execution_context_struct *));

  # ifdef RUBY_EXPORT
    extern RB_THREAD_LOCAL_SPECIFIER struct rb_execution_context_struct **ruby_current_ec_tls;
    extern struct rb_execution_context_struct *ruby_current_ec;1
    extern struct rb_execution_context_struct **ruby_current_ec;
    #define rb_current_ec_get
  # else

    // for RUBY_DEBUG_LOG()
    RUBY_EXTERN RB_THREAD_LOCAL_SPECIFIER rb_atomic_t ruby_nt_serial;
    #define RUBY_NT_SERIAL 1
  # endif
#else
typedef DWORD native_tls_key_t; // TLS index

static inline void *
native_tls_get(native_tls_key_t key)
{
    // return value should be checked by caller.
    return TlsGetValue(key);
}

static inline void
native_tls_set(native_tls_key_t key, void *ptr)
{
    if (UNLIKELY(TlsSetValue(key, ptr) == 0)) {
        rb_bug("TlsSetValue() error");
    }
}

#define rb_current_ec_set(ec) native_tls_set(ruby_current_ec_key, (ec))

#if USE_RJIT
  extern native_tls_key_t rb_vm_insns_count_key;
#endif
#endif

RUBY_SYMBOL_EXPORT_BEGIN
RUBY_EXTERN native_tls_key_t ruby_current_ec_key;
RUBY_SYMBOL_EXPORT_END

#endif /* RUBY_THREAD_WIN32_H */
