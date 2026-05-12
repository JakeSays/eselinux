// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/EseInstance.hxx"

#include "Framework/Check.hxx"
#include "Framework/TemporaryDirectory.hxx"

#include <jetapi.h>

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

EseInstance::EseInstance(const TemporaryDirectory& directory,
                         std::string_view instanceName,
                         JET_CALLBACK runtimeCallback)
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

    // BookStoreSample / Tier-2-runner pattern: set per-instance params
    // against _handle (still JET_instanceNil at this point) and let
    // JetInit allocate the real instance using those param values.
    // Setting params against a pre-allocated handle (the
    // JetCreateInstance2 pattern) doesn't reliably propagate
    // AssertAction into the OS-layer init that JetInit triggers.
    SetStringParameter(_handle, JET_paramSystemPath, pathWithSeparator);
    SetStringParameter(_handle, JET_paramTempPath, pathWithSeparator);
    SetStringParameter(_handle, JET_paramLogFilePath, pathWithSeparator);
    SetStringParameter(_handle, JET_paramBaseName, "edb");
    SetStringParameter(_handle, JET_paramEventSource, instanceNameOwned);

    // CircularLog keeps the log directory bounded — every scenario is
    // a one-shot process and we don't care about replay past it.
    SetIntegerParameter(_handle, JET_paramCircularLog, 1);

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
