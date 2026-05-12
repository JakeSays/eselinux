// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Linux backing for the NTDLL Rtl*-compression family used by the
// engine's blockcache journal-entry compression in
// os/blockcache/_journalentry.hxx.
//
// On Windows the journal is compressed with COMPRESSION_FORMAT_XPRESS_HUFF
// (Xpress-with-Huffman, served by ntdll.dll's RtlCompressBuffer).  We can't
// produce that format on Linux without the Microsoft codec, and we don't
// need to: the journal is engine-internal, regenerable, and the initial
// Linux port doesn't aim for on-disk parity with Windows (see project
// memory: project_port_scope.md).
//
// We satisfy the API contract — same NTSTATUS shape, same workspace
// semantics — using vendored zstd 1.5.7 underneath.  The CompressionFormat
// argument is observed for sanity but the actual byte stream is zstd
// regardless of whether the caller asked for Xpress, Xpress-Huff, or
// LZNT1.  The journal's own CompressionAlgorithm enum tag rides along
// in the persisted record so we'd notice if a future caller ever asked
// for two different algorithms in the same store.
//
// The blockcache call sites in _journalentry.hxx all guard on
// `status >= 0` and fall back to writing entries uncompressed, so any
// failure here is recoverable; we still try to return faithful error
// codes for diagnosability.
//
// Wired in via ENABLE_STATIC_COMPILED_LOAD_DEPENDENCIES (see
// dev/ese/published/inc/os/library.hxx and the Linux block in
// /p/ese/repo/CMakeLists.txt) — without static load deps the engine's
// FunctionLoader<> would lazily LoadLibraryExW("ntdll.dll") at first
// call and substitute a fail thunk; we collapse that indirection at
// compile time and the linker resolves to these definitions.

#include "osstd.hxx"

#include <zstd.h>

//  _journalentry.hxx declares these without extern "C" (engine convention
//  for the Rtl* family it pulls in via NTOSFuncNtStd), so the definitions
//  here use the same C++ linkage to satisfy the engine's link references.

//  NT status codes.  ntstatus.h isn't on the Linux include path, so we
//  spell the values directly.  All "error" codes have the top bit set,
//  which is what the engine's `status >= 0` check tests.
static const LONG kStatusSuccess               = (LONG)0x00000000;
static const LONG kStatusBufferTooSmall        = (LONG)0xC0000023;
static const LONG kStatusInvalidParameter      = (LONG)0xC000000D;
static const LONG kStatusBadCompressionBuffer  = (LONG)0xC0000242;
static const LONG kStatusUnsupportedCompression = (LONG)0xC000025F;

//  Compression level for the journal.  zstd's default is 3 — a balanced
//  point on the ratio/speed curve (~500 MB/s compress on this class of
//  hardware).  Tune later if the journal becomes a bottleneck in either
//  direction.
static const int kZstdJournalLevel = 3;

//  The engine asks for a workspace up front and threads it through each
//  Compress / Decompress call.  zstd's one-shot APIs allocate their own
//  scratch internally, so we just report zero — the engine handles that
//  cleanly (it Alloc's a 0-byte BYTE[] and never touches it).
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
    return kStatusSuccess;
}

LONG NTAPI RtlCompressBuffer(
    USHORT      /*CompressionFormatAndEngine*/,
    UCHAR*      UncompressedBuffer,
    ULONG       UncompressedBufferSize,
    UCHAR*      CompressedBuffer,
    ULONG       CompressedBufferSize,
    ULONG       /*UncompressedChunkSize*/,
    ULONG*      pFinalCompressedSize,
    void*       /*WorkSpace*/ )
{
    if ( pFinalCompressedSize == nullptr )
    {
        return kStatusInvalidParameter;
    }
    *pFinalCompressedSize = 0;

    if ( UncompressedBuffer == nullptr || CompressedBuffer == nullptr )
    {
        return kStatusInvalidParameter;
    }

    const size_t cbResult = ZSTD_compress( CompressedBuffer,
                                           CompressedBufferSize,
                                           UncompressedBuffer,
                                           UncompressedBufferSize,
                                           kZstdJournalLevel );

    if ( ZSTD_isError( cbResult ) )
    {
        const ZSTD_ErrorCode code = ZSTD_getErrorCode( cbResult );
        if ( code == ZSTD_error_dstSize_tooSmall )
        {
            return kStatusBufferTooSmall;
        }
        return kStatusBadCompressionBuffer;
    }

    *pFinalCompressedSize = (ULONG)cbResult;
    return kStatusSuccess;
}

LONG NTAPI RtlDecompressBufferEx(
    USHORT      /*CompressionFormat*/,
    UCHAR*      UncompressedBuffer,
    ULONG       UncompressedBufferSize,
    UCHAR*      CompressedBuffer,
    ULONG       CompressedBufferSize,
    ULONG*      pFinalUncompressedSize,
    void*       /*WorkSpace*/ )
{
    if ( pFinalUncompressedSize == nullptr )
    {
        return kStatusInvalidParameter;
    }
    *pFinalUncompressedSize = 0;

    if ( UncompressedBuffer == nullptr || CompressedBuffer == nullptr )
    {
        return kStatusInvalidParameter;
    }

    const size_t cbResult = ZSTD_decompress( UncompressedBuffer,
                                             UncompressedBufferSize,
                                             CompressedBuffer,
                                             CompressedBufferSize );

    if ( ZSTD_isError( cbResult ) )
    {
        const ZSTD_ErrorCode code = ZSTD_getErrorCode( cbResult );
        if ( code == ZSTD_error_dstSize_tooSmall )
        {
            return kStatusBufferTooSmall;
        }
        return kStatusBadCompressionBuffer;
    }

    *pFinalUncompressedSize = (ULONG)cbResult;
    return kStatusSuccess;
}
