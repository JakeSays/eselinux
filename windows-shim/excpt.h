// Linux shim for <excpt.h>.  SEH (__try/__except, GetExceptionInformation,
// EXCEPTION_POINTERS) is Windows-only.  On Linux we emulate the keyword
// pair with a thread-local sigsetjmp stack plus a process-wide SIGSEGV
// handler installed lazily on first __try entry.  See winapi_seh.cxx for
// the runtime side.
#pragma once

#include <stdint.h>
#include <setjmp.h>

// SEH "filter" return values.
#define EXCEPTION_EXECUTE_HANDLER       1
#define EXCEPTION_CONTINUE_SEARCH       0
#define EXCEPTION_CONTINUE_EXECUTION   (-1)

//  Win32 EXCEPTION_RECORD / EXCEPTION_POINTERS — must be defined as
//  full types (not forward-declared) so engine code can dereference
//  the EXCEPTION_POINTERS GetExceptionInformation() now returns.  Layout
//  matches the Windows SDK winnt.h for source compatibility.
#define EXCEPTION_MAXIMUM_PARAMETERS 15

typedef struct _EXCEPTION_RECORD
{
    unsigned long           ExceptionCode;
    unsigned long           ExceptionFlags;
    struct _EXCEPTION_RECORD* ExceptionRecord;
    void*                   ExceptionAddress;
    unsigned long           NumberParameters;
    unsigned long           __pad;          //  alignment to 8 on 64-bit
    unsigned long long      ExceptionInformation[ EXCEPTION_MAXIMUM_PARAMETERS ];
} EXCEPTION_RECORD, *PEXCEPTION_RECORD;

typedef struct _EXCEPTION_POINTERS
{
    EXCEPTION_RECORD*       ExceptionRecord;
    void*                   ContextRecord;
} EXCEPTION_POINTERS, *PEXCEPTION_POINTERS, *LPEXCEPTION_POINTERS;

// Common NTSTATUS-shaped exception codes used by the engine.
#define STATUS_ACCESS_VIOLATION         ((unsigned long)0xC0000005)
#define STATUS_IN_PAGE_ERROR            ((unsigned long)0xC0000006)
#define STATUS_DISK_FULL                ((unsigned long)0xC000007F)

#ifdef __cplusplus
extern "C" {
#endif

//  SEH intrinsics.  GetExceptionInformation() returns a synthetic
//  EXCEPTION_POINTERS that reflects the current top _SehFrame's
//  exception state — populated by the SIGSEGV handler (STATUS_ACCESS_
//  VIOLATION) or by _SehRaiseException (JETTEST_EXCEP_ENFORCE etc.).
//  Must only be called from within an __except filter expression, where
//  the topmost _SehGuard is still on the chain.  The returned pointer
//  is to thread-local storage and remains valid until the next __except
//  on the same thread.
EXCEPTION_POINTERS* GetExceptionInformation( void );
unsigned long       GetExceptionCode( void );

//  Thread-local SEH frame chain.  Push/pop runs in normal context; the
//  signal handler walks the chain to find the longjmp target.
//
//  exceptionCode / exceptionInfo let __except filters distinguish what
//  kind of "fault" caused the longjmp:
//   * SIGSEGV         → STATUS_ACCESS_VIOLATION, info == NULL
//   * Enforce failure → JETTEST_EXCEP_ENFORCE, info == JetTestEnforceSEHException*
typedef struct _SehFrame
{
    sigjmp_buf          env;
    int                 faulted;
    unsigned long       exceptionCode;
    void*               exceptionInfo;
    struct _SehFrame*   prev;
} _SehFrame;

void _SehPush( _SehFrame* f );
void _SehPop( _SehFrame* f );

//  Inspect the current top frame's exception state.  Returns 0 if no
//  frame is active.  Filters in __except call these to decide whether
//  to handle the fault.
unsigned long   _SehTopExceptionCode( void );
void*           _SehTopExceptionInfo( void );

//  Raise a non-signal "exception" — i.e. mark the top frame as faulted
//  with the given code/info and siglongjmp.  Used by
//  JetTestReportEnforceFail's Linux variant.  Aborts the process if
//  there is no active frame.
void _SehRaiseException( unsigned long exceptionCode, void* exceptionInfo );

//  JETTEST_EXCEP_ENFORCE itself lives in jettest.hxx — both Windows
//  and Linux read the same definition.

#ifdef __cplusplus
}  //  extern "C"

//  RAII guard.  Constructor pushes the frame; destructor pops it on every
//  scope exit (normal or via siglongjmp landing in __except).
//
//  sigsetjmp must be invoked AT THE CALL SITE, not wrapped in a method —
//  the C standard only mandates correct behavior when setjmp/sigsetjmp
//  is the controlling expression (or part of one) of a selection
//  statement.  Hide it behind a macro instead.
class _SehGuard
{
public:
    _SehGuard() noexcept
    {
        m_frame.faulted       = 0;
        m_frame.exceptionCode = 0;
        m_frame.exceptionInfo = nullptr;
        _SehPush( &m_frame );
    }
    ~_SehGuard() noexcept
    {
        _SehPop( &m_frame );
    }
    bool faulted() const noexcept { return m_frame.faulted != 0; }
    sigjmp_buf& env() noexcept    { return m_frame.env; }

private:
    _SehFrame   m_frame;
    _SehGuard( const _SehGuard& )            = delete;
    _SehGuard& operator=( const _SehGuard& ) = delete;
};

#endif  //  __cplusplus

// __try / __except / __finally / __leave shims.  C++ gets real SEH-like
// behavior; the C build keeps the older no-op shape (no SEH-using code
// is C-only on the engine).
//
// The C++17 init-statement-in-if scopes _seh through both branches:
//
//   __try { body } __except( filter ) { handler }
//
// becomes
//
//   if ( _SehGuard _seh; _seh.Setjmp() == 0 ) { body }
//   else if ( _seh.faulted() && (filter) != EXCEPTION_CONTINUE_SEARCH ) { handler }
//
// On first entry Setjmp() returns 0 and the body runs.  If the body
// SIGSEGVs, the handler in winapi_seh.cxx sets _seh.faulted=1 and
// siglongjmps; control resumes inside the if's setjmp call, this time
// returning 1, so we fall to the else branch.  When the scope unwinds,
// _seh's destructor pops the frame.
#ifndef ESE_COMPILER_MSVC
#  undef __try
#  undef __except
#  undef __finally
#  undef __leave
#  ifdef __cplusplus
#    define __try \
            if ( _SehGuard _seh; sigsetjmp( _seh.env(), 1 ) == 0 )
#    define __except( filter ) \
            else if ( _seh.faulted() && ( (filter) != EXCEPTION_CONTINUE_SEARCH ) )
#  else
#    define __try
#    define __except( filter ) if (0)
#  endif
#  define __finally
#  define __leave do { } while (0)
#endif
