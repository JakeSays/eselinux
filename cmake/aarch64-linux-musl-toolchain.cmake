# CMake toolchain file for the musl aarch64 target.
#
# Mirror of the x86_64 musl toolchain — same conventions, different
# target triple, sysroot, and loader name.  See
# `x86_64-linux-musl-toolchain.cmake` for the per-flag rationale.
#
# Unlike the x86_64 toolchain, the binaries this produces can't run
# on the host workstation (different ISA), so PT_INTERP and DT_RUNPATH
# bake in the DEPLOYMENT-host paths — wherever the sysroot bits will
# get rsynced before runtime.  Customize `MUSL_DEPLOY_ROOT` to match.

#  Workstation-specific paths come from one of:
#    1. The cmake command line (`-DESE_TOOLCHAIN_ROOT=...`).
#    2. local-paths.cmake at the repo root (gitignored — each
#       developer copies local-paths.cmake.template and edits).
#    3. Both — cmd-line wins thanks to the `if (NOT DEFINED …)`
#       guards in local-paths.cmake.
#
#  Required: ESE_TOOLCHAIN_ROOT, ESE_MUSL_AARCH64_SYSROOT.
#
#  Optional: ESE_MUSL_AARCH64_DEPLOY_ROOT.  When set, PT_INTERP and
#  DT_RUNPATH are baked at link time to point at
#  ${ESE_MUSL_AARCH64_DEPLOY_ROOT}/{lib,usr/lib} — useful when the
#  target host doesn't have a musl loader at the standard
#  /lib/ld-musl-aarch64.so.1 path (e.g. testing on a Debian/glibc
#  aarch64 box with the musl runtime staged at a custom path).  Leave
#  unset for true musl-native targets like Alpine — the default ELF
#  PT_INTERP from the linker is the right /lib/ path already.
include("${CMAKE_CURRENT_LIST_DIR}/../local-paths.cmake" OPTIONAL)

foreach (_required IN ITEMS ESE_TOOLCHAIN_ROOT ESE_MUSL_AARCH64_SYSROOT)
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
set(MUSL_SYSROOT "${ESE_MUSL_AARCH64_SYSROOT}")
set(TARGET_TRIPLE "aarch64-unknown-linux-musl")
set(TOOLCHAIN_MUSL_LIB "${TOOLCHAIN_ROOT}/lib-musl/lib/${TARGET_TRIPLE}")
set(TOOLCHAIN_MUSL_INC "${TOOLCHAIN_ROOT}/lib-musl/include")

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER   "${TOOLCHAIN_ROOT}/bin/clang")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_ROOT}/bin/clang++")
set(CMAKE_ASM_COMPILER "${TOOLCHAIN_ROOT}/bin/clang")

set(CMAKE_C_COMPILER_TARGET   ${TARGET_TRIPLE})
set(CMAKE_CXX_COMPILER_TARGET ${TARGET_TRIPLE})
set(CMAKE_ASM_COMPILER_TARGET ${TARGET_TRIPLE})

set(CMAKE_SYSROOT "${MUSL_SYSROOT}")

set(CMAKE_CXX_FLAGS_INIT "-nostdinc++ -isystem ${TOOLCHAIN_MUSL_INC}/c++/v1 -isystem ${TOOLCHAIN_MUSL_INC}/${TARGET_TRIPLE}/c++/v1 -isystem ${TOOLCHAIN_MUSL_INC} -D_LIBCPP_PROVIDES_DEFAULT_RUNE_TABLE -D__GNUC__=4")
set(CMAKE_C_FLAGS_INIT "-isystem ${TOOLCHAIN_MUSL_INC} -D_LIBCPP_PROVIDES_DEFAULT_RUNE_TABLE -D__GNUC__=4")

set(ESE_LLVM_LIBDIR "${TOOLCHAIN_MUSL_LIB}")

#  Base linker flags; PT_INTERP / DT_RUNPATH overrides get appended
#  below only when ESE_MUSL_AARCH64_DEPLOY_ROOT is set.
set(_baseline_link_flags "-fuse-ld=lld --rtlib=compiler-rt -stdlib=libc++ -nostdlib++ -unwindlib=libunwind -L${TOOLCHAIN_MUSL_LIB}")

if (DEFINED ESE_MUSL_AARCH64_DEPLOY_ROOT AND NOT ESE_MUSL_AARCH64_DEPLOY_ROOT STREQUAL "")
    #  Bake the deployment paths into PT_INTERP and DT_RUNPATH so the
    #  binary finds its musl loader + shared deps at a non-default
    #  location.  Only needed when the target host doesn't carry a
    #  musl runtime at /lib/ld-musl-aarch64.so.1 + /lib + /usr/lib
    #  (e.g. Debian aarch64 with the musl runtime staged elsewhere).
    set(_baseline_link_flags "${_baseline_link_flags} -Wl,--dynamic-linker=${ESE_MUSL_AARCH64_DEPLOY_ROOT}/lib/ld-musl-aarch64.so.1 -Wl,-rpath,${ESE_MUSL_AARCH64_DEPLOY_ROOT}/usr/lib:${ESE_MUSL_AARCH64_DEPLOY_ROOT}/lib")
endif ()
set(CMAKE_EXE_LINKER_FLAGS_INIT    "${_baseline_link_flags}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_baseline_link_flags}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_baseline_link_flags}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(ENV{PKG_CONFIG_LIBDIR} "${MUSL_SYSROOT}/usr/lib/pkgconfig:${MUSL_SYSROOT}/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${MUSL_SYSROOT}")
