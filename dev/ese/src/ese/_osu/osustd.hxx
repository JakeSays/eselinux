// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

//** SYSTEM **********************************************************

#pragma once

#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include <time.h>
#include <limits.h>
#include <minmax.h>
#include <ctype.h>

#include <specstrings.h>

#include <algorithm>
#include <functional>
using namespace std;

#ifndef ESE_COMPILER_MSVC
#ifndef _ESE_HETEROGENEOUS_MINMAX_DEFINED
#define _ESE_HETEROGENEOUS_MINMAX_DEFINED
//  Engine call sites use unqualified `min(a,b)` / `max(a,b)` with arguments
//  of different (but related) integer types. MSVC's <minwindef.h> exposes
//  min/max as macros that int-promote; libstdc++'s std::min<T> rejects
//  type-deduction conflict. Provide heterogeneous overloads in the global
//  namespace, gated by is_same so they never ambiguate the same-type std
//  overloads. Mirror of the definitions in os/osstd_.hxx.
#include <type_traits>
template < class A, class B,
           class = std::enable_if_t< !std::is_same_v< A, B > > >
constexpr auto min( A a, B b ) -> std::common_type_t< A, B >
{
    using T = std::common_type_t< A, B >;
    return T( a ) < T( b ) ? T( a ) : T( b );
}
template < class A, class B,
           class = std::enable_if_t< !std::is_same_v< A, B > > >
constexpr auto max( A a, B b ) -> std::common_type_t< A, B >
{
    using T = std::common_type_t< A, B >;
    return T( a ) > T( b ) ? T( a ) : T( b );
}
#endif
#endif

//** COMPILER CONTROL ************************************************

#pragma warning ( disable : 4127 )  //  conditional expression is constant
#pragma warning ( disable : 4200 )  //  we allow zero sized arrays
#pragma warning ( disable : 4201 )  //  we allow unnamed structs/unions
#pragma warning ( disable : 4355 )  //  we allow the use of this in ctor-inits
#pragma warning ( 3 : 4244 )        //  do not hide data truncations
#pragma inline_depth( 255 )
#pragma inline_recursion( on )

//** OSAL *************************************************************

#include "osu.hxx"              //  OS Abstraction Layer

//** JET API **********************************************************

#include "jet.h"                    //  Public JET API definitions


