# mc — Linux replacement for Microsoft Message Compiler

A native AOT C# tool that replaces the Windows SDK `mc.exe` for the purposes of
the ESE Linux port. Reads classic `.mc` message-text files and emits a C header
with `#define` for each `SymbolicName`, plus an empty `.rc` stub for build-system
compatibility.

## Build & install

Requires the .NET 10 SDK (or newer) with the AOT workload. The project is
configured for native AOT — the published binary is a single self-contained
ELF with no .NET runtime dependency at runtime.

```sh
cd /p/ese/repo/tools/mc
dotnet publish -c Release -r linux-x64 -o ~/bin
```

The result is `~/bin/mc`. Make sure `~/bin` is on your `PATH`.

Re-run the publish step whenever the tool's source changes — the engine build
does not rebuild it automatically.

## Usage

```
mc [-v] -h <header-dir> -r <rc-dir> <input.mc>
mc -um <input.man> -h <header-dir> -r <rc-dir>     (stub only — see below)
```

### `.mc` mode (the v1 path)

For each `MessageId=N` block in the input, emits one `#define SYMBOLIC_NAME
((MessageId)NL)` in `<header-dir>/<stem>.h`, where `<stem>` is the input file's
base name without extension. Also writes an empty `<stem>.rc` to
`<rc-dir>` (CMake expects the file to exist; the Linux build doesn't link it).

The supported `.mc` grammar is the subset used by `dev/ese/src/_res/jetmsg.mc`:

- Header directives: `MessageIdTypedef=`, `LanguageNames=(name=id:filebase)`.
- Message blocks with `MessageId=`, `SymbolicName=`, one or more `Language=…`
  bodies terminated by a line containing only `.`.
- Comments: lines beginning with `;`.

`OutputBase=`, `SeverityNames=`, `FacilityNames=` are accepted and ignored
(none of them appear in `jetmsg.mc`). `Severity=` / `Facility=` per-message
attributes are recorded but do not affect the emitted ID value (jetmsg.mc
doesn't use them).

### `-um` manifest mode

Not implemented. Emits empty header and `.rc` stubs and prints a warning.
The ESE Linux port has all ETW emitters stubbed to no-ops for v1, so this
path is never load-bearing on the engine. If/when LTTng UST integration
lands, this tool will grow real manifest support.

## Project layout

- `Program.cs` — entry point, top-level error handling.
- `CliOptions.cs` — argument parsing.
- `Model.cs` — `McFile`, `McMessage`, `McLanguage`.
- `McParser.cs` — `.mc` → `McFile`.
- `HeaderEmitter.cs` — `McFile` → `<stem>.h`.
- `RcEmitter.cs` — empty `<stem>.rc` stub.
- `mc.csproj` — AOT-enabled csproj.
