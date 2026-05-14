// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Orchestrator.hxx"

#include "Framework/Check.hxx"

#include <jetapi.h>

#include <filesystem>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#include <sys/wait.h>

namespace ese::tests::replication
{

void SeedDatabaseInto(const std::filesystem::path& seedDirectory,
                      const char* databaseFileName)
{
    std::error_code errorCode;
    std::filesystem::create_directories(seedDirectory, errorCode);
    if (errorCode)
    {
        throw std::runtime_error(std::format(
            "create_directories({}) failed: {}",
            seedDirectory.string(), errorCode.message()));
    }

    auto pathWithSeparator = seedDirectory.string();
    if (!pathWithSeparator.empty() && pathWithSeparator.back() != '/')
    {
        pathWithSeparator.push_back('/');
    }

    JET_INSTANCE instanceHandle = JET_instanceNil;
    CheckJet(JetCreateInstance2A(&instanceHandle,
                                 "ReplicationSeed",
                                 "ReplicationSeed",
                                 0));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramSystemPath, 0,
                                    pathWithSeparator.c_str()));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramTempPath, 0,
                                    pathWithSeparator.c_str()));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramLogFilePath, 0,
                                    pathWithSeparator.c_str()));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramBaseName, 0, "edb"));
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramEventSource, 0,
                                    "ReplicationSeed"));
    //  Non-circular logging: the active needs every committed log
    //  generation to remain on disk so the emit callback can ship it.
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramCircularLog, 0, nullptr));
    //  Small log files so a small amount of work rolls generations.
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramLogFileSize, 64, nullptr));

    CheckJet(JetInit(&instanceHandle));

    JET_SESID sessionHandle = JET_sesidNil;
    CheckJet(JetBeginSessionA(instanceHandle,
                              &sessionHandle,
                              nullptr, nullptr));

    const auto databasePath = seedDirectory / databaseFileName;
    JET_DBID databaseId = JET_dbidNil;
    CheckJet(JetCreateDatabaseA(sessionHandle,
                                databasePath.string().c_str(),
                                nullptr, &databaseId,
                                JET_bitDbOverwriteExisting));

    //  Add a small table so passive scenarios have a place to read
    //  from after the active ships log data.  Schema is intentionally
    //  minimal — one Long column "Value" inside a "Rows" table.
    JET_TABLEID tableId = JET_tableidNil;
    CheckJet(JetCreateTableA(sessionHandle, databaseId,
                             "Rows", 8, 100, &tableId));
    JET_COLUMNDEF columnDefinition = {};
    columnDefinition.cbStruct = sizeof(columnDefinition);
    columnDefinition.coltyp = JET_coltypLong;
    JET_COLUMNID columnId = 0;
    CheckJet(JetAddColumnA(sessionHandle, tableId, "Value",
                           &columnDefinition, nullptr, 0, &columnId));

    CheckJet(JetCloseTable(sessionHandle, tableId));
    CheckJet(JetCloseDatabase(sessionHandle, databaseId, 0));
    CheckJet(JetDetachDatabaseA(sessionHandle,
                                databasePath.string().c_str()));
    CheckJet(JetEndSession(sessionHandle, 0));
    CheckJet(JetTerm2(instanceHandle, JET_bitTermComplete));
}

void CloneDirectory(const std::filesystem::path& sourceDirectory,
                    const std::filesystem::path& destinationDirectory)
{
    std::error_code errorCode;
    std::filesystem::create_directories(destinationDirectory, errorCode);
    if (errorCode)
    {
        throw std::runtime_error(std::format(
            "create_directories({}) failed: {}",
            destinationDirectory.string(), errorCode.message()));
    }
    for (const auto& entry :
         std::filesystem::directory_iterator(sourceDirectory))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }
        const auto target = destinationDirectory / entry.path().filename();
        std::filesystem::copy_file(
            entry.path(), target,
            std::filesystem::copy_options::overwrite_existing,
            errorCode);
        if (errorCode)
        {
            throw std::runtime_error(std::format(
                "copy_file({} -> {}) failed: {}",
                entry.path().string(), target.string(),
                errorCode.message()));
        }
    }
}

void RequireCleanExit(ChildProcess& child, std::string_view childLabel)
{
    const int status = child.WaitForExit();
    if (WIFEXITED(status))
    {
        const int exitCode = WEXITSTATUS(status);
        if (exitCode != 0)
        {
            throw std::runtime_error(std::format(
                "child '{}' (pid {}) exited with status {}",
                childLabel, static_cast<long>(child.Pid()), exitCode));
        }
        return;
    }
    if (WIFSIGNALED(status))
    {
        throw std::runtime_error(std::format(
            "child '{}' (pid {}) killed by signal {}",
            childLabel, static_cast<long>(child.Pid()),
            WTERMSIG(status)));
    }
    throw std::runtime_error(std::format(
        "child '{}' (pid {}) terminated abnormally (status=0x{:x})",
        childLabel, static_cast<long>(child.Pid()), status));
}

} // namespace ese::tests::replication
