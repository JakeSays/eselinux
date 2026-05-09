// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Stubs for the NTDLL Rtl*-compression family used by the engine's
// blockcache journal-entry compression in os/blockcache/_journalentry.hxx.
// Linux has no NT compression API; we don't have a portable replacement
// wired in yet, and the Linux port doesn't yet exercise the blockcache
// journal compression path. The call sites in _journalentry.hxx all
// guard on `status >= 0` and fall back to writing journal entries
// uncompressed when compression fails, so returning a negative NTSTATUS
// here is the right semantic.
//
// Wired in via ENABLE_STATIC_COMPILED_LOAD_DEPENDENCIES (see
// dev/ese/published/inc/os/library.hxx and the Linux block in
// /p/ese/repo/CMakeLists.txt) — without static load deps the engine's
// FunctionLoader<> would lazily LoadLibraryExW("ntdll.dll") at first
// call and substitute a fail thunk when that fails; we collapse the
// indirection at compile time and the linker resolves to these stubs.

#include "osstd.hxx"

//  Note: _journalentry.hxx declares these without extern "C" (engine
//  convention for the Rtl* family it pulls in via NTOSFuncNtStd), so the
//  stubs here use the same C++ linkage to satisfy the engine's link
//  references.

//  STATUS_NOT_IMPLEMENTED — high bit set marks "error", and the engine's
//  call sites check `status >= 0`. NT defines this as 0xC0000002 in
//  ntstatus.h; reproducing the literal here avoids dragging the Win32
//  status codes into the Linux header surface.
static const LONG kStatusNotImplemented = (LONG)0xC0000002;

LONG NTAPI RtlGetCompressionWorkSpaceSize(
    USHORT  /*CompressionFormatAndEngine*/,
    ULONG*  pCompressBufferWorkSpaceSize,
    ULONG*  pCompressFragmentWorkSpaceSize )
{
    if ( pCompressBufferWorkSpaceSize )
    {
        *pCompressBufferWorkSpaceSize = 0;
    }
    if ( pCompressFragmentWorkSpaceSize )
    {
        *pCompressFragmentWorkSpaceSize = 0;
    }
    return kStatusNotImplemented;
}

LONG NTAPI RtlCompressBuffer(
    USHORT      /*CompressionFormatAndEngine*/,
    UCHAR*      /*UncompressedBuffer*/,
    ULONG       /*UncompressedBufferSize*/,
    UCHAR*      /*CompressedBuffer*/,
    ULONG       /*CompressedBufferSize*/,
    ULONG       /*UncompressedChunkSize*/,
    ULONG*      pFinalCompressedSize,
    void*       /*WorkSpace*/ )
{
    if ( pFinalCompressedSize )
    {
        *pFinalCompressedSize = 0;
    }
    return kStatusNotImplemented;
}

LONG NTAPI RtlDecompressBufferEx(
    USHORT      /*CompressionFormat*/,
    UCHAR*      /*UncompressedBuffer*/,
    ULONG       /*UncompressedBufferSize*/,
    UCHAR*      /*CompressedBuffer*/,
    ULONG       /*CompressedBufferSize*/,
    ULONG*      pFinalUncompressedSize,
    void*       /*WorkSpace*/ )
{
    if ( pFinalUncompressedSize )
    {
        *pFinalUncompressedSize = 0;
    }
    return kStatusNotImplemented;
}
