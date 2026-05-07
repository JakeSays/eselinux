// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Win32 TLS API on top of pthread_key_t. Win32 TLS slots are identified
// by DWORD indices and are dense (TlsAlloc returns the lowest free
// index). pthread keys are similarly an opaque integer in glibc, so we
// use the pthread_key_t value directly as the DWORD index. The bound
// dtor is null — engine code releases per-thread state explicitly.

#include "osstd.hxx"

#include <pthread.h>

extern "C" {

DWORD TlsAlloc( void )
{
    pthread_key_t key;
    if ( pthread_key_create( &key, nullptr ) != 0 )
    {
        return TLS_OUT_OF_INDEXES;
    }
    return static_cast<DWORD>( key );
}

BOOL TlsFree( DWORD dwTlsIndex )
{
    return ( pthread_key_delete( static_cast<pthread_key_t>( dwTlsIndex ) ) == 0 ) ? TRUE : FALSE;
}

PVOID TlsGetValue( DWORD dwTlsIndex )
{
    return pthread_getspecific( static_cast<pthread_key_t>( dwTlsIndex ) );
}

BOOL TlsSetValue( DWORD dwTlsIndex, PVOID lpTlsValue )
{
    return ( pthread_setspecific( static_cast<pthread_key_t>( dwTlsIndex ), lpTlsValue ) == 0 ) ? TRUE : FALSE;
}

}  // extern "C"
