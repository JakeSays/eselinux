// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Win32-equivalent rand()/srand(). On Windows, RAND_MAX == 0x7FFF and
// rand() returns 0..32767. Glibc returns 0..0x7FFFFFFF. The engine's
// DwRand() in os/time.cxx C_ASSERTs the Win32 contract and bit-shifts
// three 15-bit values into a uniform 32-bit number. Implement the
// Win32 LCG (same constants Microsoft's CRT uses) so the bit width
// matches.

#include "osstd.hxx"

#include <errno.h>
#include <sys/random.h>
#include <unistd.h>

// osstd.hxx already #defines rand→osposix_rand / srand→osposix_srand.
// Drop those macros for this translation unit so we can name the
// definitions without recursive expansion.
#undef rand
#undef srand

namespace
{
    thread_local unsigned long t_seed = 1;
}

extern "C" {

int osposix_rand( void )
{
    t_seed = t_seed * 214013UL + 2531011UL;
    return static_cast<int>( ( t_seed >> 16 ) & 0x7FFF );
}

void osposix_srand( unsigned int seed )
{
    t_seed = seed;
}

//  rand_s — MSVC secure-CRT cryptographically-strong random.  cc.hxx
//  declares it `extern "C"` (no body), and the static-inline body in
//  windows-shim/windows.h is only visible to TUs that include the shim
//  AFTER cc.hxx.  In Release that visibility window can collapse, leaving
//  libese.so with an undefined `rand_s` reference at link time.  Provide
//  the strong out-of-line definition here so the shape is uniform across
//  optimization levels.
int rand_s( unsigned int* pui )
{
    if ( !pui )
        return EINVAL;
    ssize_t n;
    do
    {
        n = getrandom( pui, sizeof( *pui ), 0 );
    }
    while ( n < 0 && errno == EINTR );
    return ( n == (ssize_t) sizeof( *pui ) ) ? 0 : errno;
}

}  // extern "C"
