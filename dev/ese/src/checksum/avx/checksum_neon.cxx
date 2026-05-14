// Copyright (c) Jake Helfert
// Licensed under the MIT License.
//
// AArch64 NEON implementation of the HSIAO ECC checksum, sitting
// under the same `ChecksumNewFormatAVX` symbol that the AVX path
// exports on x86.  Keeping the name lets:
//
//   - the function-pointer dispatch in checksum.cxx route to it
//     through the same `pfn = ChecksumNewFormatAVX` arm,
//   - the AVX-tagged correctness sites in checksum_test.cxx
//     (e.g. `pfnChecksumNewFormat == ChecksumNewFormatAVX`) work
//     unchanged.
//
// Algorithm is the upstream AVX implementation lifted to NEON
// intrinsics:
//
//   - 256-bit-wide accumulators become two 128-bit `uint8x16_t`
//     halves per logical 256-bit chunk.
//   - 256-bit XORs map to a pair of `veorq_u8`.
//   - The parity mask (popcount mod 2 of a 256-bit value) maps to
//     XOR-reducing the two halves into a single 128-bit value
//     (parity is preserved across XOR), then folding to a uint64
//     and using `__builtin_popcountll`.  Faster than the byte-wise
//     `vcntq_u8 + vaddlvq_u8` path because we only need parity,
//     not the actual popcount.
//   - Non-temporal prefetches use `__builtin_prefetch(p, 0, 0)`
//     which the AArch64 backend emits as `prfm pldl1strm`.

#include "checksumstd.hxx"

#if defined ESE_ARCH_ARM64

#include <arm_neon.h>


inline XECHECKSUM MakeChecksumFromECCXORAndPgno(
    const ULONG eccChecksum,
    const ULONG xorChecksum,
    const ULONG pgno )
{
    const XECHECKSUM low  = xorChecksum ^ pgno;
    const XECHECKSUM high = (XECHECKSUM)eccChecksum << 32;
    return ( high | low );
}


//  3-bit-parity-per-byte lookup table.  Same bits as the AVX path's
//  g_bECCLookupTable; we keep a private copy here so the NEON TU is
//  self-contained (the AVX TU compiles to nothing on aarch64).
__attribute__(( aligned( 128 ) ))
static const unsigned char g_bECCLookupTableNeon[ 256 ] =
{
    0x00, 0x70, 0x61, 0x11, 0x52, 0x22, 0x33, 0x43, 0x43, 0x33, 0x22, 0x52, 0x11, 0x61, 0x70, 0x00,
    0x34, 0x44, 0x55, 0x25, 0x66, 0x16, 0x07, 0x77, 0x77, 0x07, 0x16, 0x66, 0x25, 0x55, 0x44, 0x34,
    0x25, 0x55, 0x44, 0x34, 0x77, 0x07, 0x16, 0x66, 0x66, 0x16, 0x07, 0x77, 0x34, 0x44, 0x55, 0x25,
    0x11, 0x61, 0x70, 0x00, 0x43, 0x33, 0x22, 0x52, 0x52, 0x22, 0x33, 0x43, 0x00, 0x70, 0x61, 0x11,
    0x16, 0x66, 0x77, 0x07, 0x44, 0x34, 0x25, 0x55, 0x55, 0x25, 0x34, 0x44, 0x07, 0x77, 0x66, 0x16,
    0x22, 0x52, 0x43, 0x33, 0x70, 0x00, 0x11, 0x61, 0x61, 0x11, 0x00, 0x70, 0x33, 0x43, 0x52, 0x22,
    0x33, 0x43, 0x52, 0x22, 0x61, 0x11, 0x00, 0x70, 0x70, 0x00, 0x11, 0x61, 0x22, 0x52, 0x43, 0x33,
    0x07, 0x77, 0x66, 0x16, 0x55, 0x25, 0x34, 0x44, 0x44, 0x34, 0x25, 0x55, 0x16, 0x66, 0x77, 0x07,
    0x07, 0x77, 0x66, 0x16, 0x55, 0x25, 0x34, 0x44, 0x44, 0x34, 0x25, 0x55, 0x16, 0x66, 0x77, 0x07,
    0x33, 0x43, 0x52, 0x22, 0x61, 0x11, 0x00, 0x70, 0x70, 0x00, 0x11, 0x61, 0x22, 0x52, 0x43, 0x33,
    0x22, 0x52, 0x43, 0x33, 0x70, 0x00, 0x11, 0x61, 0x61, 0x11, 0x00, 0x70, 0x33, 0x43, 0x52, 0x22,
    0x16, 0x66, 0x77, 0x07, 0x44, 0x34, 0x25, 0x55, 0x55, 0x25, 0x34, 0x44, 0x07, 0x77, 0x66, 0x16,
    0x11, 0x61, 0x70, 0x00, 0x43, 0x33, 0x22, 0x52, 0x52, 0x22, 0x33, 0x43, 0x00, 0x70, 0x61, 0x11,
    0x25, 0x55, 0x44, 0x34, 0x77, 0x07, 0x16, 0x66, 0x66, 0x16, 0x07, 0x77, 0x34, 0x44, 0x55, 0x25,
    0x34, 0x44, 0x55, 0x25, 0x66, 0x16, 0x07, 0x77, 0x77, 0x07, 0x16, 0x66, 0x25, 0x55, 0x44, 0x34,
    0x00, 0x70, 0x61, 0x11, 0x52, 0x22, 0x33, 0x43, 0x43, 0x33, 0x22, 0x52, 0x11, 0x61, 0x70, 0x00,
};

inline ULONG lECCLookup8bit( const ULONG byte )
{
    return g_bECCLookupTableNeon[ byte & 0xff ];
}


//  256-bit value held as two uint8x16_t halves.
struct V256
{
    uint8x16_t lo;
    uint8x16_t hi;
};

inline V256 V256Zero()
{
    const uint8x16_t z = vdupq_n_u8( 0 );
    return { z, z };
}

inline V256 V256Load( const uint8_t* p )
{
    return { vld1q_u8( p ), vld1q_u8( p + 16 ) };
}

inline V256 V256Xor( const V256 a, const V256 b )
{
    return { veorq_u8( a.lo, b.lo ), veorq_u8( a.hi, b.hi ) };
}


//  Parity mask of 256 bits: returns 0 or 0xFFFFFFFF based on the
//  XOR of every bit in the input.
//
//  Parity is linear under XOR (parity(a^b) == parity(a)^parity(b)),
//  so XOR-reducing the two 128-bit halves to a single 128-bit value
//  preserves parity.  Fold once more to uint64_t and use the
//  builtin popcount; we only need parity (mod 2), so any sequence
//  of XOR reductions that ends in popcount-mod-2 is correct.
inline LONG lParityMaskNeon256( const V256 qq )
{
    const uint8x16_t combined = veorq_u8( qq.lo, qq.hi );
    const uint64x2_t as64 = vreinterpretq_u64_u8( combined );
    const uint64_t   r = vgetq_lane_u64( as64, 0 ) ^ vgetq_lane_u64( as64, 1 );
    return -( (LONG)__builtin_popcountll( r ) & 1 );
}


XECHECKSUM ChecksumNewFormatAVX( const unsigned char * const pb, const ULONG cb, const ULONG pgno, BOOL fHeaderBlock )
//  HSIAO ECC over a 1KB-8KB page, NEON-accelerated.  Direct port of
//  the AVX implementation; see comments there for the bit-layout
//  rationale.
{
    Assert( 0 == ( cb & ( cb - 1 ) ) );
    Assert( 1024 <= cb && cb <= 8192 );
    Assert( 0 == ( (uintptr_t)pb & ( 256 - 1 ) ) );

    const ULONG cqq = cb / 32;

    //================================================
    //  Row parity (p, p').  Accumulators qq0..qq3 hold column
    //  parities (one per 256-bit chunk-of-row, summed across all
    //  rows) for the q / q' / r / r' phases below.

    ULONG p = 0;
    V256  qq0 = V256Zero();
    V256  qq1 = V256Zero();
    V256  qq2 = V256Zero();
    V256  qq3 = V256Zero();
    {
        ULONG idxp = 0xfc000000;
        ULONG i = 0;
        V256  qqL0;

        if ( fHeaderBlock )
        {
            //  First 64 bits of the page are the checksum itself —
            //  zero them before folding into the accumulator.
            uint8x16_t lo   = vld1q_u8( pb );
            uint8x16_t hi   = vld1q_u8( pb + 16 );
            uint64x2_t lo64 = vreinterpretq_u64_u8( lo );
            lo64            = vsetq_lane_u64( 0, lo64, 0 );
            qqL0.lo         = vreinterpretq_u8_u64( lo64 );
            qqL0.hi         = hi;
            goto Start;
        }

        do
        {
            qqL0 = V256Load( pb + ( i + 0 ) * 32 );
Start:
            __builtin_prefetch( pb + ( i + 16 ) * 32, 0, 0 );

            const V256 qqL1 = V256Load( pb + ( i + 1 ) * 32 );
            const V256 qqL2 = V256Load( pb + ( i + 2 ) * 32 );
            const V256 qqL3 = V256Load( pb + ( i + 3 ) * 32 );

            qq0 = V256Xor( qq0, qqL0 );
            qq1 = V256Xor( qq1, qqL1 );
            qq2 = V256Xor( qq2, qqL2 );
            qq3 = V256Xor( qq3, qqL3 );
            const V256 qqLAcc = V256Xor( V256Xor( qqL0, qqL1 ), V256Xor( qqL2, qqL3 ) );

            __builtin_prefetch( pb + ( i + 16 + 2 ) * 32, 0, 0 );

            p ^= idxp & lParityMaskNeon256( qqLAcc );
            idxp += 0xfc000400;
            i += 4;
        }
        while ( i < cqq );
    }

    //================================================
    //  Column parity (q, q') across the 4 accumulators.

    const V256 qqAcc = V256Xor( V256Xor( qq0, qq1 ), V256Xor( qq2, qq3 ) );

    ULONG q = 0;
    ULONG idxq = 0xff000000;
    q ^= idxq & lParityMaskNeon256( qq0 );
    idxq += 0xff000100;
    q ^= idxq & lParityMaskNeon256( qq1 );
    idxq += 0xff000100;
    q ^= idxq & lParityMaskNeon256( qq2 );
    idxq += 0xff000100;
    q ^= idxq & lParityMaskNeon256( qq3 );

    //================================================
    //  Sub-column parity (q_).  Treat qqAcc as 8 × 32-bit words,
    //  iterate, accumulate.

    UINT pdw[ 8 ];
    vst1q_u32( pdw + 0, vreinterpretq_u32_u8( qqAcc.lo ) );
    vst1q_u32( pdw + 4, vreinterpretq_u32_u8( qqAcc.hi ) );

    ULONG q_ = 0;
    ULONG idxq_ = 0xffe00000;
    UINT dwAcc = 0;
    for ( ULONG i = 0; i < 8; i++ )
    {
        const UINT dwT = pdw[ i ];
        dwAcc ^= dwT;
        q_ ^= idxq_ & -( (LONG)__builtin_popcount( dwT ) & 0x01 );
        idxq_ += 0xffe00020;
    }

    //================================================
    //  r and r' bits.  Walk dwAcc byte by byte.

    ULONG r = 0;
    UINT byteT = dwAcc;
    ULONG byte0 = 0;
    ULONG idxr = 0xfff80000;
    for ( ULONG i = 0; i < 4; i++ )
    {
        r ^= idxr & -( (LONG)__builtin_popcount( byteT & 0xff ) & 0x01 );
        byte0 ^= byteT;
        byteT >>= 8;
        idxr += 0xfff80008;
    }

    //================================================
    //  Pre-calculated ECC bits via the byte table.

    const LONG bits = (LONG)lECCLookup8bit( byte0 );
    r |= ( bits & 0x07 );
    r |= ( ( bits << 12 ) & 0x00070000 );

    //================================================
    //  Mask high bits and assemble.

    const ULONG mask = ( cb << 19 ) - 1;
    const ULONG ecc  = p & 0xfc00fc00 & mask | q & 0x03000300 | q_ & 0x00e000e0 | r & 0x001f001f;
    const ULONG xor_ = dwAcc;
    return MakeChecksumFromECCXORAndPgno( ecc, xor_, pgno );
}

#endif // ESE_ARCH_ARM64
