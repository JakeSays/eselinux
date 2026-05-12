// Linux shim for <specstrings.h>. Stubs out the legacy SAL annotation
// macros (__in_opt / __out_bcount / etc.) as no-ops so headers authored
// for MSVC parse cleanly. The newer _In_/_Out_ family is stubbed in
// cc.hxx.
//
// We deliberately do NOT define bare __in / __out / __inout: libstdc++
// uses these tokens as parameter names (e.g. in <bits/stl_pair.h>), and
// an object-like macro replacement nukes them on use. Headers that still
// rely on the bare forms must be patched to use _In_ / _Out_ / _Inout_.
#pragma once

#ifndef ESE_COMPILER_MSVC

#define __in_z
#define __in_z_opt
#define __in_opt
#define __in_bcount(x)
#define __in_bcount_opt(x)
#define __in_bcount_z(x)
#define __in_ecount(x)
#define __in_ecount_opt(x)
#define __in_ecount_z(x)

#define __out_z
#define __out_z_opt
#define __out_opt
#define __out_bcount(x)
#define __out_bcount_opt(x)
#define __out_bcount_z(x)
#define __out_bcount_full(x)
#define __out_bcount_part(x, y)
#define __out_bcount_part_opt(x, y)
#define __out_ecount(x)
#define __out_ecount_opt(x)
#define __out_ecount_z(x)
#define __out_ecount_full(x)
#define __out_ecount_part(x, y)
#define __out_xcount(x)
#define __out_xcount_opt(x)
#define __out_xcount_part(x, y)
#define __out_xcount_part_opt(x, y)

// __inout (no suffix) — safe to define as a macro because libstdc++ does
// NOT use it as a parameter name (only __in / __out collide).
#define __inout
#define __inout_z
#define __inout_opt
#define __inout_bcount(x)
#define __inout_bcount_opt(x)
#define __inout_bcount_full(x)
#define __inout_bcount_part(x, y)
#define __inout_bcount_part_opt(x, y)
#define __inout_ecount(x)
#define __inout_ecount_opt(x)
#define __inout_bcount_z(x)
#define __inout_ecount_z(x)
#define __inout_bcount_full(x)

// Bare __deref / __deref_opt — annotate pointer params, no replacement value.
#define __range(low, hi)
#define __deref
#define __deref_opt
#define __deref_out
#define __deref_out_opt
#define __deref_out_bcount(x)
#define __deref_out_bcount_opt(x)
#define __deref_out_bcount_z(x)
#define __deref_out_ecount(x)
#define __deref_out_ecount_opt(x)
#define __deref_out_ecount_z(x)
#define __deref_in
#define __deref_in_opt
#define __deref_in_bcount(x)
#define __deref_in_bcount_opt(x)
#define __deref_in_ecount(x)
#define __deref_in_ecount_opt(x)
#define __deref_inout
#define __deref_inout_opt
#define __deref_inout_bcount(x)
#define __deref_inout_bcount_opt(x)
#define __deref_inout_bcount_z(x)
#define __deref_inout_ecount(x)
#define __deref_inout_ecount_opt(x)
#define __deref_inout_z
#define __deref_in_z
#define __deref_opt_out_opt

#define __field_bcount(x)
#define __field_ecount(x)
#define __field_bcount_opt(x)
#define __field_ecount_opt(x)

#define __nullterminated
#define __nullnullterminated
#define __reserved
#define __notnull
#define __maybenull
#define __format_string
#define __in_range(a, b)
#define __out_range(a, b)
#define __deref_out_range(a, b)
#define __field_range(a, b)

#define __analysis_assume(x) ((void)0)
#define __success(x)
#define __fallthrough           [[fallthrough]]

// A handful of modern SAL macros leak into the public JET header (jethdr.w
// and the generated jet.h / eseex_x.h) which does NOT include cc.hxx. Define
// them as no-ops here so any TU including <jet.h> parses cleanly. The full
// modern-SAL stub set lives in cc.hxx for the engine; these duplicate
// definitions are harmless thanks to the guards below.
#ifndef _Return_type_success_
#define _Return_type_success_(x)
#endif

// Modern SAL stubs (subset that surfaces in <jet.h>). cc.hxx redefines the
// same names for engine-internal headers; the guards keep both arms idempotent.
#ifndef _In_
#define _In_
#define _In_opt_
#define _In_z_
#define _In_opt_z_
#define _In_reads_(x)
#define _In_reads_opt_(x)
#define _In_reads_bytes_(x)
#define _In_reads_bytes_opt_(x)
#define _In_bytecount_(x)
#define _In_bytecount_opt_(x)
#define _In_bytecount_c_(x)
#define _In_range_(x, y)

#define _Out_
#define _Out_opt_
#define _Out_writes_(x)
#define _Out_writes_opt_(x)
#define _Out_writes_bytes_(x)
#define _Out_writes_bytes_opt_(x)
#define _Out_writes_z_(x)
#define _Out_writes_to_(x, y)
#define _Out_writes_to_opt_(x, y)
#define _Out_writes_bytes_to_(x, y)
#define _Out_writes_bytes_to_opt_(x, y)
#define _Out_cap_(x)
#define _Out_opt_cap_(x)
#define _Out_bytecap_(x)
#define _Out_opt_bytecap_(x)

#define _Inout_
#define _Inout_opt_
#define _Inout_z_
#define _Inout_updates_(x)
#define _Inout_updates_opt_(x)
#define _Inout_updates_bytes_(x)
#define _Inout_updates_bytes_opt_(x)

#define _Outptr_
#define _Outptr_opt_
#define _Outptr_result_z_
#define _Outptr_opt_result_z_
#define _Outptr_result_buffer_(x)

#define _Pre_notnull_
#define _Pre_z_
#define _Pre_opt_z_
#define _Post_invalid_
#define _Post_writable_byte_size_(x)
#define _Ret_maybenull_
#define _Ret_opt_z_
#define _Null_terminated_
#define _NullNull_terminated_
#define _When_(c, a)
#define _Reserved_
#define _Notnull_
#define _Maybenull_

#define _Field_size_(x)
#define _Field_size_opt_(x)
#define _Field_size_bytes_(x)
#define _Field_size_bytes_opt_(x)
#define _Field_z_
#define _Field_range_(x, y)

#define _Readable_bytes_(x)
#define _Readable_elements_(x)
#define _Writable_bytes_(x)
#define _Writable_elements_(x)

#define _Out_cap_post_count_(x, y)
#define _Out_writes_(x)

#define _Success_(x)
#define _On_failure_(x)
#define _Always_(x)
#define _Result_nullonfailure_
#define _Result_zeroonfailure_
#define _Pre_satisfies_(x)
#define _Post_satisfies_(x)
#define _Notliteral_
#endif // !_In_

#endif // !_MSC_VER
