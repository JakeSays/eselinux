// Linux shim for <winperf.h>. The Windows performance-counter ABI is
// what the engine's auto-generated perfdata.cxx hangs off — static
// structure literals describing every counter (PERF_OBJECT_TYPE,
// PERF_COUNTER_DEFINITION, …). The Linux port doesn't ship perfmon-
// style counters, but the generated table still has to compile and the
// rest of the engine reads from it (PerfOffsetOf etc.) so the structure
// layouts and counter-type constants must match.
//
// Field widths track Windows' winperf.h: DWORD (uint32), LONG (int32 in
// cc.hxx — same width on both ABIs), LPWSTR (wchar_t*), LARGE_INTEGER
// (8-byte signed). LONG is _not_ host `long` here — cc.hxx already
// pins it to int32_t on non-MSVC.
#pragma once

#ifndef ESE_COMPILER_MSVC

#include "windows.h"

typedef struct _PERF_OBJECT_TYPE {
    DWORD           TotalByteLength;
    DWORD           DefinitionLength;
    DWORD           HeaderLength;
    DWORD           ObjectNameTitleIndex;
    LPWSTR          ObjectNameTitle;
    DWORD           ObjectHelpTitleIndex;
    LPWSTR          ObjectHelpTitle;
    DWORD           DetailLevel;
    DWORD           NumCounters;
    LONG            DefaultCounter;
    LONG            NumInstances;
    DWORD           CodePage;
    LARGE_INTEGER   PerfTime;
    LARGE_INTEGER   PerfFreq;
} PERF_OBJECT_TYPE, *PPERF_OBJECT_TYPE;

typedef struct _PERF_COUNTER_DEFINITION {
    DWORD   ByteLength;
    DWORD   CounterNameTitleIndex;
    LPWSTR  CounterNameTitle;
    DWORD   CounterHelpTitleIndex;
    LPWSTR  CounterHelpTitle;
    LONG    DefaultScale;
    DWORD   DetailLevel;
    DWORD   CounterType;
    DWORD   CounterSize;
    DWORD   CounterOffset;
} PERF_COUNTER_DEFINITION, *PPERF_COUNTER_DEFINITION;

typedef struct _PERF_INSTANCE_DEFINITION {
    DWORD   ByteLength;
    DWORD   ParentObjectTitleIndex;
    DWORD   ParentObjectInstance;
    LONG    UniqueID;
    DWORD   NameOffset;
    DWORD   NameLength;
} PERF_INSTANCE_DEFINITION, *PPERF_INSTANCE_DEFINITION;

typedef struct _PERF_COUNTER_BLOCK {
    DWORD   ByteLength;
} PERF_COUNTER_BLOCK, *PPERF_COUNTER_BLOCK;

// Detail level (PERF_DETAIL_*).
#define PERF_DETAIL_NOVICE          100
#define PERF_DETAIL_ADVANCED        200
#define PERF_DETAIL_EXPERT          300
#define PERF_DETAIL_WIZARD          400
#define PERF_DETAIL_DEFAULT         PERF_DETAIL_NOVICE
#define PERF_DETAIL_DEVONLY         PERF_DETAIL_WIZARD

// Counter size (CounterType encoding bit 8-9).
#define PERF_SIZE_DWORD             0x00000000
#define PERF_SIZE_LARGE             0x00000100
#define PERF_SIZE_ZERO              0x00000200
#define PERF_SIZE_VARIABLE_LEN      0x00000300

// Counter type (display) bits 10-11.
#define PERF_TYPE_NUMBER            0x00000000
#define PERF_TYPE_COUNTER           0x00000400
#define PERF_TYPE_TEXT              0x00000800
#define PERF_TYPE_ZERO              0x00000C00

// Number sub-types (bits 16-17 when TYPE=NUMBER).
#define PERF_NUMBER_HEX             0x00000000
#define PERF_NUMBER_DECIMAL         0x00010000
#define PERF_NUMBER_DEC_1000        0x00020000

// Counter sub-types (bits 16-17 when TYPE=COUNTER).
#define PERF_COUNTER_VALUE          0x00000000
#define PERF_COUNTER_RATE           0x00010000
#define PERF_COUNTER_FRACTION       0x00020000
#define PERF_COUNTER_BASE           0x00030000

// Timer base (bits 20-21).
#define PERF_TIMER_TICK             0x00000000
#define PERF_TIMER_100NS            0x00100000
#define PERF_OBJECT_TIMER           0x00200000

// Calculation modifiers (bits 22-25).
#define PERF_DELTA_COUNTER          0x00400000
#define PERF_DELTA_BASE             0x00800000
#define PERF_INVERSE_COUNTER        0x01000000
#define PERF_MULTI_COUNTER          0x02000000

// Display flags (bits 28-30).
#define PERF_DISPLAY_NO_SUFFIX      0x00000000
#define PERF_DISPLAY_PER_SEC        0x10000000
#define PERF_DISPLAY_PERCENT        0x20000000
#define PERF_DISPLAY_SECONDS        0x30000000
#define PERF_DISPLAY_NOSHOW         0x40000000

// Composed counter types — the constants the generated perfdata.cxx
// actually references.
#define PERF_COUNTER_RAWCOUNT \
    ( PERF_SIZE_DWORD | PERF_TYPE_NUMBER | PERF_NUMBER_DECIMAL | PERF_DISPLAY_NO_SUFFIX )

#define PERF_COUNTER_LARGE_RAWCOUNT \
    ( PERF_SIZE_LARGE | PERF_TYPE_NUMBER | PERF_NUMBER_DECIMAL | PERF_DISPLAY_NO_SUFFIX )

#define PERF_COUNTER_COUNTER \
    ( PERF_SIZE_DWORD | PERF_TYPE_COUNTER | PERF_COUNTER_RATE | PERF_TIMER_TICK | PERF_DELTA_COUNTER | PERF_DISPLAY_PER_SEC )

#define PERF_COUNTER_BULK_COUNT \
    ( PERF_SIZE_LARGE | PERF_TYPE_COUNTER | PERF_COUNTER_RATE | PERF_TIMER_TICK | PERF_DELTA_COUNTER | PERF_DISPLAY_PER_SEC )

#define PERF_AVERAGE_BULK \
    ( PERF_SIZE_LARGE | PERF_TYPE_COUNTER | PERF_COUNTER_FRACTION | PERF_DELTA_COUNTER | PERF_DELTA_BASE | PERF_DISPLAY_NO_SUFFIX )

#define PERF_AVERAGE_BASE \
    ( PERF_SIZE_DWORD | PERF_TYPE_COUNTER | PERF_COUNTER_BASE | PERF_DISPLAY_NOSHOW )

#define PERF_RAW_FRACTION \
    ( PERF_SIZE_DWORD | PERF_TYPE_COUNTER | PERF_COUNTER_FRACTION | PERF_DISPLAY_PERCENT )

#define PERF_RAW_BASE \
    ( PERF_SIZE_DWORD | PERF_TYPE_COUNTER | PERF_COUNTER_BASE | PERF_DISPLAY_NOSHOW )

#define PERF_SAMPLE_FRACTION \
    ( PERF_SIZE_DWORD | PERF_TYPE_COUNTER | PERF_COUNTER_FRACTION | PERF_DELTA_COUNTER | PERF_DELTA_BASE | PERF_DISPLAY_PERCENT )

#define PERF_SAMPLE_BASE \
    ( PERF_SIZE_DWORD | PERF_TYPE_COUNTER | PERF_COUNTER_BASE | PERF_DISPLAY_NOSHOW )

#endif // !_MSC_VER
