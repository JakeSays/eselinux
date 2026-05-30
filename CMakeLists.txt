# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

cmake_minimum_required (VERSION 3.20)
project (ese)

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
        # On Windows, NTOSFuncPtr (in os/library.hxx) builds each Win32 entry
        # point as a FunctionLoader<> that lazily LoadLibraryExW's kernel32.dll
        # and GetProcAddress's the symbol on first call. On Linux the shim'd
        # Win32 surface is statically linked into libese.so via osposix
        # (--whole-archive), so we want NTOSFuncPtr to collapse to a direct
        # `decltype(&func) g_pfn = func;` instead of going through the loader.
        # Without this, every g_pfn* call site fails with the fail-thunk
        # (returns NULL or sets GLE) and the engine reports JET_errOutOfMemory
        # — surfaced first by CFlushMap.BasicPersistedFlushMap's call to
        # ErrOSTimerTaskCreate -> CreateThreadpoolTimer.
        ENABLE_STATIC_COMPILED_LOAD_DEPENDENCIES
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
        # -stdlib=libc++ is C++-only; gating with $<COMPILE_LANGUAGE:CXX>
        # prevents Wunused-command-line-argument when clang is invoked on
        # the C sources in _xpress (xencode.c, xdecode.c).
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-stdlib=libc++>)
        # Use LLVM's lld linker (matches the rest of the toolchain — clang
        # compiler, libc++, compiler-rt, libunwind). Avoids relying on the
        # system's GNU ld and its compatibility quirks.
        # `-nostdlib++` keeps clang's driver from adding `-lc++` (which would
        # pull in the .so even though we link the .a archives by hand below).
        add_link_options(-fuse-ld=lld -stdlib=libc++ -rtlib=compiler-rt -unwindlib=libunwind -nostdlib++)

        # Resolve the toolchain's per-target lib dir from the compiler.
        # `-print-resource-dir` returns `<root>/lib/clang/<ver>`; the runtime
        # archives live at `<root>/lib/<triple>/`.
        #
        # The triple comes from CMAKE_CXX_COMPILER_TARGET when the toolchain
        # file set it (cross builds); otherwise we ask clang for its default
        # target (native builds).  `clang -print-target-triple` ignores any
        # target inferred from argv0 (e.g. aarch64-linux-gnu-clang++) and
        # always reports the compiler's *default* target — so on a cross
        # build it would wrongly return the host triple if we relied on it.
        # A toolchain file (e.g. the musl one) can override the libc++
        # location directly when the layout doesn't match the default
        # lib/<triple>/ scheme — musl libs live under lib-musl/lib/<triple>/.
        if (DEFINED ESE_LLVM_LIBDIR)
            set(_llvm_libdir "${ESE_LLVM_LIBDIR}")
        else ()
            execute_process(
                COMMAND ${CMAKE_CXX_COMPILER} -print-resource-dir
                OUTPUT_VARIABLE _llvm_resource_dir
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            if (CMAKE_CXX_COMPILER_TARGET)
                set(_llvm_target_triple "${CMAKE_CXX_COMPILER_TARGET}")
            else ()
                execute_process(
                    COMMAND ${CMAKE_CXX_COMPILER} -print-target-triple
                    OUTPUT_VARIABLE _llvm_target_triple
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                )
            endif ()
            get_filename_component(_llvm_lib_root "${_llvm_resource_dir}/../.." ABSOLUTE)
            set(_llvm_libdir "${_llvm_lib_root}/${_llvm_target_triple}")
        endif ()
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

        # Resolve where the toolchain ships <libunwind.h>.  The musl
        # toolchain pins this to its lib-musl/include tree explicitly;
        # for the glibc builds we infer it from the compiler path
        # (`<clang-root>/include`).  Both end up at LLVM's libunwind
        # header (NOT the GNU libunwind in /usr/include — those two
        # disagree on the `unw_*` symbol naming, ABI-incompatible).
        # osposix consumes this in its target_include_directories.
        if (NOT DEFINED ESE_LIBUNWIND_INCLUDE_DIR)
            get_filename_component(_clang_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
            get_filename_component(_clang_root "${_clang_bin}/.." ABSOLUTE)
            set(ESE_LIBUNWIND_INCLUDE_DIR "${_clang_root}/include")
        endif ()
        if (NOT EXISTS "${ESE_LIBUNWIND_INCLUDE_DIR}/libunwind.h")
            message(FATAL_ERROR
                "libunwind.h not found at ${ESE_LIBUNWIND_INCLUDE_DIR}; "
                "expected the LLVM toolchain to provide it there.")
        endif ()

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

    # Mandatory companion to -fshort-wchar.  clang's loop-idiom pass
    # pattern-matches hand-written wide-string loops (while (s[n]) ++n, etc.)
    # and rewrites them into calls to libc wcslen/wcscmp/...  But libc's
    # wcs* walk 4-byte wchar_t while ours are 2-byte, so the rewrite reads
    # past every other code unit and returns garbage.  The fold only fires
    # at -O1+, so it is invisible in Debug and corrupts wide strings (e.g.
    # instance names round-tripped through JetGetInstanceInfoW) in Release.
    # Disable the builtins everywhere -fshort-wchar applies — they are a
    # matched pair.  (libnls/osposix also set these locally; the duplicate
    # is harmless.)
    add_compile_options(
        -fno-builtin-wcslen
        -fno-builtin-wcscpy
        -fno-builtin-wcsncpy
        -fno-builtin-wcscat
        -fno-builtin-wcsncat
        -fno-builtin-wcscmp
        -fno-builtin-wcsncmp
        -fno-builtin-wcschr
        -fno-builtin-wcsrchr
        -fno-builtin-wcsstr
    )

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

    # -fms-compatibility suppresses __GNUC__ / __GNUC_STDC_INLINE__, which
    # glibc's sys/cdefs.h uses to gate `__extern_always_inline`.  Without
    # one of those macros, optimized builds (`__OPTIMIZE__` set at -O2+)
    # pull in glibc's fortify-flavoured inline declarations of memcpy /
    # memchr / etc. that reference an undefined `__extern_always_inline`,
    # blowing up string.h on the first include.  Predefining the GNU-STDC
    # inline marker satisfies cdefs.h's gate so the macro gets defined,
    # and disabling _FORTIFY_SOURCE keeps us off the bounds-checking
    # wrappers we don't rely on.
    add_compile_options(-D__GNUC_STDC_INLINE__=1)
    add_compile_options(-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0)

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

    # Warning-noise suppressions — each one is verified non-actionable on this
    # build (see Bucket A audit notes in the May 2026 port log). They fall
    # into three families:
    #
    #   1. Microsoft language extensions we deliberately enable via
    #      -fms-extensions / -fdeclspec / -fdelayed-template-parsing — the
    #      warnings flag every site that uses the extension, but the
    #      extension itself is what makes the engine compile at all.
    #
    #   2. Target-irrelevant attributes — `__stdcall` is meaningless on
    #      x86_64 Linux (single ABI), but the engine's published headers
    #      sprinkle it on every JET_API entry point.
    #
    #   3. C++20 deprecations of patterns the engine relies on (volatile
    #      ++/--, anonymous-enum bitwise combinations, replacement
    #      operator new declared inline) — safe in practice; converting
    #      to the modern equivalent is an out-of-scope refactor.
    #
    # `-Wpragma-pack` itself is dropped because it fires on every balanced
    # `<pshpack*.h>` include and doesn't distinguish actual leaks from
    # the standard pshpack/poppack pattern. `-Wpragma-pack-suspicious-include`
    # is the precise leak detector and is enabled in its place.
    add_compile_options(
        -Wno-microsoft-goto
        -Wno-ignored-attributes
        -Wno-pragma-pack
        -Wno-invalid-offsetof
        -Wno-delayed-template-parsing-in-cxx20
        -Wno-deprecated-volatile
        -Wno-inconsistent-missing-override
        -Wno-inline-new-delete
        -Wno-microsoft-string-literal-from-predefined
        -Wno-microsoft-cast
        -Wno-microsoft-sealed
        -Wno-microsoft-enum-forward-reference
        -Wno-microsoft-extra-qualification
        -Wno-microsoft-include
        -Wno-deprecated-anon-enum-enum-conversion
        -Wno-duplicate-decl-specifier
        -Wno-missing-declarations
        -Wno-switch
        # The engine uses a banner style of block comment that nests
        # `/*` characters inside an outer /* ... */, e.g.
        #
        #     /*  SYSTEM errors
        #     /**/
        #
        # which clang flags as "/* within block comment". Valid C++
        # (the inner /* is just text inside the open comment), but
        # warning-noisy to the tune of ~900 sites.
        -Wno-comment
        # The published JET API spells string-pointer struct fields and
        # parameters as `char*` / `WCHAR*` (not const) for legacy ABI
        # reasons — Microsoft's own ESE compiles with the same types
        # and suppresses this warning. Engine code instantiates those
        # structs with string literals (e.g. `{ ..., "objid", ...}` in
        # cat.cxx) and clang fires Wwritable-strings on every literal.
        # Const-correcting would require diverging from the public
        # headers; the literals live in read-only memory and any
        # actual write attempt traps at runtime, so the warning's
        # "real-bug" value is low. Suppress.
        -Wno-writable-strings
        -Wpragma-pack-suspicious-include
        # blockcache headers use MSVC-style unqualified lookup into dependent
        # template bases pervasively; clang accepts it via -fms-extensions but
        # still warns. 143 hits, all pre-existing upstream patterns.
        -Wno-microsoft-template
        # Assignment-as-condition (if (err = Foo())) is an established Windows
        # error-handling idiom throughout the engine and test sources.
        -Wno-parentheses
        -Wno-logical-not-parentheses
        # NULL used as DWORD_PTR / integer in blockcache; pre-existing.
        -Wno-null-conversion
        -Wno-null-arithmetic
        # Expression results intentionally discarded in blockcache macros.
        -Wno-unused-value
        # memset on non-trivially-copyable structs (IOREQ, DBFILEHDR, etc.)
        # matches the engine's existing Windows usage; safe in practice.
        -Wno-nontrivial-memcall
        # blockcache has `typename` in contexts clang considers non-dependent;
        # the code compiles correctly with delayed template parsing enabled.
        -Wno-typename-missing
        # Inline functions declared in test headers but defined elsewhere
        # (e.g. FGetBit/SetBit/FlipBit in checksum_test.cxx).
        -Wno-undefined-inline
        # delete on void* in blockcache; pre-existing upstream code.
        -Wno-delete-incomplete
    )
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

    # etwlttng: parses EseEtwEventsPregen.txt and emits the LTTng UST
    # tracepoint provider + osposix dispatch glue.  Built from tools/
    # etwlttng/.
    find_program(ETWLTTNG NAMES etwlttng HINTS "$ENV{HOME}/bin")
    if (NOT ETWLTTNG)
        message(FATAL_ERROR "etwlttng not found: build and install tools/etwlttng to ~/bin")
    else ()
        message(STATUS "Found etwlttng: '${ETWLTTNG}'")
    endif ()

    # lttng-ust: required to build libese_tracepoints.so (the dlopen-
    # loaded analytic tracing provider).  Optional at build time —
    # absent boxes skip the .so target and libese.so falls back to
    # the no-tracepoint code path at runtime.
    find_package(PkgConfig QUIET)
    if (PkgConfig_FOUND)
        pkg_check_modules(LTTNG_UST IMPORTED_TARGET lttng-ust)
    endif ()
    if (LTTNG_UST_FOUND)
        message(STATUS "Found lttng-ust ${LTTNG_UST_VERSION}: building libese_tracepoints.so")
        set(ESE_BUILD_TRACEPOINTS TRUE)
    else ()
        message(STATUS "lttng-ust not found: libese_tracepoints.so will be skipped (libese.so still builds and runs without tracing)")
        set(ESE_BUILD_TRACEPOINTS FALSE)
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

#  Global ESE project defines
add_compile_definitions(
    OS_LAYER_USE_NEW_OVERRIDE
)

if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    # Using Microsoft Visual C++

    # The _HAS_STD_BYTE=0 came in along with the CMAKE_CXX_STANDARD 17 (and was <LanguageStandard>stdcpp17</LanguageStandard> change 
    # from the .vcxproj project file world) to make ESE code not conflict.  Research suggested this is only need for MSVC, so it is 
    # set in here.
    add_compile_definitions(
        _HAS_STD_BYTE=0
    )

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
        /Zc:externConstexpr
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

# Pin a $ORIGIN-relative RPATH on every Linux target — executables in
# <build>/bin/ resolve libese.so / libnls.so / etc. in <build>/lib/
# regardless of where the build tree lives, and the deployed bin+lib
# pair works under any prefix.  We use the install-RPATH machinery
# (with BUILD_WITH_INSTALL_RPATH) rather than
# CMAKE_BUILD_RPATH_USE_ORIGIN because the latter has a CMake/Ninja
# interaction that strips the leading `/` and yields a broken RUNPATH
# like `p/ese/repo/out/debug/lib`.
#
# This block must run BEFORE any add_subdirectory() that defines
# targets (vendored deps included) — directory-scoped variables only
# propagate into subdirectories processed after the set().  Putting it
# below `add_subdirectory(third_party/libnls)` made nls_smoke pick up
# the absolute build-tree path instead of $ORIGIN.
if (NOT WIN32)
    set( CMAKE_INSTALL_RPATH         "\$ORIGIN/../lib;\$ORIGIN" )
    set( CMAKE_BUILD_WITH_INSTALL_RPATH TRUE )
    set( CMAKE_SKIP_BUILD_RPATH         FALSE )
endif ()

# Vendored libucl, used by the Linux config layer (config_posix.cxx).
# Windows builds use the registry-backed config.cxx and don't need it.
if (NOT WIN32)
    add_subdirectory(third_party/libucl-0.9.4)

    # Vendored zstd — backs the RtlCompressBuffer / RtlDecompressBufferEx
    # shim in dev/ese/src/os/posix/winapi_compression.cxx, which in turn
    # backs the blockcache journal-entry compression.  Static-linked into
    # osposix; no system libzstd dependency.
    add_subdirectory(third_party/zstd-1.5.7)

    # libnls — vendored as a git submodule under third_party/libnls/,
    # built as part of the ESE build.  Provides the Win32 NLS surface
    # that windows-shim/windows.h pulls in via <winnls.h>.  Every TU
    # that includes the shim needs the header on its search path even
    # if it doesn't link libnls, so we promote nls::nls's
    # INTERFACE_INCLUDE_DIRECTORIES to project-wide.  Linking against
    # nls::nls is still per-target (currently just osposix).
    add_subdirectory(third_party/libnls)
    get_target_property(NLS_INC_DIRS nls::nls INTERFACE_INCLUDE_DIRECTORIES)
    include_directories(SYSTEM ${NLS_INC_DIRS})

    # libsodium — downloaded and built at configure time (NOT vendored
    # in the repo).  libese.so dlopens libsodium.so.26 at runtime for
    # AES-256-GCM at-rest encryption; we ship our own 1.0.22-stable
    # build because the 1.0.18 release that Ubuntu 24.04 / Debian 12
    # / etc. carry has no AArch64 AES path (its aes256gcm
    # implementation is x86-AES-NI-only).
    #
    # No CMake dep on libsodium — the .so just needs to land under
    # <build>/lib/ so libese.so's $ORIGIN/../lib RPATH resolves the
    # runtime dlopen.  ExternalProject_Add fetches, configures with
    # autotools (cross-aware via CC + CFLAGS + --host), and copies
    # the produced libsodium.so* into the build's lib output dir.
    include(ExternalProject)

    set(LIBSODIUM_URL "https://github.com/jedisct1/libsodium/releases/download/1.0.22-RELEASE/libsodium-1.0.22.tar.gz")
    set(LIBSODIUM_SHA256 "adbdd8f16149e81ac6078a03aca6fc03b592b89ef7b5ed83841c086191be3349")

    #  Bake every position-sensitive flag into the CC string passed
    #  to autotools.  libtool reorders / filters LDFLAGS when it
    #  reconstructs the link line and drops --target,
    #  --rtlib=compiler-rt, --stdlib=libc++, -unwindlib=libunwind
    #  even when they're in LDFLAGS — so the shared-library link
    #  falls back to the host triple + GCC runtime and fails to
    #  find aarch64 crt files / libgcc.  Folding the flags into CC
    #  makes them part of every clang invocation libtool emits,
    #  since CC is treated as a single command and survives all of
    #  libtool's reordering.
    #
    #  LDFLAGS still carries -fuse-ld=lld — that one libtool does
    #  preserve, and clang accepts it from either position.
    set(_libsodium_cc "${CMAKE_C_COMPILER}")
    if (CMAKE_CROSSCOMPILING)
        string(APPEND _libsodium_cc " --target=${CMAKE_C_COMPILER_TARGET}")
        if (CMAKE_SYSROOT)
            string(APPEND _libsodium_cc " --sysroot=${CMAKE_SYSROOT}")
        endif ()
    endif ()
    #  --rtlib=compiler-rt replaces libgcc, which the sysroot
    #  doesn't ship (we're LLVM end-to-end).  libsodium is pure C
    #  with no exception/unwinding needs, so we don't bother
    #  specifying an unwindlib — compiler-rt + lld produce a clean
    #  shared library on their own.
    string(APPEND _libsodium_cc " --rtlib=compiler-rt")

    set(_libsodium_cflags "")
    set(_libsodium_ldflags "-fuse-ld=lld")

    set(_libsodium_host_arg "")
    if (CMAKE_CROSSCOMPILING)
        set(_libsodium_host_arg "--host=${CMAKE_C_COMPILER_TARGET}")
    endif ()

    set(_libsodium_install_dir "${CMAKE_BINARY_DIR}/_deps/libsodium-install")

    ExternalProject_Add(libsodium_external
        URL ${LIBSODIUM_URL}
        URL_HASH SHA256=${LIBSODIUM_SHA256}
        PREFIX ${CMAKE_BINARY_DIR}/_deps/libsodium
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        CONFIGURE_COMMAND
            env "CC=${_libsodium_cc}"
                "CFLAGS=${_libsodium_cflags}"
                "LDFLAGS=${_libsodium_ldflags}"
            <SOURCE_DIR>/configure
            --prefix=${_libsodium_install_dir}
            --libdir=${_libsodium_install_dir}/lib
            --disable-static
            --disable-dependency-tracking
            ${_libsodium_host_arg}
        BUILD_COMMAND make -j14
        INSTALL_COMMAND make install
        BUILD_BYPRODUCTS
            ${_libsodium_install_dir}/lib/libsodium.so.26
    )

    #  Stage libsodium.so* into <build>/lib/ so $ORIGIN-relative
    #  dlopen from libese.so finds it.  cp -P preserves the symlinks
    #  (libsodium.so → libsodium.so.26 → libsodium.so.26.X.Y) so any
    #  SOname spelling resolves.
    add_custom_command(
        OUTPUT ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libsodium.so.26
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
        COMMAND sh -c "cp -P ${_libsodium_install_dir}/lib/libsodium.so* ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/"
        DEPENDS libsodium_external
        COMMENT "Staging libsodium.so* into ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/"
    )

    add_custom_target(libsodium_stage ALL
        DEPENDS ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libsodium.so.26
    )
endif ()

# Finally, in this project, we configure and build these subdirectories:
add_subdirectory(dev)

# `package-libs` — convenience aggregate for packaging.  Builds libese.so
# plus every runtime sidecar that has to ship next to it in <build>/lib:
# libnls (link-time dependency), libsodium (dlopen'd for at-rest
# encryption), and — when lttng-ust was found at configure time —
# libese_tracepoints.so (dlopen'd analytic-tracing provider).  The two
# dlopen'd sidecars are NOT link dependencies of `ese`, so building
# `--target ese` alone omits them; this target names them explicitly.
# Defined here (before the test subdirectories) so ese-tests can depend
# on it and stage the sidecars into <build>/lib for the encryption /
# tracing runtime paths.
if (NOT WIN32)
    add_custom_target(package-libs
        COMMENT "Built libese.so + sidecars (libnls, libsodium, tracepoints) into <build>/lib"
    )
    add_dependencies(package-libs ese nls libsodium_stage)
    if (ESE_BUILD_TRACEPOINTS)
        add_dependencies(package-libs ese_tracepoints)
    endif ()
endif ()

add_subdirectory(test)

option(IntegrationTestsEnabled "Build the public-API integration test suite" ON)
if (NOT WIN32 AND IntegrationTestsEnabled)
    add_subdirectory(integration-tests)
endif ()


# `run-all-tests` — convenience target that builds every test binary
# and then drives them through repo/run-all-tests.sh.  Each suite gets
# its own subdirectory under ${CMAKE_BINARY_DIR}/test-output, so a
# `rm -rf test-output` cleans up everything.
#
# Invoke with:  cmake --build <build-dir> --target run-all-tests
if (NOT WIN32)
    add_custom_target(run-all-tests
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/test-output
        COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR}/test-output
                ${CMAKE_SOURCE_DIR}/run-all-tests.sh ${CMAKE_BINARY_DIR}
        DEPENDS
            ese-tests
            ese-config-test
            EseLibWithTestsRunner
            CcLayerUnit
            COLLECTIONUNIT
            ERRVALIDATOR
            IterQueryUnit
            RESMGRUNIT
            STATUNIT
            SYNCUNIT
            nls_smoke
        COMMENT "Running all ESE test suites"
        USES_TERMINAL)
endif ()
