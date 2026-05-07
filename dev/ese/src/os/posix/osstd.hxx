// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

// SYSTEM (Linux/POSIX) ************************************************
//
// Precompiled-header host for the osposix static library. Mirrors the role
// of os/litent/osstd.hxx and os/winnt/osstd.hxx.
//
// During Phase 1 of the Linux port the goal is configure-clean, not
// compile-clean, so this file only forwards to the shared master header.
// Real Linux-specific includes will accumulate here as the OS layer ports
// in Phase 5.

#include "osstd_.hxx"

// Win32 rand() returns a 15-bit value (RAND_MAX == 32767); glibc rand()
// returns 31 bits. Engine code in time.cxx C_ASSERTs the Win32 contract
// and combines three rand() calls expecting 15-bit output. Shim into a
// Win32-equivalent rand/srand defined in winapi_random.cxx.
#ifdef RAND_MAX
#undef RAND_MAX
#endif
#define RAND_MAX 0x7FFF
#define rand     osposix_rand
#define srand    osposix_srand
extern "C" int  osposix_rand( void );
extern "C" void osposix_srand( unsigned int seed );
