# Building ESE on Linux

This document describes how to build the Linux port of ESE.  Supported
targets are x86_64 and aarch64 Linux, against either **glibc** or
**musl libc**; the Windows toolchain is not currently supported from
this tree.

## Prerequisites

### Clang 22 toolchain

The port is **Clang-only**.  It relies on three Clang extensions that
GCC does not implement:

- `-fms-extensions` — Microsoft anonymous structs / unions
- `-fdeclspec` — `__declspec(...)` annotations the upstream code uses
- `-fdelayed-template-parsing` — the MSVC-style template parsing the
  engine depends on

Install Clang 22 (or newer) along with its matching libc++, libc++abi,
and libunwind static archives.  Three common ways to get a usable
toolchain:

- A pre-built tarball from <https://github.com/llvm/llvm-project/releases>
  (look for `clang+llvm-22.*-x86_64-linux-gnu-ubuntu-*.tar.xz` or
  `aarch64`).  Extract it anywhere.
- Distro packages (`sudo apt install clang-22 libc++-22-dev libc++abi-22-dev`
  on recent Debian / Ubuntu).
- A self-built LLVM that bundles the runtime libraries.

Whichever path you choose, you'll point CMake at the compiler via
`-DCMAKE_C_COMPILER=` and `-DCMAKE_CXX_COMPILER=`.  Clang does **not**
need to be on `PATH`.

The build statically links libc++, libc++abi, and libunwind into
`libese.so`, so the resulting library has no C++ runtime dependency on
the host system's libstdc++.

### Linux distro packages

```sh
sudo apt install \
    cmake \
    git \
    pkg-config \
    liburing-dev \
    libssl-dev
```

`liburing` backs the asynchronous file API; CMake locates it via
`pkg-config`.

### Kernel requirements

io_uring needs **Linux kernel 5.1 or newer**.  The buffer-manager
memory-notification path uses PSI (`/proc/pressure/memory`) on
**kernel 4.20 or newer** when available and falls back to
`/proc/meminfo` polling otherwise.

### .NET 10 SDK (for the two AOT helper tools)

Two C# native-AOT tools build out of the tree and produce single
self-contained ELF binaries that the CMake configure step then locates
via `find_program`.  Install the .NET 10 SDK (or newer) with the AOT
workload from <https://dotnet.microsoft.com/download>, then build both
tools and place the outputs anywhere on `PATH`:

```sh
# From inside the repo:
dotnet publish tools/mc                          -c Release -r linux-x64 -o $HOME/bin
dotnet publish third_party/libnls/tools/nlsembed -c Release -r linux-x64 -o $HOME/bin
```

This produces `mc` and `nlsembed`.  CMake searches `$HOME/bin`
explicitly in addition to `PATH`, so either location works.

### Submodules

`third_party/libnls/` is a git submodule.  Clone with
`--recurse-submodules`, or after a plain clone:

```sh
git submodule update --init --recursive
```

## Configure & build

The instructions below assume you've cloned the repo and `cd`'d into
it.  Substitute the path to your clang install for `<clang-prefix>`
(for example `/usr` if you installed via `apt`, or
`/opt/llvm-22.1.3` if you extracted a tarball there).

### Debug build

```sh
mkdir -p build
cd build
cmake .. -G "Unix Makefiles" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER=<clang-prefix>/bin/clang \
    -DCMAKE_CXX_COMPILER=<clang-prefix>/bin/clang++
cmake --build . -j$(nproc)
```

### Release build

```sh
mkdir -p build-release
cd build-release
cmake .. -G "Unix Makefiles" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=<clang-prefix>/bin/clang \
    -DCMAKE_CXX_COMPILER=<clang-prefix>/bin/clang++
cmake --build . -j$(nproc)
```

### musl (cross-libc) build — x86_64 or aarch64

Building against musl libc instead of glibc requires:

- A musl sysroot containing the musl C library, musl-targeted
  `liburing` (+ `pkg-config` `.pc` files), and any other runtime
  dependencies (lttng-ust, liburcu, libsodium, …) you want built into
  `libese.so`.  Alpine's `musl-dev`, `liburing-dev`, etc. packages
  staged under a single sysroot prefix work well.
- A Clang install that ships **musl-targeted** libc++, libc++abi,
  and libunwind archives.  The recent LLVM tarballs publish these
  under `lib-musl/lib/x86_64-unknown-linux-musl/` next to the
  default-target `lib/x86_64-unknown-linux-gnu/` tree.  These are
  separate `_pic.a` archives — the gnu-targeted libc++ won't link
  against musl objects.

The repository ships two CMake toolchain files for canonical
configurations:

- `cmake/x86_64-linux-musl-toolchain.cmake` — musl x86_64 for local
  development on a glibc workstation.  PT_INTERP points at the
  sysroot's loader so `./ese-tests` runs directly without an
  explicit `ld-musl-x86_64.so.1 …` invocation.
- `cmake/aarch64-linux-musl-toolchain.cmake` — musl aarch64
  cross-compilation onto a separate aarch64 runtime host.  PT_INTERP
  and DT_RUNPATH bake in the deployment paths on that host, so the
  staged binary runs natively there once the sysroot's loader + libc
  + needed shared libraries are rsynced to the matching paths.

Workstation-specific paths are passed in two equivalent ways — the
toolchain files don't carry any hardcoded paths:

- **Recommended**: copy `local-paths.cmake.template` (at the repo root)
  to `local-paths.cmake` (same directory; gitignored) and edit the
  values to match your environment.  Both musl toolchain files
  `include()` this file if it exists.
- **Ad hoc**: pass the same variables on the `cmake` command line
  via `-DESE_TOOLCHAIN_ROOT=...` etc.  Command-line values win over
  whatever's in `local-paths.cmake`.

The required variables:

| Variable                       | Purpose                                                              |
|--------------------------------|----------------------------------------------------------------------|
| `ESE_TOOLCHAIN_ROOT`           | Root of the LLVM toolchain (`bin/`, `lib-musl/lib/<triple>/…`).      |
| `ESE_MUSL_X64_SYSROOT`         | musl x86_64 sysroot.  Required by the x86_64-musl toolchain.        |
| `ESE_MUSL_AARCH64_SYSROOT`     | musl aarch64 sysroot.  Required by the aarch64-musl toolchain.      |

Optional:

| Variable                       | Purpose                                                              |
|--------------------------------|----------------------------------------------------------------------|
| `ESE_MUSL_AARCH64_DEPLOY_ROOT` | aarch64 runtime-host path where the musl runtime is staged.  When set, PT_INTERP and DT_RUNPATH are baked at link time to resolve the loader + shared libs from `${path}/{lib,usr/lib}`.  Needed only if the target host doesn't carry a musl runtime at the standard `/lib/ld-musl-aarch64.so.1` + `/lib` + `/usr/lib` — for example, a Debian/glibc aarch64 box with the musl runtime rsynced to a custom location.  Leave unset for native-musl targets like Alpine. |

Configure + build:

```sh
mkdir -p build-musl
cd build-musl
cmake .. -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=../cmake/x86_64-linux-musl-toolchain.cmake \
    -DCMAKE_BUILD_TYPE=Debug
cmake --build . -j$(nproc)
```

A few non-obvious details the toolchain file handles for you:

- `_LIBCPP_PROVIDES_DEFAULT_RUNE_TABLE` is defined globally — LLVM's
  libc++ `__locale` header doesn't have a musl branch in its
  rune-table detection chain.
- `__GNUC__=4` is defined globally.  The engine compiles with
  `-fms-compatibility`, which suppresses `__GNUC__`.  musl's
  `<stddef.h>` then falls back to the non-constexpr null-pointer-trick
  `offsetof`, which breaks the engine's `static_assert( offsetof(...) )`
  uses.  Restoring `__GNUC__` makes musl headers pick the
  `__builtin_offsetof` branch.
- `PKG_CONFIG_LIBDIR` + `PKG_CONFIG_SYSROOT_DIR` route `pkg-config` at
  the sysroot, so `liburing` / `lttng-ust` / etc. resolve against
  musl-targeted libraries rather than the host's glibc ones.
- The produced binaries embed `${MUSL_SYSROOT}/lib/ld-musl-x86_64.so.1`
  as the dynamic linker (via `-Wl,--dynamic-linker=…`) and the
  sysroot's `usr/lib` + `lib` in `DT_RUNPATH`, so the result runs
  directly with no `LD_LIBRARY_PATH` dance.  The musl loader silently
  skips non-existent rpath entries, so on an Alpine target the local
  `/lib` + `/usr/lib` will win at load time.

### Building a specific target

```sh
cmake --build build              --target ese-tests              -j$(nproc)
cmake --build build              --target eseutil                -j$(nproc)
cmake --build build              --target EseLibWithTestsRunner  -j$(nproc)
```

## Output

Binaries land in `build/bin/` (or `build-release/bin/`); libraries in
`build/lib/`.  The major targets:

| Binary | Purpose |
|--------|---------|
| `libese.so` | The engine itself. |
| `eseutil` | The standalone ESE utility (recovery, defrag, dump, etc.). |
| `BookStoreSample` | End-to-end example of a `libese.so` consumer. |
| `EseLibWithTestsRunner` | Engine-side test runner (tier-1 `JETUNITTEST`, tier-2 `JETUNITTESTDB`). |
| `ese-tests` | Public-API integration test suite under `integration-tests/`. |
| `CcLayerUnit`, `COLLECTIONUNIT`, `ERRVALIDATOR`, `IterQueryUnit`, `RESMGRUNIT`, `STATUNIT`, `SYNCUNIT` | `devlibtest` unit-test binaries. |
| `nls_smoke` | Smoke test for the bundled `libnls.so`. |

## Running the tests

The test binaries write artefacts into their current working
directory.  Run each from a fresh scratch directory rather than inside
the source tree.

### Public-API integration tests

```sh
mkdir -p /tmp/ese-tests && cd /tmp/ese-tests
/path/to/build/bin/ese-tests                              # full suite
/path/to/build/bin/ese-tests --filter "Schema.*"          # filter by name glob
/path/to/build/bin/ese-tests --log-file /tmp/ese-tests/run.log
```

`--log-file <path>` mirrors every stdout/stderr write into the
specified file in addition to the terminal; convenient for `grep`ping
failures after a run without re-executing.

### Engine-side tier-1 / tier-2 tests

```sh
# Tier-1 (no DB).
mkdir -p /tmp/ese-tier1 && cd /tmp/ese-tier1
/path/to/build/bin/EseLibWithTestsRunner

# Tier-2 (uses a real JET database in the current directory).
mkdir -p /tmp/ese-tier2 && cd /tmp/ese-tier2
/path/to/build/bin/EseLibWithTestsRunner -d .
```

### `devlibtest` unit tests

Each binary self-runs:

```sh
mkdir -p /tmp/ese-devlibtest && cd /tmp/ese-devlibtest
/path/to/build/bin/CcLayerUnit
/path/to/build/bin/COLLECTIONUNIT
/path/to/build/bin/ERRVALIDATOR
/path/to/build/bin/IterQueryUnit
/path/to/build/bin/RESMGRUNIT
/path/to/build/bin/STATUNIT
/path/to/build/bin/SYNCUNIT
```

## Editor support

CMake emits `compile_commands.json` into the build directory; point
your `clangd`-based LSP (VS Code, Neovim, CLion, Emacs, ...) at it for
go-to-definition, completion, and diagnostics.

If you want to consume the public JET API from your own project
without dragging in the engine's internal headers, include
`jetapi.h` from the repository root.  It is freestanding: depends only
on `<stdint.h>` (plus `<uchar.h>` in C for `char16_t`) and exposes the
full JET API at `JET_VERSION 0x0A01`.

## Troubleshooting

**`Could NOT find PkgConfig: liburing`** — install `liburing-dev`.

**`nlsembed not found` during configure** — build the AOT tool and
make sure the output directory is on `PATH` (or in `$HOME/bin`, which
CMake also searches).  See "Prerequisites → .NET 10 SDK" above.

**`mc not found` during configure** — same fix as above for the
`tools/mc` build.

**`unknown attribute '__declspec'` or similar** — you're building
with GCC, not Clang.  Pass the clang path via
`-DCMAKE_C_COMPILER` / `-DCMAKE_CXX_COMPILER` on the `cmake` invocation.

**`'__C_ASSERT__' declared as an array with a negative size`** — a
struct-size assertion in the engine fired.  If you've modified
`dev/ese/published/inc/jethdr.w` or any of the JET_* structs, the
pinned sizes in `dev/ese/src/ese/jetapi.cxx` need to be updated to
match.
