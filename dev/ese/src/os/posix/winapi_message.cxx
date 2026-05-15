// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// FormatMessageW for the Linux port.
//
// On Windows, FormatMessage looks up message templates from a resource
// section inside the module identified by the HMODULE argument
// (FORMAT_MESSAGE_FROM_HMODULE) or from the system error table
// (FORMAT_MESSAGE_FROM_SYSTEM), then substitutes positional inserts
// (%1, %2, ...) from the caller-supplied argument list.
//
// The Linux port has no .res sections, so the message table is
// generated at build time from jetmsgex.mc into jetmsgex_table.cxx
// (see dev/ese/src/_res/gen_msg_table.pl) and binary-searched here.
//
// Supported flags (everything the engine and eseutil use):
//   FORMAT_MESSAGE_FROM_HMODULE       — engine's own messages (g_rgEseMsgTable)
//   FORMAT_MESSAGE_FROM_SYSTEM        — Win32 → errno → strerror
//   FORMAT_MESSAGE_FROM_STRING        — caller-supplied template
//   FORMAT_MESSAGE_ARGUMENT_ARRAY     — Arguments is DWORD_PTR* (vs va_list*)
//   FORMAT_MESSAGE_IGNORE_INSERTS     — emit template verbatim
//   FORMAT_MESSAGE_ALLOCATE_BUFFER    — LocalAlloc the output, write to *lpBuffer
//   FORMAT_MESSAGE_MAX_WIDTH_MASK     — low byte: max line width
//                                       (0 = preserve breaks, nonzero = drop
//                                        soft breaks; we don't wrap because
//                                        no caller asks for a finite width)
//
// Substitution rules inside a template (in non-IGNORE_INSERTS mode):
//   %1..%99    → positional insert
//   %1!spec!   → insert with printf format spec (engine's jetmsgex.mc
//                doesn't use any of these in practice; we recognise the
//                syntax and skip the spec)
//   %n         → system newline (\r\n)
//   %r         → carriage return
//   %t         → tab
//   %0         → string terminator (suppresses trailing newline)
//   %%         → literal %
//   %<other>   → emit <other> verbatim

#include "osstd.hxx"

#include <errno.h>
#include <locale.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

extern "C"
{
    struct EseMsgTableEntry
    {
        unsigned long  id;
        const wchar_t* sz;
    };
    extern const EseMsgTableEntry g_rgEseMsgTable[];
    extern const size_t           g_cEseMsgTable;
}

namespace
{

const wchar_t* FindMessageTemplate( DWORD msgid )
{
    size_t lo = 0;
    size_t hi = g_cEseMsgTable;
    while ( lo < hi )
    {
        const size_t        mid = lo + ( hi - lo ) / 2;
        const unsigned long id  = g_rgEseMsgTable[ mid ].id;
        if ( id == msgid )
        {
            return g_rgEseMsgTable[ mid ].sz;
        }
        if ( id < msgid )
        {
            lo = mid + 1;
        }
        else
        {
            hi = mid;
        }
    }
    return nullptr;
}

//  Render a system error code into an ASCII description.  Engine callers
//  pass Win32 codes; we translate the small set that the OS layer
//  actually surfaces and fall back to the raw numeric value.
//
//  `strerror` is `LC_MESSAGES`-locale-aware on Linux — i.e. would
//  emit the description in the process's currently-selected locale.
//  We want the engine's diagnostic surface to be English-only
//  regardless of host locale (matches the en-US `g_rgEseMsgTable`
//  used elsewhere), so the strerror call runs under a temporary C
//  locale via uselocale().
void FormatSystemMessage( DWORD msgid, wchar_t* dst, size_t cchDst )
{
    int en = 0;
    switch ( msgid )
    {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
            en = ENOENT;
            break;
        case ERROR_ACCESS_DENIED:
            en = EACCES;
            break;
        case ERROR_INVALID_HANDLE:
            en = EBADF;
            break;
        case ERROR_NOT_ENOUGH_MEMORY:
        case ERROR_OUTOFMEMORY:
            en = ENOMEM;
            break;
        case ERROR_INVALID_PARAMETER:
            en = EINVAL;
            break;
        case ERROR_DISK_FULL:
            en = ENOSPC;
            break;
        default:
            en = 0;
            break;
    }

    char nbuf[ 128 ];
    if ( en != 0 )
    {
        static locale_t cloc = newlocale( LC_ALL_MASK, "C", (locale_t)0 );
        const locale_t prev = uselocale( cloc );
        const char* msg = strerror( en );
        snprintf( nbuf, sizeof( nbuf ), "%s (Win32 0x%lx)",
                  msg ? msg : "(unknown)", (unsigned long)msgid );
        uselocale( prev );
    }
    else
    {
        snprintf( nbuf, sizeof( nbuf ), "System error 0x%lx", (unsigned long)msgid );
    }

    size_t i = 0;
    while ( i + 1 < cchDst && nbuf[ i ] != '\0' )
    {
        dst[ i ] = (wchar_t)(unsigned char)nbuf[ i ];
        ++i;
    }
    dst[ i ] = L'\0';
}

void AppendWChar( wchar_t* dst, size_t cchDst, size_t* used, wchar_t c )
{
    if ( *used + 1 < cchDst )
    {
        dst[ ( *used )++ ] = c;
    }
}

void AppendWString( wchar_t* dst, size_t cchDst, size_t* used, const wchar_t* src )
{
    if ( src == nullptr )
    {
        return;
    }
    while ( *src != L'\0' && *used + 1 < cchDst )
    {
        dst[ ( *used )++ ] = *src++;
    }
}

//  Engine call sites pass `(va_list*)rgpszString` with
//  FORMAT_MESSAGE_ARGUMENT_ARRAY — the value is really a DWORD_PTR
//  array.  When ARGUMENT_ARRAY isn't set the caller would hand us a
//  va_list and we'd have to read positionally; nothing in the engine
//  does that and SysV varargs make it awkward, so we just emit an
//  empty string for inserts in that case.
const wchar_t* InsertString( DWORD index, DWORD dwFlags, void* args )
{
    if ( args == nullptr || index < 1 || index > 99 )
    {
        return L"";
    }
    if ( dwFlags & FORMAT_MESSAGE_ARGUMENT_ARRAY )
    {
        DWORD_PTR* rg = static_cast<DWORD_PTR*>( args );
        const wchar_t* sz = reinterpret_cast<const wchar_t*>( rg[ index - 1 ] );
        return sz != nullptr ? sz : L"";
    }
    return L"";
}

//  Expand a template, returning chars written (excluding terminator)
//  or zero on overflow.  Honors FORMAT_MESSAGE_MAX_WIDTH_MASK (low byte
//  nonzero) by dropping soft line breaks — i.e., embedded '\n'/'\r'
//  that came from the .mc source as multi-line bodies rather than from
//  an explicit %n directive.
DWORD ExpandTemplate( const wchar_t* tpl, wchar_t* dst, size_t cchDst,
                      DWORD dwFlags, void* args )
{
    if ( dst == nullptr || cchDst == 0 )
    {
        return 0;
    }

    const bool fIgnoreInserts = ( dwFlags & FORMAT_MESSAGE_IGNORE_INSERTS ) != 0;
    //  Drop soft breaks only when the width-mask byte is exactly
    //  0xFF (the "no max width but drop soft breaks" sentinel).
    //  Bounded widths 1..254 are rejected up in FormatMessageW().
    const bool fDropSoftBreaks = ( dwFlags & FORMAT_MESSAGE_MAX_WIDTH_MASK )
                                 == FORMAT_MESSAGE_MAX_WIDTH_MASK;

    size_t used       = 0;
    bool   terminator = false;

    for ( const wchar_t* p = tpl; *p != L'\0' && !terminator; )
    {
        if ( fDropSoftBreaks && ( *p == L'\r' || *p == L'\n' ) )
        {
            ++p;
            continue;
        }

        if ( *p != L'%' )
        {
            AppendWChar( dst, cchDst, &used, *p++ );
            continue;
        }

        const wchar_t c = p[ 1 ];

        //  Insert index — one or two decimal digits.
        if ( c >= L'1' && c <= L'9' )
        {
            DWORD n = static_cast<DWORD>( c - L'0' );
            const wchar_t* q = p + 2;
            if ( *q >= L'0' && *q <= L'9' )
            {
                n = n * 10 + static_cast<DWORD>( *q - L'0' );
                ++q;
            }
            //  Optional !fmtspec! — not used by the engine.  Skip past it.
            if ( *q == L'!' )
            {
                ++q;
                while ( *q != L'\0' && *q != L'!' )
                {
                    ++q;
                }
                if ( *q == L'!' )
                {
                    ++q;
                }
            }

            if ( fIgnoreInserts )
            {
                while ( p < q )
                {
                    AppendWChar( dst, cchDst, &used, *p++ );
                }
            }
            else
            {
                AppendWString( dst, cchDst, &used, InsertString( n, dwFlags, args ) );
                p = q;
            }
            continue;
        }

        //  Escape sequences.
        if ( c == L'0' )
        {
            //  %0 ends the message; subsequent text (including any
            //  trailing newline mc.exe would have appended) is dropped.
            terminator = true;
            p += 2;
            continue;
        }
        if ( c == L'%' )
        {
            AppendWChar( dst, cchDst, &used, L'%' );
            p += 2;
            continue;
        }
        if ( c == L'n' )
        {
            AppendWChar( dst, cchDst, &used, L'\r' );
            AppendWChar( dst, cchDst, &used, L'\n' );
            p += 2;
            continue;
        }
        if ( c == L'r' )
        {
            AppendWChar( dst, cchDst, &used, L'\r' );
            p += 2;
            continue;
        }
        if ( c == L't' )
        {
            AppendWChar( dst, cchDst, &used, L'\t' );
            p += 2;
            continue;
        }

        if ( c == L'\0' )
        {
            //  Trailing % at end of template — emit as-is.
            AppendWChar( dst, cchDst, &used, L'%' );
            ++p;
            continue;
        }

        //  %<other> → emit <other> verbatim, per Win32 convention.
        AppendWChar( dst, cchDst, &used, c );
        p += 2;
    }

    if ( used < cchDst )
    {
        dst[ used ] = L'\0';
    }
    else
    {
        dst[ cchDst - 1 ] = L'\0';
        return 0;  // overflowed
    }
    return static_cast<DWORD>( used );
}

}  // namespace

extern "C"
{

DWORD FormatMessageW( DWORD dwFlags, LPCVOID lpSource, DWORD dwMessageId,
                      DWORD dwLanguageId, LPWSTR lpBuffer, DWORD nSize,
                      va_list* Arguments )
{
    //  Validate MAX_WIDTH_MASK byte.  Engine usage is exclusively
    //  0xFF (= "drop soft breaks, no wrap"), which we honor in
    //  ExpandTemplate.  Real width-bounded wrapping (1..254) is
    //  not implemented; reject rather than silently misbehave.
    const DWORD widthByte = dwFlags & FORMAT_MESSAGE_MAX_WIDTH_MASK;
    if ( widthByte != 0 && widthByte != FORMAT_MESSAGE_MAX_WIDTH_MASK )
    {
        SetLastError( ERROR_INVALID_PARAMETER );
        return 0;
    }

    //  Validate dwLanguageId for HMODULE lookups.  We ship only the
    //  en-US `g_rgEseMsgTable`, so accept LANGIDs whose primary
    //  language is English (or LANG_NEUTRAL / 0 which mean "use the
    //  process default" and are commonly passed by callers that
    //  don't care).  LANG_INVARIANT (0x7F) also accepted as a
    //  request for "no locale-specific output", which our English-
    //  only table happens to provide.  Anything else returns
    //  ERROR_RESOURCE_LANG_NOT_FOUND per Win32 spec.
    if ( dwFlags & FORMAT_MESSAGE_FROM_HMODULE )
    {
        const DWORD primary = dwLanguageId & 0x3FF;
        const bool langOk = ( dwLanguageId == 0 ) ||
                            ( primary == LANG_NEUTRAL ) ||
                            ( primary == LANG_INVARIANT ) ||
                            ( primary == LANG_ENGLISH );
        if ( !langOk )
        {
            SetLastError( ERROR_RESOURCE_LANG_NOT_FOUND );
            return 0;
        }
    }

    const wchar_t* tpl = nullptr;
    wchar_t        systemMsg[ 128 ];

    if ( dwFlags & FORMAT_MESSAGE_FROM_STRING )
    {
        tpl = reinterpret_cast<const wchar_t*>( lpSource );
    }
    else
    {
        if ( dwFlags & FORMAT_MESSAGE_FROM_HMODULE )
        {
            tpl = FindMessageTemplate( dwMessageId );
        }
        if ( tpl == nullptr && ( dwFlags & FORMAT_MESSAGE_FROM_SYSTEM ) )
        {
            FormatSystemMessage( dwMessageId, systemMsg, _countof( systemMsg ) );
            tpl = systemMsg;
        }
    }

    if ( tpl == nullptr )
    {
        if ( dwFlags & FORMAT_MESSAGE_ALLOCATE_BUFFER )
        {
            *reinterpret_cast<LPWSTR*>( lpBuffer ) = nullptr;
        }
        return 0;
    }

    //  Stage the expanded output into a stack buffer.  16K WCHARs is
    //  generous for engine event messages (the longest hit a few hundred
    //  characters even after insert substitution); overflow falls
    //  through to "return 0" which engine call sites already handle.
    wchar_t     work[ 16384 ];
    const DWORD cchOut = ExpandTemplate( tpl, work, _countof( work ),
                                         dwFlags, reinterpret_cast<void*>( Arguments ) );

    if ( cchOut == 0 && tpl[ 0 ] != L'\0' )
    {
        if ( dwFlags & FORMAT_MESSAGE_ALLOCATE_BUFFER )
        {
            *reinterpret_cast<LPWSTR*>( lpBuffer ) = nullptr;
        }
        return 0;
    }

    if ( dwFlags & FORMAT_MESSAGE_ALLOCATE_BUFFER )
    {
        const size_t cchAlloc = ( static_cast<size_t>( cchOut ) > static_cast<size_t>( nSize ) )
                              ? static_cast<size_t>( cchOut ) + 1
                              : static_cast<size_t>( nSize ) + 1;
        wchar_t* alloc = static_cast<wchar_t*>( LocalAlloc( LMEM_FIXED, cchAlloc * sizeof( wchar_t ) ) );
        if ( alloc == nullptr )
        {
            *reinterpret_cast<LPWSTR*>( lpBuffer ) = nullptr;
            return 0;
        }
        memcpy( alloc, work, ( cchOut + 1 ) * sizeof( wchar_t ) );
        *reinterpret_cast<LPWSTR*>( lpBuffer ) = alloc;
    }
    else
    {
        if ( lpBuffer == nullptr || cchOut + 1 > nSize )
        {
            return 0;
        }
        memcpy( lpBuffer, work, ( cchOut + 1 ) * sizeof( wchar_t ) );
    }

    return cchOut;
}

}  // extern "C"
