# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

cmake_minimum_required (VERSION 3.19)
project (ese)

# Keeping at verbose while the project build files are still in flux
set(CMAKE_MESSAGE_LOG_LEVEL "VERBOSE")
message(STATUS "[ CXX Default Flags ] = ${CMAKE_CXX_FLAGS}")
message(STATUS "[ CXX Debug Flags   ] = ${CMAKE_CXX_FLAGS_DEBUG}")
message(STATUS "[ CXX Release Flags ] = ${CMAKE_CXX_FLAGS_RELEASE}")

# CMake debug configuration: these symbols are expected to be defined
add_compile_options("$<$<CONFIG:Debug>:-DDEBUG>")
add_compile_options("$<$<CONFIG:Debug>:-DDBG=1>")
add_compile_options("$<$<CONFIG:Debug>:-DDEBUG_BUILD>")
add_compile_options("$<$<CONFIG:Debug>:-DMEM_CHECK>")
add_compile_options("$<$<CONFIG:Debug>:-DNDEBUG>")

# CMake release configuration: these symbols are expected to be defined.
# Point of order: In the code, for historical/internal reasons, we often
# refer to the `Release` configuration as `Retail`
add_compile_options("$<$<CONFIG:Release>:-DRTM>")
add_compile_options("$<$<CONFIG:Release>:-DNDEBUG>")

# 64-bit symbols for Visual Studio C++
if("${CMAKE_GENERATOR}" MATCHES "Visual Studio")
    if("${CMAKE_GENERATOR_PLATFORM}" MATCHES "x64")
        message(STATUS "NOTE: Setting 64-bit symbols")
        add_compile_definitions(
            _AMD64_
            _AMD64_SIMULATOR_
            _AMD64_SIMULATOR_PERF_
            _AMD64_WORKAROUND_
            _SKIP_IF_SIMULATOR_
            _WIN64
            AMD64
        )
    elseif("${CMAKE_GENERATOR_PLATFORM}" MATCHES "Win32")
        message(STATUS "NOTE: Setting 32-bit symbols")
        add_compile_definitions(
            _X86_=1
            FPO=0
            i386=1
            STD_CALL
        )
    else()
        message(FATAL_ERROR "NOTE: NOT setting 64-bit OR 32-bit symbols")
    endif()
elseif(UNIX AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(STATUS "NOTE: Setting 64-bit symbols (Linux x86_64)")
    add_compile_definitions(
        _AMD64_
        AMD64
    )
else()
    message(FATAL_ERROR "NOTE: NOT setting 64-bit OR 32-bit symbols")
endif()

# Set certain CMake path variables
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Generated build artifacts path shortcut
set(GEN_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/gen)

# The publish headers path shortcut
set(INC_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/inc)

# Ensure the above paths exist
file(MAKE_DIRECTORY ${GEN_OUTPUT_DIRECTORY})
file(MAKE_DIRECTORY ${INC_OUTPUT_DIRECTORY})
file(MAKE_DIRECTORY ${INC_OUTPUT_DIRECTORY}/jet)

# Source path shortcuts
set(ESE_DEV ${CMAKE_SOURCE_DIR}/dev/ese)
set(ESE_TEST ${CMAKE_SOURCE_DIR}/test/ese)

# C++ standard for the Linux/Unix build
if (UNIX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
endif()

# Compiler-specific project-wide flags and settings
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # using GCC or Clang (Clang is the supported compiler for the Linux port)

    # The Linux port uses the LLVM toolchain end-to-end: clang as compiler,
    # libc++ as the C++ standard library, libc++abi for ABI bits, and
    # libunwind for stack unwinding. Avoids any GCC/libstdc++ dependency.
    #
    # Statically link the C++ runtime so binaries don't need clang's lib
    # dir on LD_LIBRARY_PATH at run time. We have to use the right archive
    # variant per target type:
    #   - executables: libc++.a / libc++abi.a / libunwind.a
    #   - shared libs: libc++_pic.a / libc++abi_pic.a / libunwind_pic.a
    # clang's `-static-libstdc++` always picks the non-PIC archive, which
    # fails when linking a .so (relocation type errors), so we wire the
    # archives in by hand via generator expressions.
    if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        add_compile_options(-stdlib=libc++)
        # Use LLVM's lld linker (matches the rest of the toolchain — clang
        # compiler, libc++, compiler-rt, libunwind). Avoids relying on the
        # system's GNU ld and its compatibility quirks.
        # `-nostdlib++` keeps clang's driver from adding `-lc++` (which would
        # pull in the .so even though we link the .a archives by hand below).
        add_link_options(-fuse-ld=lld -stdlib=libc++ -rtlib=compiler-rt -unwindlib=libunwind -nostdlib++)

        # Resolve the toolchain's per-target lib dir from the compiler.
        # `-print-resource-dir` returns `<root>/lib/clang/<ver>`; the runtime
        # archives live at `<root>/lib/<triple>/`.
        execute_process(
            COMMAND ${CMAKE_CXX_COMPILER} -print-resource-dir
            OUTPUT_VARIABLE _llvm_resource_dir
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        execute_process(
            COMMAND ${CMAKE_CXX_COMPILER} -print-target-triple
            OUTPUT_VARIABLE _llvm_target_triple
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        get_filename_component(_llvm_lib_root "${_llvm_resource_dir}/../.." ABSOLUTE)
        set(_llvm_libdir "${_llvm_lib_root}/${_llvm_target_triple}")
        if (NOT EXISTS "${_llvm_libdir}/libc++.a")
            message(FATAL_ERROR
                "libc++.a not found at ${_llvm_libdir}; "
                "expected the LLVM toolchain to provide static archives there.")
        endif ()

        # libc++ depends on libc++abi which depends on libunwind; the
        # `-Wl,--start-group/--end-group` pair lets the linker resolve any
        # cycles between them.
        link_libraries(
            -Wl,--start-group
            $<IF:$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>,${_llvm_libdir}/libc++_pic.a,${_llvm_libdir}/libc++.a>
            $<IF:$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>,${_llvm_libdir}/libc++abi_pic.a,${_llvm_libdir}/libc++abi.a>
            $<IF:$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>,${_llvm_libdir}/libunwind_pic.a,${_llvm_libdir}/libunwind.a>
            -Wl,--end-group
        )

    endif ()

    # -g Enables extra debugging info for GDB
    # NOTE: if you wish to debug a core dump (instead of debugging a running program).
    # you will need to run `ulimit -c unlimited` first, to enable core dump generation.
    add_compile_options(-g)

    # Stop after the first error
    # This can be removed once the Linux build is fully working
    if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        add_compile_options(-ferror-limit=1)
    else ()
        add_compile_options(-fmax-errors=1)
    endif ()

    # WCHAR is 16-bit on the Linux build to match the engine's existing wide-string
    # semantics; equivalent to MSVC's /Zc:wchar_t-. Project-wide.
    add_compile_options(-fshort-wchar)

    # NOTE: UNICODE / _UNICODE were previously set project-wide here. The
    # upstream Windows OS layer (oswinnt target) does NOT define _UNICODE,
    # so _TCHAR = char on its build, and OS call sites pass narrow string
    # literals (e.g. `(*pcprintf)( "Cache Header:\n" )`,
    # `CSyncBasicInfo( _T("...") )`). Defining _UNICODE project-wide on
    # the Linux build broke those sites. _UNICODE is now scoped to the
    # specific targets that need it.

    # Symbols are hidden by default; only the JET API entry points get explicit
    # default visibility (matches the role of esent.def on Windows).
    add_compile_options(-fvisibility=hidden -fvisibility-inlines-hidden)

    # No C++ exceptions and no RTTI: code is built with _HAS_EXCEPTIONS=0 and the
    # MSVC arm uses /GR-.
    add_compile_options(-fno-exceptions -fno-rtti)

    # Position-independent code so we can produce libese.so.
    add_compile_options(-fPIC)

    # Accept Microsoft language extensions: __int64, __forceinline, __declspec,
    # __cdecl/__stdcall, etc. These are pervasive in the engine's published
    # headers and inline atomics; replacing each call site would touch hundreds
    # of files. Clang implements them natively under -fms-extensions.
    #
    # -fms-compatibility goes further: it tells clang's frontend to mimic
    # MSVC's permissive parsing rules — accepting things like out-of-line
    # member definitions on typedefs of template instantiations, missing
    # `typename` in dependent contexts, etc. The engine relies on these
    # MSVC laxities pervasively (e.g. blockcache hash tables).
    #
    # -fms-compatibility-version pins the MSVC version we emulate. The
    # default is too old (pre-C++11 char16_t is a non-keyword), which
    # breaks libc++ headers. 19.40 ≈ VS 2022 17.10 — covers the range
    # the engine itself targets.
    add_compile_options(-fms-extensions -fms-compatibility -fms-compatibility-version=19.40 -fdeclspec)

    # Engine headers were authored against MSVC's lazy template parsing
    # (e.g. dht.hxx references Assert/AssertSz that consumers redefine).
    # -fdelayed-template-parsing defers definition-time name lookup in template
    # bodies until instantiation, matching MSVC's behaviour.
    add_compile_options(-fdelayed-template-parsing)

    # MSVC does not treat C++ alternative tokens (`and`, `or`, `xor`, ...)
    # as keywords, and the engine has identifiers that collide with them
    # (e.g. `const ULONG xor = ...` in checksum_amd64.cxx). -fno-operator-names
    # disables clang's recognition so those identifiers parse.
    add_compile_options(-fno-operator-names)

    # Headers like checksumstd.hxx unconditionally include <tchar.h>, <windows.h>,
    # etc. windows-shim/ provides minimal stand-ins so any TU can include them
    # without each subdirectory having to wire up its own include path.
    include_directories(${CMAKE_SOURCE_DIR}/windows-shim)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    # Using Microsoft Visual C++

    add_compile_options(
        /W4 # Show level-4 lint-like informational warnings
        /WX # warnings as errors

        # Suppress certain warnings by number
        /wd4267 # 'var': conversion from 'size_t' to 'type', possible loss of data
        /wd4996 # deprecated declaration
        /wd4984 # using c++17 "if constexpr( xx )" syntax
        /wd4706 # assignment within a conditional expression
        /wd4291 # no matching operator delete found
        /wd4714 # function not inlined - TODO: add it back

        /Wv:19.24 # Force compiler warnings later than v19.24 to not be shown
    )
endif()

# Locate the Message Compiler. On Windows this is the Windows SDK mc.exe.
# On Linux it is the native-AOT replacement built from tools/mc/ — see
# tools/mc/README.md for the install instructions.
if (WIN32)
    find_program(CMAKE_MESSAGE_COMPILER mc.exe HINTS "${sdk_bindir}")
    if (NOT CMAKE_MESSAGE_COMPILER)
        message(FATAL_ERROR "message compiler not found: required to build")
    else ()
        message(STATUS "SUCCESS: Found Message Compiler: '${CMAKE_MESSAGE_COMPILER}'")
    endif (NOT CMAKE_MESSAGE_COMPILER)
else ()
    find_program(CMAKE_MESSAGE_COMPILER NAMES mc HINTS "$ENV{HOME}/bin")
    if (NOT CMAKE_MESSAGE_COMPILER)
        message(FATAL_ERROR "message compiler not found: build and install tools/mc to ~/bin (see tools/mc/README.md)")
    else ()
        message(STATUS "SUCCESS: Found Message Compiler: '${CMAKE_MESSAGE_COMPILER}'")
    endif ()
endif (WIN32)

if (MSVC)
    set(ESE_DEBUG_LIBRARIES
        debug ucrtd
        debug vcruntimed
        debug msvcrtd
        debug msvcprtd
    )

    set(ESE_RELEASE_LIBRARIES
        optimized ucrt
        optimized vcruntime
        optimized msvcrt
        optimized msvcprt
    )

    link_libraries(
        ${ESE_DEBUG_LIBRARIES}
        ${ESE_RELEASE_LIBRARIES}
    )
endif()

if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    # Using Microsoft Visual C++
    add_compile_options(
        /Oi # generate intrinsic functions
        /Ot
        /GL # whole program optimization
        # IMPORTANT: `library.cxx` reference-compares the strings defined in
        # the shared header `osstd_.hxx`, and string pooling is necessary for
        # the comparison to succeed. Without string pooling, ese.dll will fail
        # to load at runtime: watch out for NTSTATUS error 0xC0000142.
        /GF # Pool strings.
        /Gm-
        /MD
        /Zp8
        /GS
        /Gy # function-level linking
        /Zc:wchar_t-
        /Zc:forScope
        /Zc:inline-
        /GR-
        /TP
        /FC
        /Zl
        /Zo # enhance optimized debugging (previously, `/d2Zi+`)
        /Z7
    )

    # Should be used with /GL (see above)
    add_link_options(
        /LTCG # link-time generation
        /INCREMENTAL:NO # incremental incompatible with /LTCG
    )
endif()

# Finally, in this project, we configure and build these subdirectories:
add_subdirectory(dev)

add_subdirectory(test)