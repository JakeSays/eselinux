// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Roundtrip smoke test for the RtlCompressBuffer / RtlDecompressBufferEx
// shim that backs the blockcache journal-entry compression path on Linux
// (winapi_compression.cxx, zstd 1.5.7).  The actual journal-entry
// machinery in _journalentry.hxx just wraps these calls with size
// accounting and a CRC; if the shim is correct, the journal path is too.

#include "std.hxx"

#ifndef ENABLE_JET_UNIT_TEST
#error This file should only be compiled with the unit tests!
#endif // ENABLE_JET_UNIT_TEST

//  Forward-declare the Rtl* surface with the same signatures the engine
//  uses (matches _journalentry.hxx's declarations).  We intentionally
//  bypass the engine's FunctionLoader<> indirection — under
//  ENABLE_STATIC_COMPILED_LOAD_DEPENDENCIES the loader resolves to
//  these same symbols at link time, so a direct call exercises the same
//  code path as the live journal-entry call sites.

typedef LONG NTSTATUS;

NTSTATUS RtlGetCompressionWorkSpaceSize(
    USHORT  CompressionFormatAndEngine,
    ULONG*  CompressBufferWorkSpaceSize,
    ULONG*  CompressFragmentWorkSpaceSize );

NTSTATUS RtlCompressBuffer(
    USHORT  CompressionFormatAndEngine,
    UCHAR*  UncompressedBuffer,
    ULONG   UncompressedBufferSize,
    UCHAR*  CompressedBuffer,
    ULONG   CompressedBufferSize,
    ULONG   UncompressedChunkSize,
    ULONG*  FinalCompressedSize,
    void*   WorkSpace );

NTSTATUS RtlDecompressBufferEx(
    USHORT  CompressionFormat,
    UCHAR*  UncompressedBuffer,
    ULONG   UncompressedBufferSize,
    UCHAR*  CompressedBuffer,
    ULONG   CompressedBufferSize,
    ULONG*  FinalUncompressedSize,
    void*   WorkSpace );

//  The engine asks for COMPRESSION_FORMAT_XPRESS_HUFF | COMPRESSION_ENGINE_STANDARD;
//  spell the value here so we don't have to drag <winnt.h>/<winioctl.h> in.
//  Our shim ignores the format anyway (it always uses zstd) so any value works.
static const USHORT c_compressionFormat = 0x0004 | 0x0000;  // XPRESS_HUFF | STANDARD

JETUNITTEST( BlockCacheJournalCompression, RoundTripHighlyCompressible )
{
    //  Highly compressible payload — all-zero — guaranteed to take the
    //  compressed branch (compressed output well below input size).
    const ULONG cbUncompressed = 8192;
    BYTE rgbUncompressed[ cbUncompressed ] = { };

    BYTE rgbCompressed[ cbUncompressed ];
    ULONG cbCompressed = 0;

    NTSTATUS status = RtlCompressBuffer( c_compressionFormat,
                                         rgbUncompressed,
                                         cbUncompressed,
                                         rgbCompressed,
                                         sizeof( rgbCompressed ),
                                         4096,
                                         &cbCompressed,
                                         NULL );
    CHECK( status >= 0 );
    CHECK( cbCompressed > 0 );
    CHECK( cbCompressed < cbUncompressed );

    BYTE rgbRoundTrip[ cbUncompressed ] = { };
    //  Sentinel value to detect any uninitialised bytes after decompress.
    memset( rgbRoundTrip, 0xCC, sizeof( rgbRoundTrip ) );

    ULONG cbRoundTrip = 0;
    status = RtlDecompressBufferEx( c_compressionFormat,
                                    rgbRoundTrip,
                                    sizeof( rgbRoundTrip ),
                                    rgbCompressed,
                                    cbCompressed,
                                    &cbRoundTrip,
                                    NULL );
    CHECK( status >= 0 );
    CHECK( cbRoundTrip == cbUncompressed );
    CHECK( 0 == memcmp( rgbRoundTrip, rgbUncompressed, cbUncompressed ) );
}

JETUNITTEST( BlockCacheJournalCompression, RoundTripRandomData )
{
    //  Pseudo-random payload — won't compress much but must still round-trip.
    const ULONG cbUncompressed = 4096;
    BYTE rgbUncompressed[ cbUncompressed ];
    for ( ULONG ib = 0; ib < cbUncompressed; ib++ )
    {
        //  Deterministic LCG so the test is reproducible across runs.
        rgbUncompressed[ ib ] = (BYTE)( ( ib * 1103515245u + 12345u ) >> 16 );
    }

    //  zstd's worst case for input size N is a bit over N (frame overhead),
    //  so size the destination generously.
    BYTE rgbCompressed[ cbUncompressed + 256 ];
    ULONG cbCompressed = 0;

    NTSTATUS status = RtlCompressBuffer( c_compressionFormat,
                                         rgbUncompressed,
                                         cbUncompressed,
                                         rgbCompressed,
                                         sizeof( rgbCompressed ),
                                         4096,
                                         &cbCompressed,
                                         NULL );
    CHECK( status >= 0 );
    CHECK( cbCompressed > 0 );

    BYTE rgbRoundTrip[ cbUncompressed ];
    memset( rgbRoundTrip, 0xCC, sizeof( rgbRoundTrip ) );

    ULONG cbRoundTrip = 0;
    status = RtlDecompressBufferEx( c_compressionFormat,
                                    rgbRoundTrip,
                                    sizeof( rgbRoundTrip ),
                                    rgbCompressed,
                                    cbCompressed,
                                    &cbRoundTrip,
                                    NULL );
    CHECK( status >= 0 );
    CHECK( cbRoundTrip == cbUncompressed );
    CHECK( 0 == memcmp( rgbRoundTrip, rgbUncompressed, cbUncompressed ) );
}

JETUNITTEST( BlockCacheJournalCompression, GetWorkSpaceSizeReturnsSuccess )
{
    //  Sanity-check the workspace-size query.  The Linux shim doesn't
    //  need pre-allocated workspace (zstd one-shot APIs handle their own
    //  scratch) but must report success so the call site allocates a
    //  zero-byte placeholder and proceeds with compression.
    ULONG cbBuffer = 0xDEADBEEF;
    ULONG cbFragment = 0xCAFEBABE;
    NTSTATUS status = RtlGetCompressionWorkSpaceSize( c_compressionFormat,
                                                     &cbBuffer,
                                                     &cbFragment );
    CHECK( status >= 0 );
    CHECK( cbBuffer == 0 );
    CHECK( cbFragment == 0 );
}

JETUNITTEST( BlockCacheJournalCompression, CompressIntoTooSmallBufferFails )
{
    //  The caller in _journalentry.hxx sizes the output buffer at the
    //  uncompressed size and treats any failure as "couldn't compress —
    //  fall back to uncompressed".  We need to report a negative
    //  NTSTATUS when the buffer is too small so the journal path can
    //  take that branch deterministically.
    const ULONG cbUncompressed = 4096;
    BYTE rgbUncompressed[ cbUncompressed ];
    for ( ULONG ib = 0; ib < cbUncompressed; ib++ )
    {
        rgbUncompressed[ ib ] = (BYTE)( ib * 31 );
    }

    BYTE rgbCompressed[ 16 ];  //  way too small
    ULONG cbCompressed = 0xFFFFFFFF;

    NTSTATUS status = RtlCompressBuffer( c_compressionFormat,
                                         rgbUncompressed,
                                         cbUncompressed,
                                         rgbCompressed,
                                         sizeof( rgbCompressed ),
                                         4096,
                                         &cbCompressed,
                                         NULL );
    CHECK( status < 0 );
    CHECK( cbCompressed == 0 );
}
