// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Windows Error Reporting (WER) stubs. Linux has no equivalent of WER's
// per-process memory-block attachment; both entry points are no-ops
// pending LTTng / coredump integration.

#include "osstd.hxx"

extern "C" {

HRESULT WerRegisterMemoryBlock( PVOID /*pvAddress*/, DWORD /*dwSize*/ )
{
    return S_OK;
}

HRESULT WerUnregisterMemoryBlock( PVOID /*pvAddress*/ )
{
    return S_OK;
}

}  // extern "C"
