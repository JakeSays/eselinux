/*  Hand-written config.h for the vendored libucl 0.9.4.
 *
 *  Upstream libucl uses autoconf/CMake to generate this header; we
 *  vendored only the source files we need, so we ship a static config.h
 *  that declares the standard C library headers as present (which is
 *  true on every glibc system the ESE Linux port targets) and leaves
 *  the optional features (Lua, OpenSSL, libfetch, libcurl) disabled.
 */
#pragma once

#define HAVE_STDIO_H        1
#define HAVE_STDARG_H       1
#define HAVE_STRING_H       1
#define HAVE_STRINGS_H      1
#define HAVE_CTYPE_H        1
#define HAVE_ERRNO_H        1
#define HAVE_FLOAT_H        1
#define HAVE_LIMITS_H       1
#define HAVE_MATH_H         1
#define HAVE_FCNTL_H        1
#define HAVE_UNISTD_H       1
#define HAVE_LIBGEN_H       1
#define HAVE_REGEX_H        1
#define HAVE_SYS_TYPES_H    1
#define HAVE_SYS_STAT_H     1
#define HAVE_SYS_MMAN_H     1
#define HAVE_SYS_PARAM_H    1
#define HAVE_ENDIAN_H       1

/*  Atomic builtins are always present in clang/gcc that the port uses. */
#define HAVE_ATOMIC_BUILTINS 1

/*  Optional features intentionally OFF — pulls in no extra deps. */
/*  #undef HAVE_OPENSSL    */
/*  #undef HAVE_FETCH_H    */
/*  #undef HAVE_SYS_ENDIAN_H */
/*  #undef HAVE_MACHINE_ENDIAN_H */
