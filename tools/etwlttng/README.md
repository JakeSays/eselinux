# etwlttng — LTTng tracepoint provider generator

A native AOT C# tool that reads `dev/ese/src/_etw/EseEtwEventsPregen.txt`
(the canonical event-list source consumed by `gengenetw.pl`) and emits
the four files that drive the Linux port's LTTng UST analytic-tracing
provider:

| File | Purpose | Consumer |
|---|---|---|
| `ese_tracepoints.h` | `TRACEPOINT_PROVIDER ese` + per-event `TRACEPOINT_EVENT` definitions | `libese_tracepoints.so` |
| `ese_tracepoint_provider.cxx` | `TRACEPOINT_DEFINE` + trampolines + the static `EseTracepointTable` + the `EseTracepointGetTable` export | `libese_tracepoints.so` |
| `ese_tracepoint_table.h` | The shared `struct EseTracepointTable` (one function pointer per tracepoint) | both `libese.so` and `libese_tracepoints.so` |
| `oseventtrace_dispatch.g.cxx` | One static handler per event + the master `EseTpDispatch( etguid, ap, pTable )` switch | `dev/ese/src/os/posix/oseventtrace_posix.cxx` |

## Build & install

Requires the .NET 10 SDK (or newer) with the AOT workload.

```sh
cd /p/ese/repo/tools/etwlttng
dotnet publish -c Release -r linux-x64 -o ~/bin
```

The result is `~/bin/etwlttng`.  Re-run after editing this tool's
source — the engine build does not rebuild it automatically.

## Usage

```
etwlttng <EseEtwEventsPregen.txt> <output-dir>
```

CMake invokes it via the `gen_etwlttng` custom target in
`dev/ese/src/os/posix/tracepoints/CMakeLists.txt`.  The output dir is
`${GEN_OUTPUT_DIRECTORY}/etwlttng/`, included by `osposix` (for the
dispatch) and consumed by the `ese_tracepoints` shared-library target
(for the trampolines and the public `EseTracepointGetTable` symbol).

## Field-count cap (10 per tracepoint)

`lttng-ust`'s `LTTNG_UST__TP_NARGS` macro caps tracepoint arity at 20
arguments (= 10 `type, name` pairs) across every released version
through 2.15.  ESE has 13 events with more than 10 fields (largest is
`IOCompletion2` at 31 fields).

The generator handles overflow by:

- Keeping the first 9 fields as typed `ctf_*` fields (queryable in
  babeltrace).  Pregen.txt orders fields with identifiers leading, so
  the most filter-useful ones (ifmp, pgno, objid, ...) stay typed.
- Packing the rest into a single 10th `ctf_string` field named
  `overflow`, formatted as `name1=val1 name2=val2 ...`.

Lossless capture, lossy filterability for overflow fields.
