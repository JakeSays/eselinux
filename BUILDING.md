# Building ESE on Linux

This document describes how to build the Linux port of ESE.  Supported
targets are x86_64 and aarch64 Linux; the Windows toolchain is not 
currently supported from this tree.

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
```

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
