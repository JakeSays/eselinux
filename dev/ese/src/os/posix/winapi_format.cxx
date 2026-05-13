// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// MSVC-compatible printf/scanf re-implementation.
//
// Engine code is full of Windows-flavoured format strings ("%I64d",
// "%lu", "%ws", "%S", ...).  We do not rewrite any of them; instead we
// walk the format string ourselves and emit POSIX-compatible primitives
// via libc snprintf where the conversion semantics line up.
//
// MSVC differences handled here (no preprocessor rewrite of format
// strings anywhere in the codebase):
//
//   * %I64{d,i,u,o,x,X}  — 64-bit (long long)
//   * %I32{...}          — 32-bit (int)
//   * %I{...}            — pointer-sized (size_t / ptrdiff_t)
//   * %ws / %wc / %S / %C, %ls / %lc — wide string / wide char
//   * Lone %l{d,i,u,o,x,X}  — 32-bit "Win32 long".  Windows is LLP64 so
//     callers of `printf("%lu", ULONG_var)` push 4 bytes.  Linux is
//     LP64; libc's %lu would read 8 bytes and corrupt the stack.  We
//     read with va_arg(int) and emit no length modifier to libc.  %ll
//     and %I64 still mean 64-bit.
//
// Entry points (extern "C") consumed by other TUs in the OS layer:
//
//   * NarrowVPrintfImpl   — char buffer    — StringCbVPrintfA
//   * NarrowVFPrintfImpl  — FILE* stream   — printf / fprintf
//   * WideVPrintfImpl     — wchar_t buffer — StringCbVPrintfW
//   * WideVFPrintfImpl    — FILE* (UTF-8)  — wprintf / fwprintf
//   * NarrowVSScanfImpl   — char input     — swscanf_s after wide→narrow
//
// Engine wchar_t is 16-bit (-fshort-wchar); libc's wide CRT operates on
// 32-bit native wchar_t and would print garbage, so the wide path never
// goes through libc's wide functions.

#include "osstd.hxx"

#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
// ---- narrow output sinks ---------------------------------------------

struct NBufSink
{
    char* dst;
    size_t cap; // total slots including terminator
    size_t used; // chars written so far (excl. terminator)
    bool overflow;

    void Put(char c)
    {
        if (used + 1 < cap)
        {
            dst[used++] = c;
        }
        else
        {
            overflow = true;
        }
    }

    void PutN(const char* s, size_t n)
    {
        for (size_t i = 0; i < n; ++i)
            Put(s[i]);
    }

    void PutWide(const wchar_t* ws)
    {
        if (!ws)
            ws = L"(null)";
        while (*ws)
        {
            Put(static_cast<char>(*ws++));
        }
    }
};

struct NFileSink
{
    FILE* fp;
    char buf[256];
    size_t buflen;
    int written;
    bool error;

    void Flush()
    {
        if (buflen > 0 && fp && !error)
        {
            const size_t w = fwrite(buf, 1, buflen, fp);
            written += static_cast<int>(w);
            if (w != buflen)
                error = true;
        }
        buflen = 0;
    }

    void Put(char c)
    {
        if (buflen >= sizeof(buf))
            Flush();
        buf[buflen++] = c;
    }

    void PutN(const char* s, size_t n)
    {
        if (n == 0)
            return;
        if (buflen + n > sizeof(buf))
        {
            Flush();
            if (n > sizeof(buf))
            {
                if (fp && !error)
                {
                    const size_t w = fwrite(s, 1, n, fp);
                    written += static_cast<int>(w);
                    if (w != n)
                        error = true;
                }
                return;
            }
        }
        memcpy(buf + buflen, s, n);
        buflen += n;
    }

    void PutWide(const wchar_t* ws)
    {
        if (!ws)
            ws = L"(null)";
        while (*ws)
        {
            Put(static_cast<char>(*ws++));
        }
    }
};

// ---- core narrow formatter -------------------------------------------
//
// Templated on the sink concept (Put / PutN / PutWide) so the same logic
// services bounded buffers (NBufSink) and unbuffered streams (NFileSink).

template<typename Sink>
void NarrowFormatV(Sink& sink, const char* fmt, va_list args)
{
    if (!fmt)
        return;

    for (const char* p = fmt; *p;)
    {
        if (*p != '%')
        {
            const char* run = p;
            while (*p && *p != '%')
                ++p;
            sink.PutN(run, static_cast<size_t>(p - run));
            continue;
        }
        ++p;
        if (*p == '%')
        {
            sink.Put('%');
            ++p;
            continue;
        }

        char spec[64];
        size_t sn = 0;
        spec[sn++] = '%';

        // flags
        while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0')
        {
            if (sn < sizeof(spec) - 4)
                spec[sn++] = *p;
            ++p;
        }
        // width (* or digits)
        if (*p == '*')
        {
            int dynWidth = va_arg(args, int);
            int w = dynWidth < 0
                    ? -dynWidth
                    : dynWidth;
            if (dynWidth < 0 && sn < sizeof(spec) - 4)
                spec[sn++] = '-';
            char wbuf[16];
            int wlen = snprintf(wbuf, sizeof(wbuf), "%d", w);
            for (int i = 0; i < wlen && sn < sizeof(spec) - 4; ++i)
                spec[sn++] = wbuf[i];
            ++p;
        }
        else
        {
            while (*p >= '0' && *p <= '9')
            {
                if (sn < sizeof(spec) - 4)
                    spec[sn++] = *p;
                ++p;
            }
        }
        // precision
        if (*p == '.')
        {
            if (sn < sizeof(spec) - 4)
                spec[sn++] = '.';
            ++p;
            if (*p == '*')
            {
                int pr = va_arg(args, int);
                char pbuf[16];
                int plen = snprintf(pbuf, sizeof(pbuf), "%d", pr);
                for (int i = 0; i < plen && sn < sizeof(spec) - 4; ++i)
                    spec[sn++] = pbuf[i];
                ++p;
            }
            else
            {
                while (*p >= '0' && *p <= '9')
                {
                    if (sn < sizeof(spec) - 4)
                        spec[sn++] = *p;
                    ++p;
                }
            }
        }
        //  Size modifier.  sz codes:
        //      0    int (default)
        //      'h'  short,    'H' = hh / signed char
        //      'l'  Win32 long — engine pushed 32 bits; never pass 'l'
        //           through to libc on LP64.  Read with va_arg(int).
        //      'L'  ll / __int64 (64-bit)
        //      'z'  size_t,   't' ptrdiff_t,   'P' pointer-sized
        bool wideSpec = false;
        char sz = 0;
        if (*p == 'w')
        {
            wideSpec = true;
            ++p;
        }
        else if (*p == 'h')
        {
            ++p;
            if (*p == 'h')
            {
                sz = 'H';
                ++p;
            }
            else { sz = 'h'; }
        }
        else if (*p == 'l')
        {
            ++p;
            if (*p == 'l')
            {
                sz = 'L';
                ++p;
            }
            else { sz = 'l'; }
        }
        else if (*p == 'I')
        {
            ++p;
            if (p[0] == '6' && p[1] == '4')
            {
                sz = 'L';
                p += 2;
            }
            else if (p[0] == '3' && p[1] == '2')
            {
                sz = 0;
                p += 2;
            }
            else { sz = 'P'; }
        }
        else if (*p == 'z')
        {
            sz = 'z';
            ++p;
        }
        else if (*p == 't')
        {
            sz = 't';
            ++p;
        }
        else
            if (*p == 'L')
            {
                sz = 'L';
                ++p;
            }

        char conv = *p;
        if (*p)
            ++p;

        //  Wide string/char.  In narrow context %s/%c are wide when
        //  paired with %w or %l; %S/%C are always wide.
        if (conv == 'S' || (conv == 's' && (wideSpec || sz == 'l')))
        {
            const wchar_t* ws = va_arg(args, const wchar_t*);
            sink.PutWide(ws);
            continue;
        }
        if (conv == 'C' || (conv == 'c' && (wideSpec || sz == 'l')))
        {
            int wc = va_arg(args, int);
            sink.Put(static_cast<char>(wc));
            continue;
        }

        //  Build the libc-compatible length modifier.
        switch (sz)
        {
            case 'h':
                spec[sn++] = 'h';
                break;
            case 'H':
                spec[sn++] = 'h';
                spec[sn++] = 'h';
                break;
            case 'l': /* drop — Win32 long is 32-bit on the wire */ break;
            case 'L':
                spec[sn++] = 'l';
                spec[sn++] = 'l';
                break;
            case 'z':
                spec[sn++] = 'z';
                break;
            case 't':
                spec[sn++] = 't';
                break;
            case 'P':
                spec[sn++] = 'z';
                break;
        }
        spec[sn++] = conv;
        spec[sn] = '\0';

        char buf[256];
        int n = 0;
        switch (conv)
        {
            case 'd':
            case 'i':
            case 'u':
            case 'x':
            case 'X':
            case 'o':
                if (sz == 'L')
                    n = snprintf(buf, sizeof(buf), spec, va_arg(args, long long));
                else if (sz == 'z')
                    n = snprintf(buf, sizeof(buf), spec, va_arg(args, size_t));
                else if (sz == 't')
                    n = snprintf(buf, sizeof(buf), spec, va_arg(args, ptrdiff_t));
                else if (sz == 'P')
                    n = snprintf(buf, sizeof(buf), spec, va_arg(args, size_t));
                else
                    n = snprintf(buf, sizeof(buf), spec, va_arg(args, int));
                break;
            case 'p':
                n = snprintf(buf, sizeof(buf), spec, va_arg(args, void*));
                break;
            case 'f':
            case 'e':
            case 'g':
            case 'E':
            case 'G':
            case 'a':
            case 'A':
                n = snprintf(buf, sizeof(buf), spec, va_arg(args, double));
                break;
            case 's':
            {
                const char* s = va_arg(args, const char*);
                if (!s)
                    s = "(null)";
                sink.PutN(s, strlen(s));
                n = 0;
                break;
            }
            case 'c':
                sink.Put(static_cast<char>(va_arg(args, int)));
                n = 0;
                break;
            default:
                //  Unknown specifier — emit verbatim, do not consume a vararg.
                sink.PutN(spec, sn);
                n = 0;
                break;
        }
        if (n > 0)
        {
            const int cap = static_cast<int>(sizeof(buf) - 1);
            if (n > cap)
                n = cap;
            sink.PutN(buf, static_cast<size_t>(n));
        }
    }
}

// ---- wide formatter --------------------------------------------------
//
// Single-sink (wchar_t buffer) for now.  Stream output is built by
// formatting into a stack buffer and converting to UTF-8 in
// WideVFPrintfImpl below.

struct WSink
{
    wchar_t* dst;
    size_t cap; // total slots including terminator
    size_t used; // wchars written so far (excl. terminator)
    bool overflow;

    void Put(wchar_t c)
    {
        if (used + 1 < cap)
        {
            dst[used++] = c;
        }
        else
        {
            overflow = true;
        }
    }

    void PutAscii(const char* s, size_t n)
    {
        for (size_t i = 0; i < n; ++i)
            Put(static_cast<wchar_t>(s[i]));
    }
};

struct SpecInfo
{
    char flags[8];
    size_t flagsN = 0;
    char width[16];
    size_t widthN = 0;
    char prec[16];
    size_t precN = 0;
    bool hasPrec = false;
    //  0=int, 'h'=short, 'l'=Win32 long (32-bit), 'L'='ll', 'z'=size_t,
    //  't'=ptrdiff_t, '6'=I64, 'P'=pointer-sized
    char size = 0;
    bool wide = false; // %ws %wc %S %lS — wide string/char form
    char conv = 0;
};

//  Render a (possibly negative) int into a SpecInfo digit-slot
//  (`s->width[]` or `s->prec[]`).  Used by the `%*x` / `%.*x` arms
//  below to convert a runtime width/precision into the same digit
//  string a literal width/precision would have produced.
void AppendDigits(char* dst, size_t cap, size_t& n, int value)
{
    if (value < 0)
    {
        if (n < cap) dst[n++] = '-';
        value = -value;
    }
    char tmp[12];
    int t = 0;
    if (value == 0)
    {
        if (n < cap) dst[n++] = '0';
        return;
    }
    while (value > 0 && t < (int) sizeof(tmp))
    {
        tmp[t++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    }
    while (t > 0 && n < cap)
    {
        dst[n++] = tmp[--t];
    }
}

//  va_list is `__va_list_tag[1]` on x86-64 / aarch64 clang.  Function-
//  parameter va_lists decay to `__va_list_tag*`, so "pass by reference"
//  comes for free: ParseSpec's va_arg calls walk the same underlying
//  __va_list_tag the caller's WideFormatV iterates over.  No & needed
//  at the callsite, and va_list cannot legally be taken by reference
//  in any portable way.
void ParseSpec(const wchar_t*& p, SpecInfo* s, va_list args)
{
    // flags
    for (;;)
    {
        const wchar_t c = *p;
        if (c == L'-' || c == L'+' || c == L' ' || c == L'#' || c == L'0')
        {
            if (s->flagsN < sizeof(s->flags))
                s->flags[s->flagsN++] = static_cast<char>(c);
            ++p;
        }
        else
            break;
    }
    // width (* consumes an int from args; digits accumulate as-is)
    if (*p == L'*')
    {
        AppendDigits(s->width, sizeof(s->width), s->widthN,
                     va_arg(args, int));
        ++p;
    }
    else
    {
        while (*p >= L'0' && *p <= L'9')
        {
            if (s->widthN < sizeof(s->width))
                s->width[s->widthN++] = static_cast<char>(*p);
            ++p;
        }
    }
    // precision
    if (*p == L'.')
    {
        s->hasPrec = true;
        ++p;
        if (*p == L'*')
        {
            AppendDigits(s->prec, sizeof(s->prec), s->precN,
                         va_arg(args, int));
            ++p;
        }
        else
        {
            while (*p >= L'0' && *p <= L'9')
            {
                if (s->precN < sizeof(s->prec))
                    s->prec[s->precN++] = static_cast<char>(*p);
                ++p;
            }
        }
    }
    // size modifiers
    if (*p == L'I')
    {
        ++p;
        if (p[0] == L'6' && p[1] == L'4')
        {
            s->size = '6';
            p += 2;
        }
        else if (p[0] == L'3' && p[1] == L'2')
        {
            s->size = 0;
            p += 2;
        }
        else { s->size = 'P'; }
    }
    else if (*p == L'l')
    {
        ++p;
        if (*p == L'l')
        {
            s->size = 'L';
            ++p;
        }
        else { s->size = 'l'; }
    }
    else if (*p == L'h')
    {
        ++p;
        s->size = 'h';
        if (*p == L'h')
            ++p;
    }
    else if (*p == L'z')
    {
        s->size = 'z';
        ++p;
    }
    else if (*p == L't')
    {
        s->size = 't';
        ++p;
    }
    else if (*p == L'L')
    {
        s->size = 'L';
        ++p;
    }
    else
        if (*p == L'w')
        {
            s->wide = true;
            ++p;
        }

    s->conv = static_cast<char>(*p);
    if (*p)
        ++p;
}

size_t BuildNarrowSpec(const SpecInfo& s, char convOverride, char* out)
{
    size_t n = 0;
    out[n++] = '%';
    for (size_t i = 0; i < s.flagsN; ++i)
        out[n++] = s.flags[i];
    for (size_t i = 0; i < s.widthN; ++i)
        out[n++] = s.width[i];
    if (s.hasPrec)
    {
        out[n++] = '.';
        for (size_t i = 0; i < s.precN; ++i)
            out[n++] = s.prec[i];
    }
    switch (s.size)
    {
        case 'h':
            out[n++] = 'h';
            break;
        case 'l': /* drop — Win32 long is 32-bit on the wire */ break;
        case 'L':
            out[n++] = 'l';
            out[n++] = 'l';
            break;
        case 'z':
            out[n++] = 'z';
            break;
        case 't':
            out[n++] = 't';
            break;
        case '6':
            out[n++] = 'l';
            out[n++] = 'l';
            break;
        case 'P':
            out[n++] = 'z';
            break;
        default:
            break;
    }
    out[n++] = convOverride;
    out[n] = '\0';
    return n;
}

void EmitNarrowString(WSink& sink, const SpecInfo& s, const char* p)
{
    if (!p)
        p = "(null)";
    size_t srcLen = strlen(p);
    if (s.hasPrec)
    {
        size_t prec = 0;
        for (size_t i = 0; i < s.precN; ++i)
            prec = prec * 10 + (s.prec[i] - '0');
        if (srcLen > prec)
            srcLen = prec;
    }
    size_t width = 0;
    for (size_t i = 0; i < s.widthN; ++i)
        width = width * 10 + (s.width[i] - '0');
    const bool leftAlign = (memchr(s.flags, '-', s.flagsN) != nullptr);
    const size_t pad = (width > srcLen)
                       ? width - srcLen
                       : 0;
    if (!leftAlign)
        for (size_t i = 0; i < pad; ++i)
            sink.Put(L' ');
    for (size_t i = 0; i < srcLen; ++i)
        sink.Put(static_cast<wchar_t>(p[i]));
    if (leftAlign)
        for (size_t i = 0; i < pad; ++i)
            sink.Put(L' ');
}

void EmitWideString(WSink& sink, const SpecInfo& s, const wchar_t* p)
{
    if (!p)
    {
        EmitNarrowString(sink, s, "(null)");
        return;
    }
    size_t srcLen = 0;
    while (p[srcLen])
        ++srcLen;
    if (s.hasPrec)
    {
        size_t prec = 0;
        for (size_t i = 0; i < s.precN; ++i)
            prec = prec * 10 + (s.prec[i] - '0');
        if (srcLen > prec)
            srcLen = prec;
    }
    size_t width = 0;
    for (size_t i = 0; i < s.widthN; ++i)
        width = width * 10 + (s.width[i] - '0');
    const bool leftAlign = (memchr(s.flags, '-', s.flagsN) != nullptr);
    const size_t pad = (width > srcLen)
                       ? width - srcLen
                       : 0;
    if (!leftAlign)
        for (size_t i = 0; i < pad; ++i)
            sink.Put(L' ');
    for (size_t i = 0; i < srcLen; ++i)
        sink.Put(p[i]);
    if (leftAlign)
        for (size_t i = 0; i < pad; ++i)
            sink.Put(L' ');
}

HRESULT WideFormatV(WSink& sink, const wchar_t* fmt, va_list args)
{
    if (!fmt)
    {
        return STRSAFE_E_INVALID_PARAMETER;
    }

    for (const wchar_t* p = fmt; *p;)
    {
        if (*p != L'%')
        {
            sink.Put(*p++);
            continue;
        }
        ++p;
        if (*p == L'%')
        {
            sink.Put(L'%');
            ++p;
            continue;
        }

        SpecInfo s{};
        ParseSpec(p, &s, args);

        switch (s.conv)
        {
            case 's':
            {
                //  Win32 convention: in a WIDE format string %s is wide; %hs
                //  forces narrow.
                if (s.size == 'h')
                {
                    EmitNarrowString(sink, s, va_arg(args, const char*));
                }
                else
                {
                    EmitWideString(sink, s, va_arg(args, const wchar_t*));
                }
                break;
            }
            case 'S':
            {
                //  %S in wide-format means narrow string.
                EmitNarrowString(sink, s, va_arg(args, const char*));
                break;
            }
            case 'c':
            {
                if (s.size == 'h')
                {
                    sink.Put(static_cast<wchar_t>(static_cast<unsigned char>(va_arg(args, int))));
                }
                else
                {
                    sink.Put(static_cast<wchar_t>(va_arg(args, int)));
                }
                break;
            }
            case 'C':
            {
                sink.Put(static_cast<wchar_t>(static_cast<unsigned char>(va_arg(args, int))));
                break;
            }
            case 'd':
            case 'i':
            case 'u':
            case 'x':
            case 'X':
            case 'o':
            case 'p':
            case 'f':
            case 'e':
            case 'g':
            case 'E':
            case 'G':
            case 'a':
            case 'A':
            {
                char nspec[64];
                BuildNarrowSpec(s, s.conv, nspec);
                char buf[96];
                int n = 0;
                if (s.conv == 'p')
                {
                    n = snprintf(buf, sizeof(buf), nspec, va_arg(args, void*));
                }
                else if (s.conv == 'f' || s.conv == 'e' || s.conv == 'g'
                    || s.conv == 'E' || s.conv == 'G' || s.conv == 'a' || s.conv == 'A')
                {
                    n = snprintf(buf, sizeof(buf), nspec, va_arg(args, double));
                }
                else if (s.size == '6' || s.size == 'L')
                {
                    n = snprintf(buf, sizeof(buf), nspec, va_arg(args, long long));
                }
                else if (s.size == 'z')
                {
                    n = snprintf(buf, sizeof(buf), nspec, va_arg(args, size_t));
                }
                else if (s.size == 't')
                {
                    n = snprintf(buf, sizeof(buf), nspec, va_arg(args, ptrdiff_t));
                }
                else if (s.size == 'P')
                {
                    n = snprintf(buf, sizeof(buf), nspec, va_arg(args, size_t));
                }
                else
                {
                    //  default + lone 'l' (Win32 long) — read 32-bit int
                    n = snprintf(buf, sizeof(buf), nspec, va_arg(args, int));
                }
                if (n < 0)
                    n = 0;
                if (n > static_cast<int>(sizeof(buf) - 1))
                    n = static_cast<int>(sizeof(buf) - 1);
                sink.PutAscii(buf, static_cast<size_t>(n));
                break;
            }
            case 'n':
                //  %n is a security hazard; engine doesn't use it.
                (void) va_arg(args, int*);
                break;
            default:
                sink.Put(L'%');
                sink.Put(static_cast<wchar_t>(s.conv));
                break;
        }
    }

    return sink.overflow
           ? STRSAFE_E_INSUFFICIENT_BUFFER
           : S_OK;
}

// ---- scanf -----------------------------------------------------------

void StoreScannedInt(void* dst, char sz, bool isSigned, long long sval, unsigned long long uval)
{
    if (!dst)
        return;
    const long long v = isSigned
                        ? sval
                        : static_cast<long long>(uval);
    switch (sz)
    {
        case 'H':
            *static_cast<signed char*>(dst) = static_cast<signed char>(v);
            break;
        case 'h':
            *static_cast<short*>(dst) = static_cast<short>(v);
            break;
        case 'l':
            *static_cast<int32_t*>(dst) = static_cast<int32_t>(v);
            break; // Win32 long
        case 'L':
            *static_cast<long long*>(dst) = v;
            break;
        case 'z':
            *static_cast<size_t*>(dst) = static_cast<size_t>(v);
            break;
        case 't':
            *static_cast<ptrdiff_t*>(dst) = static_cast<ptrdiff_t>(v);
            break;
        case 'P':
            *static_cast<size_t*>(dst) = static_cast<size_t>(v);
            break;
        default:
            *static_cast<int*>(dst) = static_cast<int>(v);
            break;
    }
}
} // namespace

// ---- entry points ----------------------------------------------------

extern "C"
{
HRESULT NarrowVPrintfImpl(char* dst, size_t cchDst, const char* fmt, va_list args)
{
    if (!dst || cchDst == 0)
        return STRSAFE_E_INVALID_PARAMETER;
    NBufSink sink{dst, cchDst, 0, false};
    NarrowFormatV(sink, fmt, args);
    if (sink.used < cchDst)
        dst[sink.used] = '\0';
    else
        dst[cchDst - 1] = '\0';
    return sink.overflow
           ? STRSAFE_E_INSUFFICIENT_BUFFER
           : S_OK;
}

int NarrowVFPrintfImpl(FILE* fp, const char* fmt, va_list args)
{
    NFileSink sink{fp, {}, 0, 0, false};
    NarrowFormatV(sink, fmt, args);
    sink.Flush();
    return sink.error
           ? -1
           : sink.written;
}

HRESULT WideVPrintfImpl(wchar_t* dst, size_t cchDst, const wchar_t* fmt, va_list args)
{
    if (!dst || cchDst == 0)
        return STRSAFE_E_INVALID_PARAMETER;
    if (!fmt)
    {
        dst[0] = 0;
        return STRSAFE_E_INVALID_PARAMETER;
    }
    WSink sink{dst, cchDst, 0, false};
    const HRESULT hr = WideFormatV(sink, fmt, args);
    sink.dst[sink.used] = 0;
    return hr;
}

int WideVFPrintfImpl(FILE* fp, const wchar_t* fmt, va_list args)
{
    if (!fp || !fmt)
        return -1;
    wchar_t wbuf[4096];
    WSink sink{wbuf, sizeof(wbuf) / sizeof(wbuf[0]), 0, false};
    WideFormatV(sink, fmt, args);
    wbuf[sink.used] = 0;

    char nbuf[8192];
    const int cbN = WideCharToMultiByte(CP_UTF8, 0, wbuf, -1,
        nbuf, sizeof(nbuf),
        nullptr, nullptr);
    const size_t cb = (cbN > 0)
                      ? static_cast<size_t>(cbN - 1)
                      : 0;
    if (cb)
    {
        const size_t w = fwrite(nbuf, 1, cb, fp);
        if (w != cb)
            return -1;
    }
    return cbN > 0
           ? cbN - 1
           : -1;
}

//  MSVC-compatible narrow vsscanf.  Handles %I64/%I32 directly and
//  treats lone %l as 32-bit (Win32 long), so engine format strings work
//  unchanged on LP64 without ever invoking libc's vsscanf.  Engine usage
//  is numerics-only; %s/%c/%n are supported but we don't bother with %[
//  scanlists.
int NarrowVSScanfImpl(const char* in, const char* fmt, va_list args)
{
    if (!in || !fmt)
        return 0;

    int matched = 0;
    const char* p = fmt;

    while (*p)
    {
        const unsigned char fc = static_cast<unsigned char>(*p);
        if (isspace(fc))
        {
            while (*in && isspace(static_cast<unsigned char>(*in)))
                ++in;
            ++p;
            continue;
        }
        if (*p != '%')
        {
            if (*in != *p)
                return matched;
            ++in;
            ++p;
            continue;
        }

        ++p;
        if (*p == '%')
        {
            if (*in != '%')
                return matched;
            ++in;
            ++p;
            continue;
        }

        bool suppress = false;
        if (*p == '*')
        {
            suppress = true;
            ++p;
        }

        int width = 0;
        bool hasWidth = false;
        while (*p >= '0' && *p <= '9')
        {
            width = width * 10 + (*p - '0');
            hasWidth = true;
            ++p;
        }

        char sz = 0;
        if (*p == 'I')
        {
            ++p;
            if (p[0] == '6' && p[1] == '4')
            {
                sz = 'L';
                p += 2;
            }
            else if (p[0] == '3' && p[1] == '2')
            {
                sz = 0;
                p += 2;
            }
            else { sz = 'P'; }
        }
        else if (*p == 'l')
        {
            ++p;
            if (*p == 'l')
            {
                sz = 'L';
                ++p;
            }
            else { sz = 'l'; }
        }
        else if (*p == 'h')
        {
            ++p;
            if (*p == 'h')
            {
                sz = 'H';
                ++p;
            }
            else { sz = 'h'; }
        }
        else if (*p == 'z')
        {
            sz = 'z';
            ++p;
        }
        else if (*p == 't')
        {
            sz = 't';
            ++p;
        }
        else
            if (*p == 'L')
            {
                sz = 'L';
                ++p;
            }

        const char conv = *p;
        if (!conv)
            return matched;
        ++p;

        //  Skip whitespace before non-character conversions.
        if (conv != 'c' && conv != 'n')
        {
            while (*in && isspace(static_cast<unsigned char>(*in)))
                ++in;
        }

        switch (conv)
        {
            case 'd':
            case 'i':
            case 'u':
            case 'o':
            case 'x':
            case 'X':
            {
                int radix;
                bool isSigned;
                switch (conv)
                {
                    case 'd':
                        radix = 10;
                        isSigned = true;
                        break;
                    case 'i':
                        radix = 0;
                        isSigned = true;
                        break;
                    case 'u':
                        radix = 10;
                        isSigned = false;
                        break;
                    case 'o':
                        radix = 8;
                        isSigned = false;
                        break;
                    case 'x':
                    case 'X':
                        radix = 16;
                        isSigned = false;
                        break;
                    default:
                        radix = 10;
                        isSigned = true;
                        break;
                }

                char tmp[96];
                const size_t tmpCap = sizeof(tmp) - 1;
                const size_t cap = (hasWidth && static_cast<size_t>(width) < tmpCap)
                                   ? static_cast<size_t>(width)
                                   : tmpCap;
                size_t k = 0;
                while (*in && k < cap)
                {
                    tmp[k++] = *in++;
                }
                tmp[k] = '\0';

                char* endPtr = nullptr;
                long long sval = 0;
                unsigned long long uval = 0;
                if (isSigned)
                    sval = strtoll(tmp, &endPtr, radix);
                else
                    uval = strtoull(tmp, &endPtr, radix);

                if (!endPtr || endPtr == tmp)
                {
                    in -= k;
                    return matched;
                }

                const size_t consumed = static_cast<size_t>(endPtr - tmp);
                if (consumed < k)
                {
                    in -= (k - consumed);
                }

                if (!suppress)
                {
                    void* d = va_arg(args, void*);
                    StoreScannedInt(d, sz, isSigned, sval, uval);
                    ++matched;
                }
                break;
            }
            case 'c':
            {
                const int n = hasWidth
                              ? width
                              : 1;
                char* dst = suppress
                            ? nullptr
                            : va_arg(args, char*);
                for (int i = 0; i < n; ++i)
                {
                    if (!*in)
                        return matched;
                    if (dst)
                        dst[i] = *in;
                    ++in;
                }
                if (!suppress)
                    ++matched;
                break;
            }
            case 's':
            {
                char* dst = suppress
                            ? nullptr
                            : va_arg(args, char*);
                size_t k = 0;
                while (*in && !isspace(static_cast<unsigned char>(*in)))
                {
                    if (hasWidth && k >= static_cast<size_t>(width))
                        break;
                    if (dst)
                        dst[k] = *in;
                    ++k;
                    ++in;
                }
                if (dst)
                    dst[k] = '\0';
                if (k > 0 && !suppress)
                    ++matched;
                break;
            }
            case 'n':
                if (!suppress)
                {
                    int* dst = va_arg(args, int*);
                    if (dst)
                        *dst = 0;
                }
                break;
            default:
                return matched;
        }
    }

    return matched;
}
} // extern "C"
