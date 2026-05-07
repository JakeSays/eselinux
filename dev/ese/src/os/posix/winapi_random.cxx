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

}  // extern "C"
