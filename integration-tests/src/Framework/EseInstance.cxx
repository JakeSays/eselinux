// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/EseInstance.hxx"

#include "Framework/Check.hxx"
#include "Framework/TemporaryDirectory.hxx"

#include <jetapi.h>

#include <atomic>
#include <format>
#include <string>
#include <vector>

namespace ese::tests
{

namespace
{

// Set a string-valued system parameter and check the result. Wraps
// JetSetSystemParameterA so the call site stays terse.
void SetStringParameter(JET_INSTANCE instance,
                        unsigned long parameterId,
                        const std::string& value)
{
    CheckJet(JetSetSystemParameterA(&instance,
                                    JET_sesidNil,
                                    parameterId,
                                    0,
                                    value.c_str()));
}

void SetIntegerParameter(JET_INSTANCE instance,
                         unsigned long parameterId,
                         JET_API_PTR value)
{
    CheckJet(JetSetSystemParameterA(&instance,
                                    JET_sesidNil,
                                    parameterId,
                                    value,
                                    nullptr));
}

} // namespace

namespace
{

// MultiInstance mode names every JET_INSTANCE uniquely across the
// process — JetCreateInstance2 rejects collisions on the live set,
// and the post-JetTerm release isn't visible at the API surface, so
// the safest answer is to never reuse a name within one process.
std::atomic<uint64_t> g_multiInstanceCounter{0};

} // namespace

EseInstance::EseInstance(const TemporaryDirectory& directory,
                         std::string_view instanceName,
                         JET_CALLBACK runtimeCallback,
                         EseInstanceMode mode,
                         EseInstanceOptions options)
    : _directory(directory.Path())
{
    // Path strings need a trailing slash so JET concatenates filenames
    // correctly. Build them once and reuse for the three path params.
    auto pathWithSeparator = _directory.string();
    if (!pathWithSeparator.empty() && pathWithSeparator.back() != '/')
    {
        pathWithSeparator.push_back('/');
    }

    const std::string instanceNameOwned(instanceName);

    if (mode == EseInstanceMode::MultiInstance)
    {
        // JetCreateInstance2 → per-instance params → JetInit.  Required
        // for scenarios that need more than one live instance.  ESE
        // permanently switches to multi-instance mode the first time
        // this is called in a process; from that point on, the no-
        // instance Global APIs (JetBeginExternalBackup, JetTruncateLog,
        // etc.) start returning JET_errRunningInMultiInstanceMode.
        const uint64_t instanceTag =
            g_multiInstanceCounter.fetch_add(1, std::memory_order_relaxed);
        const std::string uniqueInstanceName =
            std::format("{}-{}", instanceNameOwned, instanceTag);

        CheckJet(JetCreateInstance2A(&_handle,
                                     uniqueInstanceName.c_str(),
                                     uniqueInstanceName.c_str(),
                                     0));

        SetStringParameter(_handle, JET_paramSystemPath, pathWithSeparator);
        SetStringParameter(_handle, JET_paramTempPath, pathWithSeparator);
        SetStringParameter(_handle, JET_paramLogFilePath, pathWithSeparator);
        SetStringParameter(_handle, JET_paramBaseName, "edb");
        SetStringParameter(_handle, JET_paramEventSource, uniqueInstanceName);
        SetIntegerParameter(_handle, JET_paramCircularLog,
                            options.EnableCircularLog ? 1 : 0);

        if (runtimeCallback != nullptr)
        {
            SetIntegerParameter(_handle,
                                JET_paramRuntimeCallback,
                                reinterpret_cast<JET_API_PTR>(runtimeCallback));
        }

        CheckJet(JetInit(&_handle));
        return;
    }

    // SingleInstance default: set per-instance params against _handle
    // (still JET_instanceNil at this point) and let JetInit allocate
    // the real instance using those param values.  Setting params
    // against a pre-allocated handle (the JetCreateInstance2 pattern)
    // doesn't reliably propagate AssertAction into the OS-layer init
    // that JetInit triggers, AND it permanently flips ESE into
    // multi-instance mode, breaking the Global backup APIs.
    SetStringParameter(_handle, JET_paramSystemPath, pathWithSeparator);
    SetStringParameter(_handle, JET_paramTempPath, pathWithSeparator);
    SetStringParameter(_handle, JET_paramLogFilePath, pathWithSeparator);
    SetStringParameter(_handle, JET_paramBaseName, "edb");
    SetStringParameter(_handle, JET_paramEventSource, instanceNameOwned);

    // CircularLog keeps the log directory bounded — every scenario is
    // a one-shot process and we don't care about replay past it.
    // Backup-family scenarios (incremental/atomic/surrogate) opt out
    // via EseInstanceOptions::EnableCircularLog = false because the
    // engine rejects those backups under circular logging.
    SetIntegerParameter(_handle, JET_paramCircularLog,
                        options.EnableCircularLog ? 1 : 0);

    if (runtimeCallback != nullptr)
    {
        SetIntegerParameter(_handle,
                            JET_paramRuntimeCallback,
                            reinterpret_cast<JET_API_PTR>(runtimeCallback));
    }

    CheckJet(JetInit(&_handle));
}

EseInstance::~EseInstance()
{
    if (_handle != JET_instanceNil)
    {
        // JetTerm errors during teardown are not actionable — the
        // scenario already passed or failed by this point. Swallow.
        (void)JetTerm2(_handle, JET_bitTermComplete);
        _handle = JET_instanceNil;
    }
}

} // namespace ese::tests
