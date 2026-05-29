// POSIX equivalent of os/encrypt.cxx.  Upstream uses Win32 CryptoAPI
// (CryptAcquireContext / CryptGenKey / CryptEncrypt) with AES-256-CBC
// + an appended CRC32 for plaintext integrity.  The Linux port uses
// libsodium's AES-256-GCM instead — but as a *runtime-optional*
// dependency, dlopened on first use:
//
//   - libsodium is NOT a hard build-time dep.  libese.so has no
//     reference to libsodium symbols; encrypt_posix.cxx loads
//     libsodium.so via dlopen() and resolves the four functions
//     we need (sodium_init, _aes256gcm_is_available, _encrypt,
//     _decrypt, plus randombytes_buf) into a static table.
//   - Boxes without libsodium installed get cleanly-failing encrypt
//     entry points (JET_errFeatureNotAvailable) — engine simply
//     can't enable at-rest encryption.  Engines that don't request
//     encryption are unaffected.
//   - GCM is authenticated encryption (AEAD) — the auth tag replaces
//     the appended CRC32-of-plaintext integrity check.
//   - GCM is a stream cipher mode, so no padding to a 16-byte block;
//     ciphertext is the same length as plaintext.
//   - libsodium's crypto_aead_aes256gcm_* uses AES-NI on x86_64 and
//     the ARM CryptoExtension on aarch64.  The library reports 0
//     from `_is_available()` on machines without hardware support;
//     we treat that the same as a missing library.
//
// On-disk layout produced by the encrypt path:
//
//     [ciphertext (N bytes)] [auth tag (16)] [trailer (Version=1, IV=16)]
//
// The trailer struct matches the upstream layout byte-for-byte
// (1-byte Version, 16-byte InitVector), but only the first 12 bytes
// of InitVector are meaningful (the GCM nonce).  Keeping the same
// struct simplifies the engine-side accounting and lets the
// decrypt-path "find the trailer at the end" idiom carry over
// unchanged.
//
// What we keep portable from upstream:
//
//   - dwCRC32_LOOKUP_TABLE — used by ErrOSEncryptionVerifyKey for
//     key-header integrity (still keyed by the Win32 protocol)
//   - Crc32Checksum — table-driven implementation
//   - OSInitializeProcessorSupportsCRC32 — kept as a no-op
//   - ErrOSEncryptionVerifyKey — same struct, same checksum

#include "osstd.hxx"

#include <dlfcn.h>
#include <pthread.h>
#include <stddef.h>


////////////////////////////////////////////////
//  libsodium type aliases (we don't pull <sodium.h> in — the .so is
//  optional and there's no compile-time dep on its headers).
//
//  The constants below come straight from libsodium 1.0.x's stable
//  ABI; they haven't moved in years and aren't expected to.

namespace
{

constexpr size_t kAes256GcmKeyBytes = 32;
constexpr size_t kAes256GcmNonceBytes = 12;
constexpr size_t kAes256GcmAbytes = 16;

typedef int ( *PfnSodiumInit )( void );
typedef int ( *PfnAes256GcmIsAvailable )( void );
typedef int ( *PfnAes256GcmEncrypt )(
        unsigned char* c, unsigned long long* clen_p,
        const unsigned char* m, unsigned long long mlen,
        const unsigned char* ad, unsigned long long adlen,
        const unsigned char* nsec,
        const unsigned char* npub,
        const unsigned char* k );
typedef int ( *PfnAes256GcmDecrypt )(
        unsigned char* m, unsigned long long* mlen_p,
        unsigned char* nsec,
        const unsigned char* c, unsigned long long clen,
        const unsigned char* ad, unsigned long long adlen,
        const unsigned char* npub,
        const unsigned char* k );
typedef void ( *PfnRandombytesBuf )( void* buf, size_t size );

} // anonymous namespace


////////////////////////////////////////////////
//  CRC32C (Castagnoli) — table-driven, portable

static const ULONG dwCRC32_LOOKUP_TABLE [256] =
{
     0x00000000, 0xF26B8303, 0xE13B70F7, 0x1350F3F4, 0xC79A971F, 0x35F1141C, 0x26A1E7E8, 0xD4CA64EB,
     0x8AD958CF, 0x78B2DBCC, 0x6BE22838, 0x9989AB3B, 0x4D43CFD0, 0xBF284CD3, 0xAC78BF27, 0x5E133C24,
     0x105EC76F, 0xE235446C, 0xF165B798, 0x030E349B, 0xD7C45070, 0x25AFD373, 0x36FF2087, 0xC494A384,
     0x9A879FA0, 0x68EC1CA3, 0x7BBCEF57, 0x89D76C54, 0x5D1D08BF, 0xAF768BBC, 0xBC267848, 0x4E4DFB4B,
     0x20BD8EDE, 0xD2D60DDD, 0xC186FE29, 0x33ED7D2A, 0xE72719C1, 0x154C9AC2, 0x061C6936, 0xF477EA35,
     0xAA64D611, 0x580F5512, 0x4B5FA6E6, 0xB93425E5, 0x6DFE410E, 0x9F95C20D, 0x8CC531F9, 0x7EAEB2FA,
     0x30E349B1, 0xC288CAB2, 0xD1D83946, 0x23B3BA45, 0xF779DEAE, 0x05125DAD, 0x1642AE59, 0xE4292D5A,
     0xBA3A117E, 0x4851927D, 0x5B016189, 0xA96AE28A, 0x7DA08661, 0x8FCB0562, 0x9C9BF696, 0x6EF07595,
     0x417B1DBC, 0xB3109EBF, 0xA0406D4B, 0x522BEE48, 0x86E18AA3, 0x748A09A0, 0x67DAFA54, 0x95B17957,
     0xCBA24573, 0x39C9C670, 0x2A993584, 0xD8F2B687, 0x0C38D26C, 0xFE53516F, 0xED03A29B, 0x1F682198,
     0x5125DAD3, 0xA34E59D0, 0xB01EAA24, 0x42752927, 0x96BF4DCC, 0x64D4CECF, 0x77843D3B, 0x85EFBE38,
     0xDBFC821C, 0x2997011F, 0x3AC7F2EB, 0xC8AC71E8, 0x1C661503, 0xEE0D9600, 0xFD5D65F4, 0x0F36E6F7,
     0x61C69362, 0x93AD1061, 0x80FDE395, 0x72966096, 0xA65C047D, 0x5437877E, 0x4767748A, 0xB50CF789,
     0xEB1FCBAD, 0x197448AE, 0x0A24BB5A, 0xF84F3859, 0x2C855CB2, 0xDEEEDFB1, 0xCDBE2C45, 0x3FD5AF46,
     0x7198540D, 0x83F3D70E, 0x90A324FA, 0x62C8A7F9, 0xB602C312, 0x44694011, 0x5739B3E5, 0xA55230E6,
     0xFB410CC2, 0x092A8FC1, 0x1A7A7C35, 0xE811FF36, 0x3CDB9BDD, 0xCEB018DE, 0xDDE0EB2A, 0x2F8B6829,
     0x82F63B78, 0x709DB87B, 0x63CD4B8F, 0x91A6C88C, 0x456CAC67, 0xB7072F64, 0xA457DC90, 0x563C5F93,
     0x082F63B7, 0xFA44E0B4, 0xE9141340, 0x1B7F9043, 0xCFB5F4A8, 0x3DDE77AB, 0x2E8E845F, 0xDCE5075C,
     0x92A8FC17, 0x60C37F14, 0x73938CE0, 0x81F80FE3, 0x55326B08, 0xA759E80B, 0xB4091BFF, 0x466298FC,
     0x1871A4D8, 0xEA1A27DB, 0xF94AD42F, 0x0B21572C, 0xDFEB33C7, 0x2D80B0C4, 0x3ED04330, 0xCCBBC033,
     0xA24BB5A6, 0x502036A5, 0x4370C551, 0xB11B4652, 0x65D122B9, 0x97BAA1BA, 0x84EA524E, 0x7681D14D,
     0x2892ED69, 0xDAF96E6A, 0xC9A99D9E, 0x3BC21E9D, 0xEF087A76, 0x1D63F975, 0x0E330A81, 0xFC588982,
     0xB21572C9, 0x407EF1CA, 0x532E023E, 0xA145813D, 0x758FE5D6, 0x87E466D5, 0x94B49521, 0x66DF1622,
     0x38CC2A06, 0xCAA7A905, 0xD9F75AF1, 0x2B9CD9F2, 0xFF56BD19, 0x0D3D3E1A, 0x1E6DCDEE, 0xEC064EED,
     0xC38D26C4, 0x31E6A5C7, 0x22B65633, 0xD0DDD530, 0x0417B1DB, 0xF67C32D8, 0xE52CC12C, 0x1747422F,
     0x49547E0B, 0xBB3FFD08, 0xA86F0EFC, 0x5A048DFF, 0x8ECEE914, 0x7CA56A17, 0x6FF599E3, 0x9D9E1AE0,
     0xD3D3E1AB, 0x21B862A8, 0x32E8915C, 0xC083125F, 0x144976B4, 0xE622F5B7, 0xF5720643, 0x07198540,
     0x590AB964, 0xAB613A67, 0xB831C993, 0x4A5A4A90, 0x9E902E7B, 0x6CFBAD78, 0x7FAB5E8C, 0x8DC0DD8F,
     0xE330A81A, 0x115B2B19, 0x020BD8ED, 0xF0605BEE, 0x24AA3F05, 0xD6C1BC06, 0xC5914FF2, 0x37FACCF1,
     0x69E9F0D5, 0x9B8273D6, 0x88D28022, 0x7AB90321, 0xAE7367CA, 0x5C18E4C9, 0x4F48173D, 0xBD23943E,
     0xF36E6F75, 0x0105EC76, 0x12551F82, 0xE03E9C81, 0x34F4F86A, 0xC69F7B69, 0xD5CF889D, 0x27A40B9E,
     0x79B737BA, 0x8BDCB4B9, 0x988C474D, 0x6AE7C44E, 0xBE2DA0A5, 0x4C4623A6, 0x5F16D052, 0xAD7D5351
};

BOOL g_fProcessorSupportsCRC32 = fFalse;

void OSInitializeProcessorSupportsCRC32()
{
    g_fProcessorSupportsCRC32 = fFalse;
}

ULONG Crc32Checksum( _In_reads_bytes_(cbData) const BYTE *pbData,
                     _In_                     ULONG     cbData )
{
    ULONG crc = 0xffffffff;
    for ( ; cbData > 0; cbData-- )
    {
        crc = dwCRC32_LOOKUP_TABLE[ BYTE(crc ^ *pbData++) ] ^ (crc >> 8);
    }
    return crc ^ 0xffffffff;
}


////////////////////////////////////////////////
//  libsodium dlopen + lifecycle
//
//  The library is loaded lazily on first use (pthread_once-gated).
//  If any step fails — dlopen, dlsym, sodium_init(), or the
//  AES-NI / CryptoExtension probe — g_fEncryptionAvailable stays
//  fFalse and all three AES entry points return
//  JET_errFeatureNotAvailable.  No engine-visible side effect on
//  boxes without libsodium.

namespace
{

struct SodiumProvider
{
    PfnSodiumInit pfnInit;
    PfnAes256GcmIsAvailable pfnIsAvailable;
    PfnAes256GcmEncrypt pfnEncrypt;
    PfnAes256GcmDecrypt pfnDecrypt;
    PfnRandombytesBuf pfnRandombytesBuf;
};

pthread_once_t g_onceSodium = PTHREAD_ONCE_INIT;
void* g_phSodium = nullptr;
SodiumProvider g_sodium = { nullptr, nullptr, nullptr, nullptr, nullptr };
BOOL g_fEncryptionAvailable = fFalse;

void SodiumInitOnce()
{
    //  Candidate library names, in order of preference:
    //
    //    1. libsodium.so.26.4.0 — the fully-versioned real file of the
    //       1.0.22 build our ExternalProject ships next to libese.so.
    //       We load it by its real name rather than the libsodium.so.26
    //       / libsodium.so symlinks so a deployment that ships only the
    //       versioned file (no dev symlinks) still resolves.  This is the
    //       build with the AArch64 AES path; older versions on aarch64
    //       fail the crypto_aead_aes256gcm_is_available() check.
    //
    //    2. libsodium.so.26 — system SOname of libsodium >= 1.0.20
    //       (Debian trixie / Ubuntu 25.04+ / recent Fedora), used only
    //       as a fallback when our staged build isn't present.
    //
    //    3. libsodium.so.23 — pre-1.0.20 system SOname (Ubuntu 24.04,
    //       Debian 12).  Works on x86 with AES-NI; on aarch64 the
    //       availability check fails and we return FNA at the
    //       call-site level.
    static const char* const sodiumSonames[] =
    {
        "libsodium.so.26.4.0",
        "libsodium.so.26",
        "libsodium.so.23",
    };
    for ( const char* soname : sodiumSonames )
    {
        g_phSodium = dlopen( soname, RTLD_LAZY | RTLD_LOCAL );
        if ( g_phSodium != nullptr )
        {
            break;
        }
    }
    if ( g_phSodium == nullptr )
    {
        return;
    }

    g_sodium.pfnInit = (PfnSodiumInit)dlsym( g_phSodium, "sodium_init" );
    g_sodium.pfnIsAvailable = (PfnAes256GcmIsAvailable)dlsym( g_phSodium, "crypto_aead_aes256gcm_is_available" );
    g_sodium.pfnEncrypt = (PfnAes256GcmEncrypt)dlsym( g_phSodium, "crypto_aead_aes256gcm_encrypt" );
    g_sodium.pfnDecrypt = (PfnAes256GcmDecrypt)dlsym( g_phSodium, "crypto_aead_aes256gcm_decrypt" );
    g_sodium.pfnRandombytesBuf = (PfnRandombytesBuf)dlsym( g_phSodium, "randombytes_buf" );

    if ( !g_sodium.pfnInit || !g_sodium.pfnIsAvailable ||
         !g_sodium.pfnEncrypt || !g_sodium.pfnDecrypt || !g_sodium.pfnRandombytesBuf )
    {
        return;
    }

    if ( g_sodium.pfnInit() < 0 )
    {
        return;
    }
    if ( g_sodium.pfnIsAvailable() == 0 )
    {
        //  No AES-NI on x86_64 or no ARM CryptoExtension on aarch64.
        //  Software fallback is intentionally not provided — bail
        //  the same as if the library were missing.
        return;
    }

    g_fEncryptionAvailable = fTrue;
}

void SodiumEnsureInit()
{
    pthread_once( &g_onceSodium, SodiumInitOnce );
}

} // anonymous namespace

BOOL FOSEncryptionPreinit()
{
    OSInitializeProcessorSupportsCRC32();
    SodiumEnsureInit();
    return fTrue;
}

void OSEncryptionPostterm() {}
void OSEncryptionTerm()     {}
ERR  ErrOSEncryptionInit()
{
    //  Always succeed.  If libsodium is missing (or its aes256gcm
    //  isn't available on this arch — e.g. libsodium <= 1.0.18 on
    //  aarch64), encryption-using call sites get
    //  JET_errFeatureNotAvailable from ErrOSCreateAes256Key /
    //  Encrypt / Decrypt; the engine continues working for any
    //  consumer that doesn't enable at-rest encryption.
    SodiumEnsureInit();
    return JET_errSuccess;
}


////////////////////////////////////////////////
//  AES-256-GCM surface, backed by libsodium when available

#include <pshpack1.h>
struct AES256KEY
{
    BYTE                            Version;
    UnalignedLittleEndian<ULONG>    Checksum;
    BYTE                            pbKey[0];
};
//  Trailer appended to ciphertext: same layout as the upstream Win32
//  build (1-byte Version + 16-byte InitVector), but only the first 12
//  bytes of InitVector are used (the GCM nonce).
struct AES256BLOBTRAILER
{
    BYTE                            Version;
    BYTE                            InitVector[ 16 ];
};
#include <poppack.h>

ULONG CbOSEncryptAes256SizeNeeded( ULONG cbDataLen )
{
    //  GCM is a stream cipher mode: ciphertext is the same length as
    //  plaintext.  We append the 16-byte auth tag and the trailer.
    return cbDataLen + (ULONG)kAes256GcmAbytes + (ULONG)sizeof( AES256BLOBTRAILER );
}

ERR ErrOSEncryptionVerifyKey(
    _In_reads_bytes_(cbKey) const   BYTE *pbKey,
    _In_                            ULONG cbKey )
{
    AES256KEY * pKey = (AES256KEY *)pbKey;
    if ( cbKey < sizeof(AES256KEY) ||
         pKey->Version != JET_EncryptionAlgorithmAes256 ||
         pKey->Checksum != Crc32Checksum( pKey->pbKey, cbKey - sizeof(AES256KEY) ) )
    {
        return ErrERRCheck( JET_errInvalidParameter );
    }
    return JET_errSuccess;
}

ERR ErrOSCreateAes256Key(
    _Out_writes_bytes_to_opt_(*pcbKeySize, *pcbKeySize) BYTE *  pbKey,
    _Inout_                                             ULONG * pcbKeySize )
{
    SodiumEnsureInit();
    if ( !g_fEncryptionAvailable )
    {
        if ( pcbKeySize )
        {
            *pcbKeySize = 0;
        }
        return ErrERRCheck( JET_errFeatureNotAvailable );
    }

    const ULONG cbNeeded = (ULONG)sizeof( AES256KEY ) + (ULONG)kAes256GcmKeyBytes;

    //  Caller protocol matches the Win32 path: pass *pcbKeySize < cbNeeded
    //  (or pbKey == NULL) to query the required buffer size; we write
    //  cbNeeded back and return JET_errBufferTooSmall.
    if ( pbKey == nullptr || *pcbKeySize < cbNeeded )
    {
        *pcbKeySize = cbNeeded;
        return ErrERRCheck( JET_errBufferTooSmall );
    }

    AES256KEY * const pKey = (AES256KEY *)pbKey;
    g_sodium.pfnRandombytesBuf( pKey->pbKey, kAes256GcmKeyBytes );
    pKey->Version  = JET_EncryptionAlgorithmAes256;
    pKey->Checksum = Crc32Checksum( pKey->pbKey, (ULONG)kAes256GcmKeyBytes );
    *pcbKeySize    = cbNeeded;

#ifdef DEBUG
    CallS( ErrOSEncryptionVerifyKey( pbKey, *pcbKeySize ) );
#endif

    return JET_errSuccess;
}

ERR ErrOSEncryptWithAes256(
    _Inout_updates_bytes_to_(cbDataBufLen, *pcbDataLen)     BYTE *  pbData,
    _Inout_                                                 ULONG * pcbDataLen,
    _In_                                                    ULONG   cbDataBufLen,
    _In_reads_bytes_(cbKey)                         const   BYTE *  pbKey,
    _In_                                                    ULONG   cbKey )
{
    ERR err = JET_errSuccess;

    SodiumEnsureInit();
    if ( !g_fEncryptionAvailable )
    {
        return ErrERRCheck( JET_errFeatureNotAvailable );
    }

    CallR( ErrOSEncryptionVerifyKey( pbKey, cbKey ) );
    if ( cbKey - sizeof( AES256KEY ) < kAes256GcmKeyBytes )
    {
        return ErrERRCheck( JET_errInvalidParameter );
    }

    const ULONG cbPlaintext = *pcbDataLen;
    const ULONG cbNeeded    = CbOSEncryptAes256SizeNeeded( cbPlaintext );
    if ( cbNeeded > cbDataBufLen )
    {
        *pcbDataLen = cbNeeded;
        return ErrERRCheck( JET_errBufferTooSmall );
    }

    //  Build the trailer first so we can hand its InitVector to GCM
    //  as the nonce.  Only the first 12 bytes are meaningful for
    //  AES-GCM; zero the remainder so the trailer bits are
    //  deterministic on disk.
    AES256BLOBTRAILER trailer;
    memset( &trailer, 0, sizeof( trailer ) );
    trailer.Version = JET_EncryptionAlgorithmAes256;
    g_sodium.pfnRandombytesBuf( trailer.InitVector, kAes256GcmNonceBytes );

    AES256KEY * const pKey = (AES256KEY *)pbKey;

    //  In-place encrypt: writes ciphertext + tag (cbPlaintext +
    //  kAes256GcmAbytes bytes) starting at pbData.  libsodium
    //  documents that c == m (overlap) is supported.
    unsigned long long clen = 0;
    if ( g_sodium.pfnEncrypt(
                pbData, &clen,
                pbData, cbPlaintext,
                /* ad   */ nullptr, 0,
                /* nsec */ nullptr,
                /* npub */ trailer.InitVector,
                /* k    */ pKey->pbKey ) != 0 )
    {
        return ErrERRCheck( JET_errInternalError );
    }
    Assert( clen == (unsigned long long)( cbPlaintext + kAes256GcmAbytes ) );

    //  Append the trailer after the ciphertext+tag block.
    memcpy( pbData + clen, &trailer, sizeof( trailer ) );
    *pcbDataLen = (ULONG)clen + (ULONG)sizeof( AES256BLOBTRAILER );

    Assert( *pcbDataLen == cbNeeded );
    Assert( *pcbDataLen <= cbDataBufLen );

    return err;
}

ERR ErrOSDecryptWithAes256(
    _In_reads_( *pcbDataLen )                           BYTE *  pbDataIn,
    _Out_writes_bytes_to_(*pcbDataLen, *pcbDataLen)     BYTE *  pbDataOut,
    _Inout_                                             ULONG * pcbDataLen,
    _In_reads_bytes_(cbKey)                     const   BYTE *  pbKey,
    _In_                                                ULONG   cbKey )
{
    ERR err = JET_errSuccess;

    SodiumEnsureInit();
    if ( !g_fEncryptionAvailable )
    {
        return ErrERRCheck( JET_errFeatureNotAvailable );
    }

    CallR( ErrOSEncryptionVerifyKey( pbKey, cbKey ) );
    if ( cbKey - sizeof( AES256KEY ) < kAes256GcmKeyBytes )
    {
        return ErrERRCheck( JET_errInvalidParameter );
    }

    //  Minimum size = auth tag (16) + trailer (17).  Anything below
    //  that can't possibly be valid GCM ciphertext from our encrypt
    //  path.
    if ( *pcbDataLen < kAes256GcmAbytes + sizeof( AES256BLOBTRAILER ) )
    {
        return ErrERRCheck( JET_errInvalidParameter );
    }

    const AES256BLOBTRAILER * const pTrailer =
        (const AES256BLOBTRAILER *)( pbDataIn + *pcbDataLen ) - 1;
    if ( pTrailer->Version != JET_EncryptionAlgorithmAes256 )
    {
        return ErrERRCheck( JET_errInvalidParameter );
    }

    const ULONG cbCipherAndTag = *pcbDataLen - (ULONG)sizeof( AES256BLOBTRAILER );
    AES256KEY * const pKey     = (AES256KEY *)pbKey;

    //  libsodium accepts c == m for overlap, but the caller hands us
    //  separate in/out buffers — copy then decrypt-in-place on the
    //  output, which still satisfies GCM's auth-before-release
    //  guarantee since it tag-verifies before writing the plaintext
    //  into the buffer.
    memcpy( pbDataOut, pbDataIn, cbCipherAndTag );

    unsigned long long mlen = 0;
    if ( g_sodium.pfnDecrypt(
                pbDataOut, &mlen,
                /* nsec */ nullptr,
                pbDataOut, cbCipherAndTag,
                /* ad   */ nullptr, 0,
                /* npub */ pTrailer->InitVector,
                /* k    */ pKey->pbKey ) != 0 )
    {
        return ErrERRCheck( JET_errDecryptionFailed );
    }

    *pcbDataLen = (ULONG)mlen;
    return err;
}
