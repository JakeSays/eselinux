# CMake toolchain file for the musl x86_64 target.
#
# Layered alongside the default glibc build (build/debug) and the
# aarch64 glibc cross (build/aarch64-debug).  Differences vs glibc:
#
#   - target triple is x86_64-linux-musl
#   - sysroot points at /p/ese/musl-x64 (musl libc + headers; libsodium
#     is intentionally NOT picked up from the sysroot — we build it
#     from source like the glibc builds do)
#   - libc++ / libc++abi / libunwind come from the toolchain's
#     lib-musl/ tree (clang ships a separate musl-targeted libc++
#     install).  Statically linked into every binary so the result
#     doesn't carry a runtime dependency on a particular libc++
#     version.  The root CMakeLists.txt does the actual linking; we
#     just publish the directory via ESE_LLVM_LIBDIR.
#   - _LIBCPP_PROVIDES_DEFAULT_RUNE_TABLE is defined because libc++'s
#     __locale rune-table chain doesn't recognise musl; without this
#     the platform-detection cascade falls through to an #error.

#  Workstation-specific paths come from one of:
#    1. The cmake command line (`-DESE_TOOLCHAIN_ROOT=...`).
#    2. local-paths.cmake at the repo root (gitignored — each
#       developer copies local-paths.cmake.template and edits).
#    3. Both — cmd-line wins thanks to the `if (NOT DEFINED …)`
#       guards in local-paths.cmake.
#  Required: ESE_TOOLCHAIN_ROOT, ESE_MUSL_X64_SYSROOT.
include("${CMAKE_CURRENT_LIST_DIR}/../local-paths.cmake" OPTIONAL)

foreach (_required IN ITEMS ESE_TOOLCHAIN_ROOT ESE_MUSL_X64_SYSROOT)
    if (NOT DEFINED ${_required})
        message(FATAL_ERROR
            "${_required} not set.  Either copy "
            "local-paths.cmake.template to local-paths.cmake at the "
            "repo root and edit the values, or pass each required "
            "variable on the cmake command line via "
            "-D${_required}=...  See BUILDING.md for details.")
    endif ()
endforeach ()

set(TOOLCHAIN_ROOT "${ESE_TOOLCHAIN_ROOT}")
set(MUSL_SYSROOT "${ESE_MUSL_X64_SYSROOT}")
set(TARGET_TRIPLE "x86_64-unknown-linux-musl")
set(TOOLCHAIN_MUSL_LIB "${TOOLCHAIN_ROOT}/lib-musl/lib/${TARGET_TRIPLE}")
set(TOOLCHAIN_MUSL_INC "${TOOLCHAIN_ROOT}/lib-musl/include")

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER   "${TOOLCHAIN_ROOT}/bin/clang")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_ROOT}/bin/clang++")
set(CMAKE_ASM_COMPILER "${TOOLCHAIN_ROOT}/bin/clang")

set(CMAKE_C_COMPILER_TARGET   ${TARGET_TRIPLE})
set(CMAKE_CXX_COMPILER_TARGET ${TARGET_TRIPLE})
set(CMAKE_ASM_COMPILER_TARGET ${TARGET_TRIPLE})

set(CMAKE_SYSROOT "${MUSL_SYSROOT}")

# Replace the default libc++ header search path with the
# toolchain's musl-targeted libc++ headers.  -nostdinc++ drops the
# defaults; the two -isystem entries restore them from lib-musl/.
# musl isn't in libc++'s rune-table detection chain, so force the
# default rune table to make <__locale> parse.
# `-fms-compatibility` (used pervasively by the engine for MSVC source
# compatibility) clears `__GNUC__`.  musl's `<stddef.h>` gates the
# constexpr `__builtin_offsetof` form behind `__GNUC__ > 3` and falls
# back to the non-constexpr null-pointer trick when it's missing —
# which then fails every `static_assert( offsetof(...) ... )` the
# engine does.  Define `__GNUC__=4` so musl headers pick the GCC
# branches; clang has always claimed GCC-4 compatibility outside MSVC
# mode anyway.
# `<libunwind.h>` ships under ${TOOLCHAIN_MUSL_INC} (not on the
# default musl-target include path).  winapi_memory.cxx includes it
# for the RtlCaptureStackBackTrace shim.
set(CMAKE_CXX_FLAGS_INIT "-nostdinc++ -isystem ${TOOLCHAIN_MUSL_INC}/c++/v1 -isystem ${TOOLCHAIN_MUSL_INC}/${TARGET_TRIPLE}/c++/v1 -isystem ${TOOLCHAIN_MUSL_INC} -D_LIBCPP_PROVIDES_DEFAULT_RUNE_TABLE -D__GNUC__=4")
set(CMAKE_C_FLAGS_INIT "-isystem ${TOOLCHAIN_MUSL_INC} -D_LIBCPP_PROVIDES_DEFAULT_RUNE_TABLE -D__GNUC__=4")

# Tell the root CMakeLists.txt where to find the static archives.
# Without this it would compute lib/<triple>/ from -print-resource-dir,
# which misses the musl-specific lib-musl/lib/<triple>/ layout.
set(ESE_LLVM_LIBDIR "${TOOLCHAIN_MUSL_LIB}")

# Enough linker setup to make CMake's C/C++ compiler-test
# try_compile pass; the libc++ static archives are wired up later
# in the root CMakeLists.txt via link_libraries().  Without lld +
# compiler-rt here, CMake falls back to /usr/bin/ld -lgcc which can't
# touch musl objects.
# Point PT_INTERP at the sysroot's loader so the resulting executables
# run directly on this host (`./ese-tests`, no `ld-musl … prefix`).
# The default `/lib/ld-musl-x86_64.so.1` is the standard Alpine path
# but doesn't exist on a glibc developer machine; pinning to the
# sysroot's copy keeps both worlds working — local dev + the Alpine
# target path is preserved via the same loader binary regardless,
# since musl loaders are version-pinned by sysroot.
#  -Wl,-rpath bakes the sysroot's lib dirs into DT_RUNPATH so the
#  resulting binary finds liburing / lttng-ust / etc. at runtime
#  without an LD_LIBRARY_PATH dance.  The musl loader silently skips
#  non-existent rpath entries, so when this binary later ships to an
#  Alpine target the local /lib + /usr/lib still win.
set(_baseline_link_flags "-fuse-ld=lld --rtlib=compiler-rt -stdlib=libc++ -nostdlib++ -unwindlib=libunwind -L${TOOLCHAIN_MUSL_LIB} -Wl,--dynamic-linker=${MUSL_SYSROOT}/lib/ld-musl-x86_64.so.1 -Wl,-rpath,${MUSL_SYSROOT}/usr/lib:${MUSL_SYSROOT}/lib")
set(CMAKE_EXE_LINKER_FLAGS_INIT    "${_baseline_link_flags}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_baseline_link_flags}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_baseline_link_flags}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Route pkg-config at the sysroot's .pc files so liburing,
# lttng-ust, liburcu (and anything else pkg_check_module hits)
# resolve to the musl-targeted versions in /p/ese/musl-x64,
# not the host's /usr/lib/x86_64-linux-gnu glibc libraries.
# PKG_CONFIG_LIBDIR fully replaces the default search dirs;
# PKG_CONFIG_SYSROOT_DIR makes pkg-config prepend the sysroot
# to every path it returns (so `-L/usr/lib` from a .pc becomes
# `-L${MUSL_SYSROOT}/usr/lib`).
set(ENV{PKG_CONFIG_LIBDIR} "${MUSL_SYSROOT}/usr/lib/pkgconfig:${MUSL_SYSROOT}/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${MUSL_SYSROOT}")
