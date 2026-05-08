// POSIX equivalent of os/memorynotification.cxx. Upstream wires up the Win32
// CreateMemoryResourceNotification + RegisterWaitForSingleObject path so a
// caller's callback fires when the system enters/exits a low-memory state.
//
// Linux has analogous mechanisms (cgroup memory.events, PSI, vm.notify) but
// nothing as drop-in. For Phase 5 we provide a stub that successfully
// allocates + tracks a notification object but never fires the callback;
// query always returns "not low memory". The cache uses this only as an
// advisory signal — never reporting low-memory pressure just means the cache
// runs at full configured size.

#include "osstd.hxx"

#include "memorynotification.hxx"

namespace {

struct PosixMemoryNotification
{
    PfnMemNotification  pfnCallback;
    DWORD_PTR           dwContext;
};

} // anonymous

ERR ErrOSCreateLowMemoryNotification(
    PfnMemNotification const            pfnCallback,
    DWORD_PTR const                     dwContext,
    _Out_ HMEMORY_NOTIFICATION * const  ppNotification )
{
    PosixMemoryNotification * pn = new PosixMemoryNotification;
    if ( !pn )
    {
        *ppNotification = nullptr;
        return ErrERRCheck( JET_errOutOfMemory );
    }
    pn->pfnCallback = pfnCallback;
    pn->dwContext   = dwContext;
    *ppNotification = pn;
    return JET_errSuccess;
}

ERR ErrOSRegisterMemoryNotification( _In_ HMEMORY_NOTIFICATION /* pvNotification */ )
{
    return JET_errSuccess;
}

ERR ErrOSQueryMemoryNotification( HMEMORY_NOTIFICATION const /* pvNotification */,
                                  _Out_ BOOL * const          pfLowMemory )
{
    if ( pfLowMemory )
    {
        *pfLowMemory = fFalse;
    }
    return JET_errSuccess;
}

VOID OSUnregisterAndDestroyMemoryNotification( HMEMORY_NOTIFICATION const pvNotification )
{
    delete static_cast< PosixMemoryNotification * >( pvNotification );
}
