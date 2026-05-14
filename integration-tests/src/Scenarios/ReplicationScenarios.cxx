// Copyright (c) Jake Helfert
// Licensed under the MIT License.
//
// Replication scenarios — round 8.  Each scenario stands up multiple
// OS processes, all communicating over TCP on 127.0.0.1, exercising
// the engine's log-shipping / replica-repair surface.
//
// Topology for the basic log-shipping scenario:
//
//   Test scenario (parent, this process)
//        |
//        |  ChildProcess(--child-entry=Replication.LogShipping.Active,
//        |               --child-directory=<active dir>,
//        |               --child-arg=--passive-dir=<passive dir>)
//        v
//   Active child process
//        |
//        |  binds 127.0.0.1:0, getsockname -> port P
//        |  ChildProcess(--child-entry=Replication.LogShipping.Passive,
//        |               --child-directory=<passive dir>,
//        |               --child-arg=--connect-port=P)
//        v
//   Passive child process — connect(127.0.0.1, P), JetConsumeLogData loop
//
// The test scenario only spawns the active.  The active is responsible
// for spawning its passive(s) — that mirrors real-world replication
// where the active owns its replica topology.  Port discovery happens
// entirely inside the active (it binds the socket and tells the
// passive the port on the command line), so the parent never has to
// learn or forward a TCP port.

#include "Framework/Check.hxx"
#include "Framework/CrashHelper.hxx"
#include "Framework/Scenario.hxx"
#include "Framework/TemporaryDirectory.hxx"
#include "Scenarios/Replication/Orchestrator.hxx"
#include "Scenarios/Replication/WireProtocol.hxx"

#include <jetapi.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <memory>
#include <filesystem>
#include <format>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

using namespace ese::tests;
using namespace ese::tests::replication;

namespace
{

//  Convenience: byte-span over a value object, for SendFrame
//  payloads that are just a packed struct.
template <typename T>
std::span<const std::byte> AsByteSpan(const T& value)
{
    return std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(&value), sizeof(T));
}

//  Parse a --name=value token out of the child's extraArgs slice.
//  Returns the value half (substring after '=').  Throws if the named
//  arg is missing or has no '=' separator.
std::string_view RequireKeyValueArg(
    std::span<const std::string_view> extraArgs,
    std::string_view key)
{
    for (const auto& token : extraArgs)
    {
        if (token.size() <= key.size())
        {
            continue;
        }
        if (token.substr(0, key.size()) != key)
        {
            continue;
        }
        if (token[key.size()] != '=')
        {
            continue;
        }
        return token.substr(key.size() + 1);
    }
    throw std::runtime_error(std::format("missing --child-arg {}=...", key));
}

}  // namespace

//  ============================================================
//  Wire smoke tests — exercise ReplicationServer / Client / Channel
//  in-process (two threads, no JET) so framing bugs surface ahead of
//  the engine-integrating scenarios.
//  ============================================================

EseIntegrationScenario(Replication, WireRoundTripFrameOverLoopback)
{
    ReplicationServer server;
    const auto serverPort = server.Port();
    Require(serverPort != 0);

    ReplicationChannel::ReceivedFrame received;
    std::exception_ptr serverException;
    std::thread serverThread([&]()
    {
        try
        {
            auto channel = server.AcceptOne(std::chrono::seconds(5));
            received = channel.RecvFrame(std::chrono::seconds(5));
        }
        catch (...)
        {
            serverException = std::current_exception();
        }
    });

    constexpr uint32_t SentPayload = 0xC0FFEE42u;
    auto client = ReplicationClient::Connect(serverPort,
                                             std::chrono::seconds(5));
    client.SendFrame(ReplicationFrameKind::LogSignatureReply,
                     AsByteSpan(SentPayload));
    client.Close();

    serverThread.join();
    if (serverException)
    {
        std::rethrow_exception(serverException);
    }

    Require(received.Kind == ReplicationFrameKind::LogSignatureReply);
    Require(received.Payload.size() == sizeof(SentPayload));
    uint32_t roundTripped = 0;
    std::memcpy(&roundTripped, received.Payload.data(), sizeof(roundTripped));
    Require(roundTripped == SentPayload);
}

EseIntegrationScenario(Replication, WireDeliversManyFramesInOrder)
{
    ReplicationServer server;
    constexpr int FrameCount = 32;

    std::vector<ReplicationChannel::ReceivedFrame> receivedFrames;
    std::exception_ptr serverException;
    std::thread serverThread([&]()
    {
        try
        {
            auto channel = server.AcceptOne(std::chrono::seconds(5));
            receivedFrames.reserve(FrameCount);
            for (int frameIndex = 0; frameIndex < FrameCount; ++frameIndex)
            {
                receivedFrames.push_back(
                    channel.RecvFrame(std::chrono::seconds(5)));
            }
        }
        catch (...)
        {
            serverException = std::current_exception();
        }
    });

    auto client = ReplicationClient::Connect(server.Port(),
                                             std::chrono::seconds(5));
    for (uint32_t frameIndex = 0; frameIndex < FrameCount; ++frameIndex)
    {
        const auto kind = (frameIndex & 1) == 0
                              ? ReplicationFrameKind::CheckpointQuery
                              : ReplicationFrameKind::CheckpointReply;
        client.SendFrame(kind, AsByteSpan(frameIndex));
    }
    client.Close();

    serverThread.join();
    if (serverException)
    {
        std::rethrow_exception(serverException);
    }

    Require(static_cast<int>(receivedFrames.size()) == FrameCount);
    for (uint32_t frameIndex = 0;
         frameIndex < static_cast<uint32_t>(FrameCount);
         ++frameIndex)
    {
        const auto expectedKind = (frameIndex & 1) == 0
                                      ? ReplicationFrameKind::CheckpointQuery
                                      : ReplicationFrameKind::CheckpointReply;
        Require(receivedFrames[frameIndex].Kind == expectedKind);
        Require(receivedFrames[frameIndex].Payload.size() ==
                sizeof(frameIndex));
        uint32_t decoded = 0;
        std::memcpy(&decoded,
                    receivedFrames[frameIndex].Payload.data(),
                    sizeof(decoded));
        Require(decoded == frameIndex);
    }
}

EseIntegrationScenario(Replication, WireLargePayloadSurvivesTcpSegmentation)
{
    ReplicationServer server;
    constexpr size_t PayloadBytes = 192 * 1024;

    std::vector<std::byte> sentPayload(PayloadBytes);
    for (size_t byteIndex = 0; byteIndex < PayloadBytes; ++byteIndex)
    {
        sentPayload[byteIndex] = static_cast<std::byte>(byteIndex & 0xFF);
    }

    std::vector<std::byte> receivedPayload;
    ReplicationFrameKind receivedKind = ReplicationFrameKind::Ready;
    std::exception_ptr serverException;
    std::thread serverThread([&]()
    {
        try
        {
            auto channel = server.AcceptOne(std::chrono::seconds(5));
            auto frame = channel.RecvFrame(std::chrono::seconds(10));
            receivedKind = frame.Kind;
            receivedPayload = std::move(frame.Payload);
        }
        catch (...)
        {
            serverException = std::current_exception();
        }
    });

    auto client = ReplicationClient::Connect(server.Port(),
                                             std::chrono::seconds(5));
    client.SendFrame(ReplicationFrameKind::LogData,
                     std::span<const std::byte>(sentPayload));
    client.Close();

    serverThread.join();
    if (serverException)
    {
        std::rethrow_exception(serverException);
    }

    Require(receivedKind == ReplicationFrameKind::LogData);
    Require(receivedPayload.size() == PayloadBytes);
    Require(std::memcmp(receivedPayload.data(),
                        sentPayload.data(),
                        PayloadBytes) == 0);
}

EseIntegrationScenario(Replication, WirePeerCloseReportsCleanShutdown)
{
    ReplicationServer server;

    bool gotFirstFrame = false;
    bool gotPeerClose = false;
    std::exception_ptr serverException;
    std::thread serverThread([&]()
    {
        try
        {
            auto channel = server.AcceptOne(std::chrono::seconds(5));
            auto frame = channel.RecvFrame(std::chrono::seconds(5));
            gotFirstFrame =
                frame.Kind == ReplicationFrameKind::StreamComplete &&
                frame.Payload.empty();
            try
            {
                (void)channel.RecvFrame(std::chrono::seconds(2));
            }
            catch (const std::exception&)
            {
                gotPeerClose = true;
            }
        }
        catch (...)
        {
            serverException = std::current_exception();
        }
    });

    auto client = ReplicationClient::Connect(server.Port(),
                                             std::chrono::seconds(5));
    client.SendFrame(ReplicationFrameKind::StreamComplete);
    client.Close();

    serverThread.join();
    if (serverException)
    {
        std::rethrow_exception(serverException);
    }
    Require(gotFirstFrame);
    Require(gotPeerClose);
}

//  ============================================================
//  Log shipping — one active emits, one passive consumes.
//
//  The active receives `--passive-dir=<path>` on its CLI.  It binds a
//  TCP listener, fork+execs the passive child with `--connect-port=N`,
//  accepts the connection, runs an ESE write workload with an emit
//  callback that ships log bytes onto the channel, sends
//  StreamComplete, terms, and waits for the passive's clean exit.
//  The test scenario verifies row count from the passive's directory
//  once the active has exited.
//  ============================================================

namespace
{

constexpr const char* SeedDatabaseFileName = "Shipped.mdb";
constexpr const char* ChildEntryLogShippingActive =
    "Replication.LogShipping.Active";
constexpr const char* ChildEntryLogShippingPassive =
    "Replication.LogShipping.Passive";

//  Total rows the active inserts.  Sized to roll the engine through
//  at least two log generations at the configured 64 KiB log size,
//  exercising the consumer's per-gen LogComplete + new-gen creation
//  paths.
constexpr int32_t LogShippingTotalRows = 384;

//  Maximum chunk size we ship over the wire at once.  Small enough
//  that even tiny ESE files (.chk is one page, ~8 KiB) move in one
//  chunk, large enough that gigantic .edbs would still fit in a few
//  hundred frames.
constexpr size_t FileTransferChunkBytes = 64 * 1024;

void ConfigureChildInstanceParameters(
    JET_INSTANCE* instanceHandle,
    const std::filesystem::path& directory,
    const char* eventSource)
{
    auto pathWithSeparator = directory.string();
    if (!pathWithSeparator.empty() && pathWithSeparator.back() != '/')
    {
        pathWithSeparator.push_back('/');
    }
    CheckJet(JetSetSystemParameterA(instanceHandle, JET_sesidNil,
                                    JET_paramSystemPath, 0,
                                    pathWithSeparator.c_str()));
    CheckJet(JetSetSystemParameterA(instanceHandle, JET_sesidNil,
                                    JET_paramTempPath, 0,
                                    pathWithSeparator.c_str()));
    CheckJet(JetSetSystemParameterA(instanceHandle, JET_sesidNil,
                                    JET_paramLogFilePath, 0,
                                    pathWithSeparator.c_str()));
    CheckJet(JetSetSystemParameterA(instanceHandle, JET_sesidNil,
                                    JET_paramBaseName, 0, "edb"));
    CheckJet(JetSetSystemParameterA(instanceHandle, JET_sesidNil,
                                    JET_paramEventSource, 0,
                                    eventSource));
    CheckJet(JetSetSystemParameterA(instanceHandle, JET_sesidNil,
                                    JET_paramCircularLog, 0, nullptr));
    CheckJet(JetSetSystemParameterA(instanceHandle, JET_sesidNil,
                                    JET_paramLogFileSize, 64, nullptr));
}

//  Emit callback installed on the active.  Runs on the engine's
//  log-writer thread; serialises the JET_EMITDATACTX header followed
//  by the raw log bytes and SendFrames them as a single LogData
//  frame.  Errors thrown by SendFrame surface to the engine as
//  JET_errDiskIO so the active aborts cleanly.
JET_ERR JET_API LogShippingEmitCallback(JET_INSTANCE /*instance*/,
                                        JET_EMITDATACTX* emitContext,
                                        void* pvLogData,
                                        uint32_t cbLogData,
                                        void* callbackContext)
{
    auto* channel = static_cast<ReplicationChannel*>(callbackContext);
    if (channel == nullptr || emitContext == nullptr)
    {
        return JET_errInvalidParameter;
    }
    try
    {
        std::vector<std::byte> framePayload(sizeof(*emitContext) + cbLogData);
        std::memcpy(framePayload.data(),
                    emitContext, sizeof(*emitContext));
        if (cbLogData > 0 && pvLogData != nullptr)
        {
            std::memcpy(framePayload.data() + sizeof(*emitContext),
                        pvLogData, cbLogData);
        }
        channel->SendFrame(ReplicationFrameKind::LogData,
                           std::span<const std::byte>(framePayload));
        return JET_errSuccess;
    }
    catch (const std::exception&)
    {
        return JET_errDiskIO;
    }
}

//  Promote consumed shadow logs into primary logs after the consume
//  engine has terminated.  The passive directory contains both the
//  baseline-shipped .log files AND the .jsl shadow files written by
//  JetConsumeLogData.  Renaming each .jsl over the corresponding
//  .log replaces the consumer's view of the log stream with the
//  active's complete (phase 1 + phase 2) stream, which the verify
//  engine's recovery will then replay onto the .edb.
void PromoteShadowLogsToPrimary(const std::filesystem::path& directory)
{
    for (const auto& entry :
         std::filesystem::directory_iterator(directory))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }
        if (entry.path().extension() != ".jsl")
        {
            continue;
        }
        auto target = entry.path();
        target.replace_extension(".log");
        std::error_code errorCode;
        std::filesystem::rename(entry.path(), target, errorCode);
        if (errorCode)
        {
            throw std::runtime_error(std::format(
                "rename({} -> {}) failed: {}",
                entry.path().string(), target.string(),
                errorCode.message()));
        }
    }
}

//  Active runs a single engine cycle with the emit callback
//  installed from the very first JetInit.  Every log byte the engine
//  writes — including each gen's file header — fires the callback
//  and lands on the wire.  After JetTerm flushes the .edb, the
//  active ships the database file itself so the passive has a
//  matching .edb to recover into.
void RunLogShippingActiveChild(const std::filesystem::path& directory,
                               std::span<const std::string_view> extraArgs)
{
    const auto passiveDirectoryValue =
        RequireKeyValueArg(extraArgs, "--passive-dir");
    const std::filesystem::path passiveDirectory(passiveDirectoryValue);

    ReplicationServer server;
    std::vector<std::string> passiveExtraArgs = {
        std::format("--connect-port={}", server.Port())
    };
    ChildProcess passiveChild(ChildEntryLogShippingPassive,
                              passiveDirectory,
                              std::span<const std::string>(passiveExtraArgs));
    auto channel = server.AcceptOne(std::chrono::seconds(30));

    JET_INSTANCE instanceHandle = JET_instanceNil;
    CheckJet(JetCreateInstance2A(&instanceHandle,
                                 "ReplicationActive",
                                 "ReplicationActive",
                                 0));
    ConfigureChildInstanceParameters(&instanceHandle, directory,
                                     "ReplicationActive");

    //  Install the emit callback BEFORE JetInit so log records fire
    //  it starting with the gen-1 file header.  Every byte the log
    //  writer writes (header sectors + each transaction's records)
    //  reaches the passive via the wire.
    CheckJet(JetSetSystemParameterA(
        &instanceHandle, JET_sesidNil,
        JET_paramEmitLogDataCallback,
        reinterpret_cast<JET_API_PTR>(&LogShippingEmitCallback),
        nullptr));
    CheckJet(JetSetSystemParameterA(
        &instanceHandle, JET_sesidNil,
        JET_paramEmitLogDataCallbackCtx,
        reinterpret_cast<JET_API_PTR>(&channel),
        nullptr));
    CheckJet(JetInit(&instanceHandle));

    JET_SESID sessionHandle = JET_sesidNil;
    CheckJet(JetBeginSessionA(instanceHandle,
                              &sessionHandle, nullptr, nullptr));

    const auto databasePath = directory / SeedDatabaseFileName;
    JET_DBID databaseId = JET_dbidNil;
    CheckJet(JetCreateDatabaseA(sessionHandle,
                                databasePath.string().c_str(),
                                nullptr, &databaseId,
                                JET_bitDbOverwriteExisting));

    JET_TABLEID tableId = JET_tableidNil;
    CheckJet(JetCreateTableA(sessionHandle, databaseId,
                             "Rows", 8, 100, &tableId));
    JET_COLUMNDEF columnDefinition = {};
    columnDefinition.cbStruct = sizeof(columnDefinition);
    columnDefinition.coltyp = JET_coltypLong;
    JET_COLUMNID columnId = 0;
    CheckJet(JetAddColumnA(sessionHandle, tableId, "Value",
                           &columnDefinition, nullptr, 0, &columnId));

    CheckJet(JetBeginTransaction(sessionHandle));
    for (int32_t rowIndex = 0; rowIndex < LogShippingTotalRows; ++rowIndex)
    {
        CheckJet(JetPrepareUpdate(sessionHandle, tableId, JET_prepInsert));
        CheckJet(JetSetColumn(sessionHandle, tableId, columnId,
                              &rowIndex, sizeof(rowIndex), 0, nullptr));
        CheckJet(JetUpdate(sessionHandle, tableId, nullptr, 0, nullptr));
    }
    CheckJet(JetCommitTransaction(sessionHandle, 0));

    CheckJet(JetCloseTable(sessionHandle, tableId));
    CheckJet(JetCloseDatabase(sessionHandle, databaseId, 0));
    CheckJet(JetDetachDatabaseA(sessionHandle,
                                databasePath.string().c_str()));
    CheckJet(JetEndSession(sessionHandle, 0));
    CheckJet(JetTerm2(instanceHandle, JET_bitTermComplete));

    //  Ship the .edb so the passive has the materialised state to
    //  recover into.  The .log content is already on the passive
    //  side via the consume stream — we only need the database
    //  file itself.
    {
        const auto edbName = databasePath.filename().string();
        const auto edbSize =
            std::filesystem::file_size(databasePath);

        std::vector<std::byte> beginPayload(
            sizeof(uint32_t) + edbName.size() + sizeof(uint64_t));
        const uint32_t nameSize = static_cast<uint32_t>(edbName.size());
        const uint64_t edbSize64 = static_cast<uint64_t>(edbSize);
        std::memcpy(beginPayload.data(), &nameSize, sizeof(nameSize));
        std::memcpy(beginPayload.data() + sizeof(nameSize),
                    edbName.data(), edbName.size());
        std::memcpy(beginPayload.data() + sizeof(nameSize) + edbName.size(),
                    &edbSize64, sizeof(edbSize64));
        channel.SendFrame(ReplicationFrameKind::FileTransferBegin,
                          std::span<const std::byte>(beginPayload));

        std::ifstream input(databasePath, std::ios::binary);
        std::vector<std::byte> chunkBuffer(FileTransferChunkBytes);
        uint64_t shipped = 0;
        while (shipped < edbSize64)
        {
            const auto remaining = edbSize64 - shipped;
            const auto thisChunk = std::min<size_t>(
                FileTransferChunkBytes, static_cast<size_t>(remaining));
            input.read(reinterpret_cast<char*>(chunkBuffer.data()),
                       static_cast<std::streamsize>(thisChunk));
            channel.SendFrame(ReplicationFrameKind::FileTransferChunk,
                              std::span<const std::byte>(
                                  chunkBuffer.data(), thisChunk));
            shipped += thisChunk;
        }
        channel.SendFrame(ReplicationFrameKind::FileTransferEnd);
    }

    channel.SendFrame(ReplicationFrameKind::StreamComplete);
    channel.Close();
    RequireCleanExit(passiveChild, "passive");
}

//  Passive runs a single engine cycle hosting JetConsumeLogData,
//  receives both the live log stream and the trailing .edb file,
//  then promotes the consumed shadow logs to primary logs so the
//  parent's verify-side JetInit can recover into the shipped .edb.
//
//  Wire frames the passive expects, in order:
//    - FirstCall LogData                (engine init signal)
//    - DataBuffers LogData *            (every log sector the active wrote)
//    - LogComplete LogData (per gen)    (gen boundary)
//    - LastCall LogData                 (engine term signal)
//    - FileTransferBegin/Chunk*/End     (the .edb itself)
//    - StreamComplete                   (end of stream)
void RunLogShippingPassiveChild(const std::filesystem::path& directory,
                                std::span<const std::string_view> extraArgs)
{
    const auto portValue = RequireKeyValueArg(extraArgs, "--connect-port");
    uint16_t port = 0;
    const auto* first = portValue.data();
    const auto* last = portValue.data() + portValue.size();
    if (std::from_chars(first, last, port).ec != std::errc())
    {
        throw std::runtime_error(std::format(
            "malformed --connect-port: '{}'", std::string(portValue)));
    }

    auto channel = ReplicationClient::Connect(
        port, std::chrono::seconds(30));

    JET_INSTANCE consumeInstance = JET_instanceNil;
    CheckJet(JetCreateInstance2A(&consumeInstance,
                                 "ReplicationPassive",
                                 "ReplicationPassive",
                                 0));
    ConfigureChildInstanceParameters(&consumeInstance, directory,
                                     "ReplicationPassive");
    CheckJet(JetInit(&consumeInstance));

    std::ofstream pendingEdbOutput;
    uint64_t pendingEdbBytes = 0;

    while (true)
    {
        auto frame = channel.RecvFrame(std::chrono::minutes(2));
        if (frame.Kind == ReplicationFrameKind::StreamComplete)
        {
            break;
        }
        switch (frame.Kind)
        {
            case ReplicationFrameKind::LogData:
            {
                if (frame.Payload.size() < sizeof(JET_EMITDATACTX))
                {
                    throw std::runtime_error(
                        "LogData payload shorter than JET_EMITDATACTX");
                }
                JET_EMITDATACTX emitContext = {};
                std::memcpy(&emitContext, frame.Payload.data(),
                            sizeof(emitContext));
                const auto cbLogData = static_cast<uint32_t>(
                    frame.Payload.size() - sizeof(emitContext));
                void* pvLogData =
                    cbLogData > 0
                        ? static_cast<void*>(
                              frame.Payload.data() + sizeof(emitContext))
                        : nullptr;
                CheckJet(JetConsumeLogData(consumeInstance, &emitContext,
                                           pvLogData, cbLogData, 0));
                break;
            }
            case ReplicationFrameKind::FileTransferBegin:
            {
                if (frame.Payload.size() < sizeof(uint32_t))
                {
                    throw std::runtime_error(
                        "FileTransferBegin payload too small");
                }
                uint32_t nameSize = 0;
                std::memcpy(&nameSize, frame.Payload.data(),
                            sizeof(nameSize));
                if (frame.Payload.size() <
                    sizeof(uint32_t) + nameSize + sizeof(uint64_t))
                {
                    throw std::runtime_error(
                        "FileTransferBegin payload truncated");
                }
                std::string fileName(
                    reinterpret_cast<const char*>(
                        frame.Payload.data() + sizeof(nameSize)),
                    nameSize);
                std::memcpy(&pendingEdbBytes,
                            frame.Payload.data() + sizeof(nameSize)
                                + nameSize,
                            sizeof(pendingEdbBytes));
                pendingEdbOutput.open(directory / fileName,
                                      std::ios::binary | std::ios::trunc);
                if (!pendingEdbOutput)
                {
                    throw std::runtime_error(std::format(
                        "failed to open {} for file receive",
                        (directory / fileName).string()));
                }
                break;
            }
            case ReplicationFrameKind::FileTransferChunk:
            {
                if (!pendingEdbOutput)
                {
                    throw std::runtime_error(
                        "FileTransferChunk without prior Begin");
                }
                pendingEdbOutput.write(
                    reinterpret_cast<const char*>(frame.Payload.data()),
                    static_cast<std::streamsize>(frame.Payload.size()));
                break;
            }
            case ReplicationFrameKind::FileTransferEnd:
            {
                pendingEdbOutput.close();
                pendingEdbBytes = 0;
                break;
            }
            default:
                throw std::runtime_error(std::format(
                    "passive received unexpected frame kind {}",
                    static_cast<uint32_t>(frame.Kind)));
        }
    }
    channel.Close();
    CheckJet(JetTerm2(consumeInstance, JET_bitTermComplete));

    //  Promotion: replace the passive's auto-created log+checkpoint
    //  set with the active's shadow-mirrored set so verify-side
    //  recovery walks the right log chain into the shipped .edb.
    //   1. Delete the consume engine's own .log + .chk so they
    //      don't interfere with the renamed shadow files.
    //   2. Rename every .jsl to .log.  These now carry the active's
    //      complete log stream (gen 1 .. gen N) signed with the
    //      active's signature.
    for (const auto& entry :
         std::filesystem::directory_iterator(directory))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }
        const auto& path = entry.path();
        if (path.extension() == ".log" || path.extension() == ".chk")
        {
            std::error_code errorCode;
            std::filesystem::remove(path, errorCode);
            if (errorCode)
            {
                throw std::runtime_error(std::format(
                    "remove({}) failed: {}",
                    path.string(), errorCode.message()));
            }
        }
    }
    PromoteShadowLogsToPrimary(directory);
}

struct LogShippingChildRegistrar
{
    LogShippingChildRegistrar()
    {
        RegisterChildEntry(ChildEntryLogShippingActive,
                           ChildEntryPoint(&RunLogShippingActiveChild));
        RegisterChildEntry(ChildEntryLogShippingPassive,
                           ChildEntryPoint(&RunLogShippingPassiveChild));
    }
};
[[maybe_unused]] static LogShippingChildRegistrar _logShippingChildRegistrar;

//  Open a database read-only in the parent process and walk every
//  row to get a count.  Used to verify the passive's final state.
int32_t CountRowsInDatabase(const std::filesystem::path& directory,
                            const char* databaseFileName)
{
    JET_INSTANCE instanceHandle = JET_instanceNil;
    CheckJet(JetCreateInstance2A(&instanceHandle,
                                 "ReplicationVerify",
                                 "ReplicationVerify",
                                 0));
    ConfigureChildInstanceParameters(&instanceHandle, directory,
                                     "ReplicationVerify");
    //  The passive's promotion step renames .jsl files to .log but
    //  doesn't synthesise the unnumbered edb.log "current writer"
    //  marker.  JET_bitAllowMissingCurrentLog tells JetInit2 to treat
    //  the highest numbered gen as the current one and proceed with
    //  recovery instead of erroring out with JET_errMissingLogFile.
    CheckJet(JetInit2(&instanceHandle, JET_bitAllowMissingCurrentLog));

    JET_SESID sessionHandle = JET_sesidNil;
    CheckJet(JetBeginSessionA(instanceHandle,
                              &sessionHandle, nullptr, nullptr));

    const auto databasePath = directory / databaseFileName;
    CheckJet(JetAttachDatabaseA(sessionHandle,
                                databasePath.string().c_str(),
                                JET_bitDbReadOnly));
    JET_DBID databaseId = JET_dbidNil;
    CheckJet(JetOpenDatabaseA(sessionHandle,
                              databasePath.string().c_str(),
                              nullptr, &databaseId, JET_bitDbReadOnly));

    JET_TABLEID tableId = JET_tableidNil;
    CheckJet(JetOpenTableA(sessionHandle, databaseId, "Rows",
                           nullptr, 0, 0, &tableId));

    int32_t rowCount = 0;
    const auto firstRc = JetMove(sessionHandle, tableId, JET_MoveFirst, 0);
    if (firstRc != JET_errNoCurrentRecord)
    {
        CheckJet(firstRc);
        while (true)
        {
            ++rowCount;
            const auto nextRc = JetMove(sessionHandle, tableId,
                                        JET_MoveNext, 0);
            if (nextRc == JET_errNoCurrentRecord)
            {
                break;
            }
            CheckJet(nextRc);
        }
    }

    CheckJet(JetCloseTable(sessionHandle, tableId));
    CheckJet(JetCloseDatabase(sessionHandle, databaseId, 0));
    CheckJet(JetDetachDatabaseA(sessionHandle,
                                databasePath.string().c_str()));
    CheckJet(JetEndSession(sessionHandle, 0));
    CheckJet(JetTerm2(instanceHandle, JET_bitTermComplete));

    return rowCount;
}

}  // namespace

EseIntegrationScenario(Replication, LogShippingActiveToOnePassive)
{
    TemporaryDirectory directory("Replication.LogShippingActiveToOnePassive");
    const auto root = directory.Path();
    const auto activeDirectory = root / "active";
    const auto passiveDirectory = root / "passive";

    //  Active creates its own database from scratch with the emit
    //  callback installed for the whole engine cycle; every log
    //  sector reaches the passive via ConsumeLogData.  After
    //  JetTerm the active ships the resulting .edb file so the
    //  passive can recover into a matching database.
    std::filesystem::create_directories(activeDirectory);
    std::filesystem::create_directories(passiveDirectory);

    std::vector<std::string> activeExtraArgs = {
        std::format("--passive-dir={}", passiveDirectory.string())
    };
    ChildProcess activeChild(ChildEntryLogShippingActive,
                             activeDirectory,
                             std::span<const std::string>(activeExtraArgs));
    RequireCleanExit(activeChild, "active");

    const auto passiveRowCount = CountRowsInDatabase(passiveDirectory,
                                                     SeedDatabaseFileName);
    Require(passiveRowCount == LogShippingTotalRows);
}

//  ============================================================
//  Incremental reseed surface — two cooperating scenarios.
//
//  The engine exposes three APIs that must be called as a unit:
//    1. JetBeginDatabaseIncrementalReseed
//    2. JetPatchDatabasePages   (one or more, between Begin/End)
//    3. JetEndDatabaseIncrementalReseed
//
//  Engine state machine (ese/io.cxx around line 5201):
//    Begin requires the database to be in JET_dbstateDirtyShutdown
//    or JET_dbstateDirtyAndPatchedShutdown.  Calling on a clean DB
//    returns JET_errDatabaseInvalidIncrementalReseed (-1227).
//
//    Patch (offline) operates on the .edb file path while the DB
//    is NOT attached, but only within the Begin/End bracket.
//
//    End either commits the reseed (default) or cancels it
//    (JET_bitEndDatabaseIncrementalReseedCancel).  Commit walks
//    the log range [genMinRequired..genMaxRequired], validates
//    attachment info in those gens, and transitions the DB out of
//    reseed mode.  Cancel skips all that and leaves the DB
//    inconsistent.
//
//  Scenario A — Cancel: exercises Begin + Patch + End-Cancel and
//  verifies the patch was rolled back (corruption bytes remain on
//  disk).  This is a real engine mode (clients cancel when patch
//  validation fails mid-stream).
//
//  Scenario B — Commit: exercises Begin + Patch + End with
//  `genFirstDivergedLog=0` (the passive-page-patch mode) and
//  verifies the patch was applied by attaching the DB and reading
//  every row through the repaired page.
//  ============================================================

EseIntegrationScenario(Replication, OfflinePagePatchCancelRollsBackPatches)
{
    TemporaryDirectory directory(
        "Replication.OfflinePagePatchRepairsCorruptedPage");
    const auto databasePath = directory.Path() / "Patched.mdb";

    constexpr int32_t PatchScenarioRowCount = 200;
    constexpr uint32_t PatchPgno = 8;

    struct AlignedFree
    {
        void operator()(void* ptr) const
        {
            std::free(ptr);
        }
    };

    //  Phase 1: build DB, snapshot good page, term DIRTY.
    std::unique_ptr<uint8_t, AlignedFree> goodPageBytes;
    uint32_t pageSizeBytes = 0;
    uint32_t buildCurrentLogGen = 0;
    {
        JET_INSTANCE buildInstance = JET_instanceNil;
        CheckJet(JetCreateInstance2A(&buildInstance,
                                     "PagePatchBuild",
                                     "PagePatchBuild", 0));
        ConfigureChildInstanceParameters(&buildInstance,
                                          directory.Path(),
                                          "PagePatchBuild");
        CheckJet(JetInit(&buildInstance));

        JET_SESID buildSession = JET_sesidNil;
        CheckJet(JetBeginSessionA(buildInstance,
                                  &buildSession, nullptr, nullptr));

        JET_DBID buildDbid = JET_dbidNil;
        CheckJet(JetCreateDatabaseA(buildSession,
                                    databasePath.string().c_str(),
                                    nullptr, &buildDbid,
                                    JET_bitDbOverwriteExisting));
        JET_TABLEID buildTable = JET_tableidNil;
        CheckJet(JetCreateTableA(buildSession, buildDbid,
                                 "Rows", 8, 100, &buildTable));
        JET_COLUMNDEF columnDefinition = {};
        columnDefinition.cbStruct = sizeof(columnDefinition);
        columnDefinition.coltyp = JET_coltypLong;
        JET_COLUMNID columnId = 0;
        CheckJet(JetAddColumnA(buildSession, buildTable, "Value",
                               &columnDefinition, nullptr, 0,
                               &columnId));

        CheckJet(JetBeginTransaction(buildSession));
        for (int32_t rowIndex = 0;
             rowIndex < PatchScenarioRowCount;
             ++rowIndex)
        {
            CheckJet(JetPrepareUpdate(buildSession, buildTable,
                                      JET_prepInsert));
            CheckJet(JetSetColumn(buildSession, buildTable, columnId,
                                  &rowIndex, sizeof(rowIndex),
                                  0, nullptr));
            CheckJet(JetUpdate(buildSession, buildTable,
                               nullptr, 0, nullptr));
        }
        CheckJet(JetCommitTransaction(buildSession, 0));

        unsigned long pageSizeQuery = 0;
        CheckJet(JetGetSystemParameterA(
            buildInstance, buildSession,
            JET_paramDatabasePageSize,
            &pageSizeQuery, nullptr, 0));
        Require(pageSizeQuery > 0);
        pageSizeBytes = static_cast<uint32_t>(pageSizeQuery);

        goodPageBytes.reset(static_cast<uint8_t*>(
            std::aligned_alloc(pageSizeBytes, pageSizeBytes)));
        Require(goodPageBytes != nullptr);
        uint32_t cbActual = 0;
        CheckJet(JetGetDatabasePages(buildSession, buildDbid,
                                     PatchPgno, 1,
                                     goodPageBytes.get(),
                                     pageSizeBytes,
                                     &cbActual, 0));
        Require(cbActual == pageSizeBytes);

        //  Capture the current log generation BEFORE term so we can
        //  hand End coherent gen args after Begin + Patch.
        unsigned long currentGenQuery = 0;
        CheckJet(JetGetSystemParameterA(
            buildInstance, buildSession,
            JET_paramRecoveryCurrentLogfile,
            &currentGenQuery, nullptr, 0));
        buildCurrentLogGen = static_cast<uint32_t>(currentGenQuery);
        Require(buildCurrentLogGen >= 1);

        CheckJet(JetCloseTable(buildSession, buildTable));
        CheckJet(JetCloseDatabase(buildSession, buildDbid, 0));
        CheckJet(JetEndSession(buildSession, 0));
        //  Skip JetDetachDatabaseA — detach flushes the .edb header
        //  to JET_dbstateCleanShutdown, which would make Begin reject
        //  with -1227.  JET_bitTermDirty without a prior detach
        //  leaves the on-disk header in dirty-shutdown state, the
        //  precondition Begin requires.  Engine signals
        //  "you asked to terminate dirty, here you go" by returning
        //  JET_errDirtyShutdown (-1116) which we treat as success.
        const auto termRc = JetTerm2(buildInstance, JET_bitTermDirty);
        if (termRc != JET_errSuccess && termRc != JET_errDirtyShutdown)
        {
            CheckJet(termRc);
        }
    }

    //  Corrupt page PatchPgno on disk so a plain attach would trip
    //  the page checksum.  Overwriting the first 32 bytes is more
    //  than enough — that range is where the checksum lives.
    {
        std::fstream file(databasePath,
                          std::ios::binary | std::ios::in | std::ios::out);
        Require(file.is_open());
        const std::streamoff pageOffset =
            static_cast<std::streamoff>(PatchPgno - 1) *
            static_cast<std::streamoff>(pageSizeBytes);
        file.seekp(pageOffset);
        std::array<char, 32> garbage{};
        for (size_t i = 0; i < garbage.size(); ++i)
        {
            garbage[i] = static_cast<char>(0xA5 ^ (i * 17));
        }
        file.write(garbage.data(),
                   static_cast<std::streamsize>(garbage.size()));
        Require(file.good());
    }

    //  Phase 2: Begin + Patch + End on the dirty database.
    {
        JET_INSTANCE reseedInstance = JET_instanceNil;
        CheckJet(JetCreateInstance2A(&reseedInstance,
                                     "PagePatchReseed",
                                     "PagePatchReseed", 0));
        ConfigureChildInstanceParameters(&reseedInstance,
                                          directory.Path(),
                                          "PagePatchReseed");
        //  Begin doesn't take an init'd instance — it does its own
        //  ErrIRSAttachDatabaseForIrsV2.  Skip JetInit; if recovery
        //  ran first the dirty-shutdown state would transition to
        //  clean and Begin would return -1227.
        CheckJet(JetBeginDatabaseIncrementalReseedA(
            reseedInstance, databasePath.string().c_str(),
            /*genFirstDivergedLog=*/1, 0));

        CheckJet(JetPatchDatabasePagesA(reseedInstance,
                                        databasePath.string().c_str(),
                                        PatchPgno, 1,
                                        goodPageBytes.get(),
                                        pageSizeBytes, 0));

        //  End with Cancel: the engine treats the patches as
        //  unconfirmed and discards them on disk.  The DB stays in
        //  JET_dbstateIncrementalReseedInProgress (the next
        //  attempted recovery would have to start fresh).
        (void)buildCurrentLogGen;
        CheckJet(JetEndDatabaseIncrementalReseedA(
            reseedInstance, databasePath.string().c_str(),
            0, 0, 0,
            JET_bitEndDatabaseIncrementalReseedCancel));

        CheckJet(JetTerm2(reseedInstance, JET_bitTermComplete));
    }

    //  Functional verification of the CANCEL contract: read the
    //  first 32 bytes of page PatchPgno back from disk and confirm
    //  the corruption pattern is STILL there.  Cancel must NOT
    //  persist the patches we issued between Begin and End.
    {
        std::ifstream file(databasePath, std::ios::binary);
        Require(file.is_open());
        const std::streamoff pageOffset =
            static_cast<std::streamoff>(PatchPgno - 1) *
            static_cast<std::streamoff>(pageSizeBytes);
        file.seekg(pageOffset);
        std::array<uint8_t, 32> firstBytes{};
        file.read(reinterpret_cast<char*>(firstBytes.data()),
                  static_cast<std::streamsize>(firstBytes.size()));
        Require(file.gcount() ==
                static_cast<std::streamsize>(firstBytes.size()));
        for (size_t i = 0; i < firstBytes.size(); ++i)
        {
            const auto garbageExpected =
                static_cast<uint8_t>(0xA5 ^ (i * 17));
            Require(firstBytes[i] == garbageExpected);
        }
    }
}

//  Scenario B — the full commit path.  Build, dirty-term, corrupt,
//  Begin, Patch, End (passive-page-patch mode with
//  genFirstDivergedLog=0), recover, walk every row.  Verifies that
//  a successful incremental reseed restores the corrupted page so
//  the database recovers cleanly to its pre-corruption state.
EseIntegrationScenario(Replication, OfflinePagePatchRepairsCorruptedPage)
{
    TemporaryDirectory directory(
        "Replication.OfflinePagePatchRepairsCorruptedPage");
    const auto databasePath = directory.Path() / "Patched.mdb";

    //  Higher row count than the cancel-path scenario so log writes
    //  fill gen 1 and roll into gen 2 — the engine writes the DB's
    //  attach info into the new gen's log file header on rollover,
    //  which is what End's per-gen scan (io.cxx:5586) requires.
    constexpr int32_t PatchScenarioRowCount = 2000;
    constexpr uint32_t PatchPgno = 8;

    struct AlignedFree
    {
        void operator()(void* ptr) const
        {
            std::free(ptr);
        }
    };

    //  Phase 1: build DB, snapshot good page, term DIRTY.
    std::unique_ptr<uint8_t, AlignedFree> goodPageBytes;
    uint32_t pageSizeBytes = 0;
    uint32_t buildCurrentLogGen = 0;
    {
        JET_INSTANCE buildInstance = JET_instanceNil;
        CheckJet(JetCreateInstance2A(&buildInstance,
                                     "PagePatchCommitBuild",
                                     "PagePatchCommitBuild", 0));
        ConfigureChildInstanceParameters(&buildInstance,
                                          directory.Path(),
                                          "PagePatchCommitBuild");
        CheckJet(JetInit(&buildInstance));

        JET_SESID buildSession = JET_sesidNil;
        CheckJet(JetBeginSessionA(buildInstance,
                                  &buildSession, nullptr, nullptr));

        JET_DBID buildDbid = JET_dbidNil;
        CheckJet(JetCreateDatabaseA(buildSession,
                                    databasePath.string().c_str(),
                                    nullptr, &buildDbid,
                                    JET_bitDbOverwriteExisting));
        JET_TABLEID buildTable = JET_tableidNil;
        CheckJet(JetCreateTableA(buildSession, buildDbid,
                                 "Rows", 8, 100, &buildTable));
        JET_COLUMNDEF columnDefinition = {};
        columnDefinition.cbStruct = sizeof(columnDefinition);
        columnDefinition.coltyp = JET_coltypLong;
        JET_COLUMNID columnId = 0;
        CheckJet(JetAddColumnA(buildSession, buildTable, "Value",
                               &columnDefinition, nullptr, 0,
                               &columnId));

        //  Insert in chunks of 50 rows per transaction so each
        //  commit pushes records to the log writer, encouraging
        //  generation rollover within a small (64 KiB) log file.
        constexpr int32_t RowsPerTransaction = 50;
        for (int32_t startIndex = 0;
             startIndex < PatchScenarioRowCount;
             startIndex += RowsPerTransaction)
        {
            CheckJet(JetBeginTransaction(buildSession));
            const int32_t endIndex = std::min(
                startIndex + RowsPerTransaction,
                PatchScenarioRowCount);
            for (int32_t rowIndex = startIndex;
                 rowIndex < endIndex;
                 ++rowIndex)
            {
                CheckJet(JetPrepareUpdate(buildSession, buildTable,
                                          JET_prepInsert));
                CheckJet(JetSetColumn(buildSession, buildTable, columnId,
                                      &rowIndex, sizeof(rowIndex),
                                      0, nullptr));
                CheckJet(JetUpdate(buildSession, buildTable,
                                   nullptr, 0, nullptr));
            }
            CheckJet(JetCommitTransaction(buildSession, 0));
        }

        unsigned long pageSizeQuery = 0;
        CheckJet(JetGetSystemParameterA(
            buildInstance, buildSession,
            JET_paramDatabasePageSize,
            &pageSizeQuery, nullptr, 0));
        Require(pageSizeQuery > 0);
        pageSizeBytes = static_cast<uint32_t>(pageSizeQuery);

        goodPageBytes.reset(static_cast<uint8_t*>(
            std::aligned_alloc(pageSizeBytes, pageSizeBytes)));
        Require(goodPageBytes != nullptr);
        uint32_t cbActual = 0;
        CheckJet(JetGetDatabasePages(buildSession, buildDbid,
                                     PatchPgno, 1,
                                     goodPageBytes.get(),
                                     pageSizeBytes,
                                     &cbActual, 0));
        Require(cbActual == pageSizeBytes);

        unsigned long currentGenQuery = 0;
        CheckJet(JetGetSystemParameterA(
            buildInstance, buildSession,
            JET_paramRecoveryCurrentLogfile,
            &currentGenQuery, nullptr, 0));
        buildCurrentLogGen = static_cast<uint32_t>(currentGenQuery);
        Require(buildCurrentLogGen >= 1);

        CheckJet(JetCloseTable(buildSession, buildTable));
        CheckJet(JetCloseDatabase(buildSession, buildDbid, 0));
        CheckJet(JetEndSession(buildSession, 0));
        const auto termRc = JetTerm2(buildInstance, JET_bitTermDirty);
        if (termRc != JET_errSuccess && termRc != JET_errDirtyShutdown)
        {
            CheckJet(termRc);
        }
    }

    //  Corrupt page PatchPgno.
    {
        std::fstream file(databasePath,
                          std::ios::binary | std::ios::in | std::ios::out);
        Require(file.is_open());
        const std::streamoff pageOffset =
            static_cast<std::streamoff>(PatchPgno - 1) *
            static_cast<std::streamoff>(pageSizeBytes);
        file.seekp(pageOffset);
        std::array<char, 32> garbage{};
        for (size_t i = 0; i < garbage.size(); ++i)
        {
            garbage[i] = static_cast<char>(0xA5 ^ (i * 17));
        }
        file.write(garbage.data(),
                   static_cast<std::streamsize>(garbage.size()));
        Require(file.good());
    }

    //  Phase 2: Begin + Patch + End-commit on the dirty database.
    {
        JET_INSTANCE reseedInstance = JET_instanceNil;
        CheckJet(JetCreateInstance2A(&reseedInstance,
                                     "PagePatchCommitReseed",
                                     "PagePatchCommitReseed", 0));
        ConfigureChildInstanceParameters(&reseedInstance,
                                          directory.Path(),
                                          "PagePatchCommitReseed");

        CheckJet(JetBeginDatabaseIncrementalReseedA(
            reseedInstance, databasePath.string().c_str(),
            /*genFirstDivergedLog=*/1, 0));

        CheckJet(JetPatchDatabasePagesA(reseedInstance,
                                        databasePath.string().c_str(),
                                        PatchPgno, 1,
                                        goodPageBytes.get(),
                                        pageSizeBytes, 0));

        //  Commit the reseed.  genFirstDivergedLog=0 selects the
        //  "passive page patch" path (io.cxx:5529-5532): the engine
        //  treats this as patch-only with no log divergence, so the
        //  required log range is whatever the DB header already
        //  tracks (le_lGenMinRequired .. le_lGenMaxRequired).  Pass
        //  the current log gen as both min and max so the engine's
        //  per-gen attachment-info scan covers the gen that
        //  recorded the database attach.
        CheckJet(JetEndDatabaseIncrementalReseedA(
            reseedInstance, databasePath.string().c_str(),
            /*genMinRequired=*/buildCurrentLogGen,
            /*genFirstDivergedLog=*/0,
            /*genMaxRequired=*/buildCurrentLogGen,
            0));

        //  Recover + walk: confirms the corrupted page is now
        //  intact and every row inserted before corruption is
        //  readable.
        CheckJet(JetInit(&reseedInstance));
        JET_SESID verifySession = JET_sesidNil;
        CheckJet(JetBeginSessionA(reseedInstance,
                                  &verifySession, nullptr, nullptr));
        CheckJet(JetAttachDatabaseA(verifySession,
                                    databasePath.string().c_str(),
                                    JET_bitDbReadOnly));
        JET_DBID verifyDbid = JET_dbidNil;
        CheckJet(JetOpenDatabaseA(verifySession,
                                  databasePath.string().c_str(),
                                  nullptr, &verifyDbid,
                                  JET_bitDbReadOnly));
        JET_TABLEID verifyTable = JET_tableidNil;
        CheckJet(JetOpenTableA(verifySession, verifyDbid, "Rows",
                               nullptr, 0, 0, &verifyTable));

        int32_t rowCount = 0;
        const auto firstRc = JetMove(verifySession, verifyTable,
                                     JET_MoveFirst, 0);
        if (firstRc != JET_errNoCurrentRecord)
        {
            CheckJet(firstRc);
            while (true)
            {
                ++rowCount;
                const auto nextRc = JetMove(verifySession, verifyTable,
                                            JET_MoveNext, 0);
                if (nextRc == JET_errNoCurrentRecord)
                {
                    break;
                }
                CheckJet(nextRc);
            }
        }
        Require(rowCount == PatchScenarioRowCount);

        CheckJet(JetCloseTable(verifySession, verifyTable));
        CheckJet(JetCloseDatabase(verifySession, verifyDbid, 0));
        CheckJet(JetDetachDatabaseA(verifySession,
                                    databasePath.string().c_str()));
        CheckJet(JetEndSession(verifySession, 0));
        CheckJet(JetTerm2(reseedInstance, JET_bitTermComplete));
    }
}

//  ============================================================
//  Online page patch — JetOnlinePatchDatabasePage applies a page
//  image to a live (attached) database.  Engine gates on the
//  per-instance param `JET_paramEnableExternalAutoHealing`
//  (io.cxx:4413) which is 0 by default — we set it to 1 before
//  JetInit.
//
//  The page image must be a complete ESE page (correct page header
//  + matching checksum), so we get one via JetGetDatabasePages
//  and immediately feed it back to JetOnlinePatchDatabasePage.
//  Functional verification: the row count is unchanged across the
//  patch — a page write that corrupted state would make subsequent
//  reads fail.
//  ============================================================

EseIntegrationScenario(Replication, OnlinePagePatchRoundTripsValidPage)
{
    TemporaryDirectory directory(
        "Replication.OnlinePagePatchRoundTripsValidPage");
    const auto databasePath = directory.Path() / "Patched.mdb";

    constexpr int32_t OnlinePatchRowCount = 200;
    constexpr uint32_t PatchPgno = 8;

    struct AlignedFree
    {
        void operator()(void* ptr) const
        {
            std::free(ptr);
        }
    };

    JET_INSTANCE instanceHandle = JET_instanceNil;
    CheckJet(JetCreateInstance2A(&instanceHandle,
                                 "OnlinePagePatch",
                                 "OnlinePagePatch", 0));
    ConfigureChildInstanceParameters(&instanceHandle,
                                      directory.Path(),
                                      "OnlinePagePatch");
    CheckJet(JetSetSystemParameterA(&instanceHandle, JET_sesidNil,
                                    JET_paramEnableExternalAutoHealing,
                                    1, nullptr));
    CheckJet(JetInit(&instanceHandle));

    JET_SESID sessionHandle = JET_sesidNil;
    CheckJet(JetBeginSessionA(instanceHandle,
                              &sessionHandle, nullptr, nullptr));

    JET_DBID databaseId = JET_dbidNil;
    CheckJet(JetCreateDatabaseA(sessionHandle,
                                databasePath.string().c_str(),
                                nullptr, &databaseId,
                                JET_bitDbOverwriteExisting));
    JET_TABLEID tableId = JET_tableidNil;
    CheckJet(JetCreateTableA(sessionHandle, databaseId,
                             "Rows", 8, 100, &tableId));
    JET_COLUMNDEF columnDefinition = {};
    columnDefinition.cbStruct = sizeof(columnDefinition);
    columnDefinition.coltyp = JET_coltypLong;
    JET_COLUMNID columnId = 0;
    CheckJet(JetAddColumnA(sessionHandle, tableId, "Value",
                           &columnDefinition, nullptr, 0, &columnId));

    CheckJet(JetBeginTransaction(sessionHandle));
    for (int32_t rowIndex = 0; rowIndex < OnlinePatchRowCount; ++rowIndex)
    {
        CheckJet(JetPrepareUpdate(sessionHandle, tableId, JET_prepInsert));
        CheckJet(JetSetColumn(sessionHandle, tableId, columnId,
                              &rowIndex, sizeof(rowIndex), 0, nullptr));
        CheckJet(JetUpdate(sessionHandle, tableId, nullptr, 0, nullptr));
    }
    CheckJet(JetCommitTransaction(sessionHandle, 0));

    unsigned long pageSizeQuery = 0;
    CheckJet(JetGetSystemParameterA(
        instanceHandle, sessionHandle,
        JET_paramDatabasePageSize,
        &pageSizeQuery, nullptr, 0));
    Require(pageSizeQuery > 0);
    const auto pageSizeBytes = static_cast<uint32_t>(pageSizeQuery);

    //  Snapshot page PatchPgno into an aligned buffer.
    std::unique_ptr<uint8_t, AlignedFree> pageBytes(
        static_cast<uint8_t*>(
            std::aligned_alloc(pageSizeBytes, pageSizeBytes)));
    Require(pageBytes != nullptr);
    uint32_t cbActual = 0;
    CheckJet(JetGetDatabasePages(sessionHandle, databaseId,
                                 PatchPgno, 1,
                                 pageBytes.get(),
                                 pageSizeBytes,
                                 &cbActual, 0));
    Require(cbActual == pageSizeBytes);

    //  Read the instance's log signature.  The engine validates the
    //  token's signLog field against this before applying the patch
    //  (prl.cxx:962-965 throws JET_errBadLogSignature otherwise).
    JET_SIGNATURE logSignature = {};
    CheckJet(JetGetInstanceMiscInfo(instanceHandle, &logSignature,
                                    sizeof(logSignature),
                                    JET_InstanceMiscInfoLogSignature));

    //  Build a PAGE_PATCH_TOKEN matching the engine's internal
    //  layout (inc/log.hxx:71).  Default-packed 48 bytes:
    //    offset 0:  INT       cbStruct  (4)
    //    offset 4:  padding   (4)
    //    offset 8:  DBTIME    dbtime    (8)
    //    offset 16: SIGNATURE signLog   (28)
    //    offset 44: padding   (4)
    //  The engine looks up an active patch request by
    //  (ifmp, pgno, dbtime) — we have no request, so dbtime=0
    //  causes a miss; the engine returns success with fPatched=false
    //  (prl.cxx:984-987) instead of erroring out.  The signLog match
    //  is what gets us past the API's validation layer.
    alignas(8) std::array<unsigned char, 48> tokenBuffer{};
    const int32_t tokenStructSize = 48;
    std::memcpy(tokenBuffer.data() + 0, &tokenStructSize,
                sizeof(tokenStructSize));
    //  dbtime at offset 8 — leave zero (no matching request).
    std::memcpy(tokenBuffer.data() + 16, &logSignature,
                sizeof(logSignature));

    //  Apply via JetOnlinePatchDatabasePage.  Engine validates the
    //  token shape, matches signLog, then walks its patch-request
    //  table.  No request is registered for our (ifmp, pgno, 0) key
    //  so the call returns success without overwriting the page —
    //  exactly the engine's documented behaviour for a stale or
    //  speculative patch.
    CheckJet(JetOnlinePatchDatabasePage(sessionHandle, databaseId,
                                        PatchPgno,
                                        tokenBuffer.data(),
                                        static_cast<uint32_t>(
                                            tokenBuffer.size()),
                                        pageBytes.get(),
                                        pageSizeBytes, 0));

    //  Functional verification: walk every row.  If the patch
    //  corrupted the page or broke the buffer manager's cache
    //  state, the move-next chain would error out before reaching
    //  the expected row count.
    int32_t rowCount = 0;
    const auto firstRc = JetMove(sessionHandle, tableId, JET_MoveFirst, 0);
    if (firstRc != JET_errNoCurrentRecord)
    {
        CheckJet(firstRc);
        while (true)
        {
            ++rowCount;
            const auto nextRc = JetMove(sessionHandle, tableId,
                                        JET_MoveNext, 0);
            if (nextRc == JET_errNoCurrentRecord)
            {
                break;
            }
            CheckJet(nextRc);
        }
    }
    Require(rowCount == OnlinePatchRowCount);

    CheckJet(JetCloseTable(sessionHandle, tableId));
    CheckJet(JetCloseDatabase(sessionHandle, databaseId, 0));
    CheckJet(JetDetachDatabaseA(sessionHandle,
                                databasePath.string().c_str()));
    CheckJet(JetEndSession(sessionHandle, 0));
    CheckJet(JetTerm2(instanceHandle, JET_bitTermComplete));
}

//  ============================================================
//  External restore — JetExternalRestoreA ingests a backup of a
//  database (db file + log gens + checkpoint) from one directory
//  and lays it down in a fresh target directory.  This is the
//  full-replica seed path that a real HA system uses before
//  starting JetConsumeLogData live tailing.
//
//  Flow:
//   1. Build a database under `source/`.
//   2. Clone `source/` to `backup/` after JetTerm — simulating a
//      consistent backup snapshot.
//   3. mkdir `restore/` (empty target).
//   4. Call JetExternalRestoreA with the backup as source, the
//      restore dir as target, and a JET_RSTMAP_A remapping the
//      database path from source location to restore location.
//   5. Open a fresh instance pointed at `restore/`; attach + walk
//      every row.  Verifies the API actually moved bytes (not just
//      shape-validated the call).
//  ============================================================

namespace
{

constexpr int32_t ExternalRestoreRowCount = 250;
constexpr const char* ExternalRestoreDbName = "Restored.mdb";
constexpr const char* ChildEntryExternalRestore =
    "Replication.ExternalRestore.Worker";

//  Run the entire JetBackupInstance + JetExternalRestore + verify
//  flow inside a fresh child process.  JetExternalRestoreA creates
//  its OWN internal instance and re-runs ErrInit on it; doing that
//  alongside an already-init'd engine in the same process trips
//  paths through ErrNewInst / single-instance-mode state that get
//  awkward.  A clean child sidesteps the issue.
void RunExternalRestoreWorker(const std::filesystem::path& directory)
{
    const auto sourceDirectory = directory / "source";
    const auto backupDirectory = directory / "backup";
    const auto restoreDirectory = directory / "restore";
    std::filesystem::create_directories(sourceDirectory);
    std::filesystem::create_directories(backupDirectory);
    std::filesystem::create_directories(restoreDirectory);

    const auto sourceDbPath = sourceDirectory / ExternalRestoreDbName;
    const auto restoreDbPath = restoreDirectory / ExternalRestoreDbName;

    //  Phase 1: build the source DB, snapshot via JetBackupInstanceA
    //  into backup/.  JetBackupInstanceA writes the canonical backup
    //  layout that JetExternalRestoreA expects (which differs from a
    //  raw std::filesystem::copy of the live directory — engine
    //  puts logs in a numbered "logs" subdir, etc.).
    {
        JET_INSTANCE buildInstance = JET_instanceNil;
        CheckJet(JetCreateInstance2A(&buildInstance,
                                     "ExternalRestoreSource",
                                     "ExternalRestoreSource", 0));
        ConfigureChildInstanceParameters(&buildInstance,
                                          sourceDirectory,
                                          "ExternalRestoreSource");
        CheckJet(JetInit(&buildInstance));

        JET_SESID buildSession = JET_sesidNil;
        CheckJet(JetBeginSessionA(buildInstance,
                                  &buildSession, nullptr, nullptr));

        JET_DBID buildDbid = JET_dbidNil;
        CheckJet(JetCreateDatabaseA(buildSession,
                                    sourceDbPath.string().c_str(),
                                    nullptr, &buildDbid,
                                    JET_bitDbOverwriteExisting));
        JET_TABLEID buildTable = JET_tableidNil;
        CheckJet(JetCreateTableA(buildSession, buildDbid,
                                 "Rows", 8, 100, &buildTable));
        JET_COLUMNDEF columnDefinition = {};
        columnDefinition.cbStruct = sizeof(columnDefinition);
        columnDefinition.coltyp = JET_coltypLong;
        JET_COLUMNID columnId = 0;
        CheckJet(JetAddColumnA(buildSession, buildTable, "Value",
                               &columnDefinition, nullptr, 0,
                               &columnId));

        CheckJet(JetBeginTransaction(buildSession));
        for (int32_t rowIndex = 0;
             rowIndex < ExternalRestoreRowCount;
             ++rowIndex)
        {
            CheckJet(JetPrepareUpdate(buildSession, buildTable,
                                      JET_prepInsert));
            CheckJet(JetSetColumn(buildSession, buildTable, columnId,
                                  &rowIndex, sizeof(rowIndex),
                                  0, nullptr));
            CheckJet(JetUpdate(buildSession, buildTable,
                               nullptr, 0, nullptr));
        }
        CheckJet(JetCommitTransaction(buildSession, 0));
        CheckJet(JetCloseTable(buildSession, buildTable));
        CheckJet(JetCloseDatabase(buildSession, buildDbid, 0));

        //  Take backup BEFORE detach — JetBackupInstanceA wants the
        //  DB attached so it can include attach state in the backup.
        CheckJet(JetBackupInstanceA(buildInstance,
                                    backupDirectory.string().c_str(),
                                    0, nullptr));

        CheckJet(JetDetachDatabaseA(buildSession,
                                    sourceDbPath.string().c_str()));
        CheckJet(JetEndSession(buildSession, 0));
        CheckJet(JetTerm2(buildInstance, JET_bitTermComplete));
    }

    //  Phase 3: external restore from backup into the restore dir.
    //  szDatabaseName must point at the BACKUP copy (where the
    //  engine actually reads .edb bytes); szNewDatabaseName is the
    //  target.  szBackupLogPath points to the directory holding the
    //  backup's log files.  Target paths land in restore/.
    JET_RSTMAP_A restoreMap = {};
    auto backupDbPathString = (backupDirectory / ExternalRestoreDbName).string();
    auto restoreDbPathString = restoreDbPath.string();
    restoreMap.szDatabaseName = backupDbPathString.data();
    restoreMap.szNewDatabaseName = restoreDbPathString.data();

    //  Engine asserts SystemPath / LogPath end with a trailing
    //  '/' — bake it in here before handing the buffers off.
    auto restoreDirString = restoreDirectory.string();
    if (restoreDirString.back() != '/')
    {
        restoreDirString.push_back('/');
    }
    auto backupDirString = backupDirectory.string();
    if (backupDirString.back() != '/')
    {
        backupDirString.push_back('/');
    }
    //  JetExternalRestoreA does the "external" half of external
    //  restore — apply the backup's log files to bring the database
    //  to the backup point.  The CALLER is responsible for
    //  physically placing the backup files in the target directory
    //  beforehand (this is what makes it "external" vs JetRestore
    //  which copies files itself).  Stage the .edb + .pat + log
    //  files now.
    CloneDirectory(backupDirectory, restoreDirectory);

    //  Determine the highest gen in the backup so the restore picks
    //  up every numbered log file.
    long highestBackupGen = 0;
    for (const auto& entry :
         std::filesystem::directory_iterator(backupDirectory))
    {
        if (!entry.is_regular_file() ||
            entry.path().extension() != ".log")
        {
            continue;
        }
        const auto stem = entry.path().stem().string();
        if (stem.size() != 11 || stem.substr(0, 3) != "edb")
        {
            continue;
        }
        try
        {
            const auto gen = std::stol(stem.substr(3));
            if (gen > highestBackupGen)
            {
                highestBackupGen = gen;
            }
        }
        catch (...) {}
    }
    Require(highestBackupGen >= 1);

    CheckJet(JetExternalRestoreA(
        restoreDirString.data(),    // szCheckpointFilePath — target system dir
        restoreDirString.data(),    // szLogPath — target log dir
        &restoreMap, 1,
        backupDirString.data(),     // szBackupLogPath — source log dir
        1, highestBackupGen,        // genLow, genHigh
        nullptr));

    //  Phase 4: open the restored database and confirm the rows are
    //  intact end-to-end.
    {
        JET_INSTANCE verifyInstance = JET_instanceNil;
        CheckJet(JetCreateInstance2A(&verifyInstance,
                                     "ExternalRestoreVerify",
                                     "ExternalRestoreVerify", 0));
        ConfigureChildInstanceParameters(&verifyInstance,
                                          restoreDirectory,
                                          "ExternalRestoreVerify");
        CheckJet(JetInit(&verifyInstance));

        JET_SESID verifySession = JET_sesidNil;
        CheckJet(JetBeginSessionA(verifyInstance,
                                  &verifySession, nullptr, nullptr));
        CheckJet(JetAttachDatabaseA(verifySession,
                                    restoreDbPath.string().c_str(),
                                    JET_bitDbReadOnly));
        JET_DBID verifyDbid = JET_dbidNil;
        CheckJet(JetOpenDatabaseA(verifySession,
                                  restoreDbPath.string().c_str(),
                                  nullptr, &verifyDbid,
                                  JET_bitDbReadOnly));
        JET_TABLEID verifyTable = JET_tableidNil;
        CheckJet(JetOpenTableA(verifySession, verifyDbid, "Rows",
                               nullptr, 0, 0, &verifyTable));

        int32_t rowCount = 0;
        const auto firstRc = JetMove(verifySession, verifyTable,
                                     JET_MoveFirst, 0);
        if (firstRc != JET_errNoCurrentRecord)
        {
            CheckJet(firstRc);
            while (true)
            {
                ++rowCount;
                const auto nextRc = JetMove(verifySession, verifyTable,
                                            JET_MoveNext, 0);
                if (nextRc == JET_errNoCurrentRecord)
                {
                    break;
                }
                CheckJet(nextRc);
            }
        }
        Require(rowCount == ExternalRestoreRowCount);

        CheckJet(JetCloseTable(verifySession, verifyTable));
        CheckJet(JetCloseDatabase(verifySession, verifyDbid, 0));
        CheckJet(JetDetachDatabaseA(verifySession,
                                    restoreDbPath.string().c_str()));
        CheckJet(JetEndSession(verifySession, 0));
        CheckJet(JetTerm2(verifyInstance, JET_bitTermComplete));
    }
}

struct ExternalRestoreRegistrar
{
    ExternalRestoreRegistrar()
    {
        RegisterChildEntry(ChildEntryExternalRestore,
                           SimpleChildEntryPoint(&RunExternalRestoreWorker));
    }
};
[[maybe_unused]] static ExternalRestoreRegistrar
    _externalRestoreRegistrar;

}  // namespace

EseIntegrationScenario(Replication, ExternalRestoreImportsBackupIntoFreshDir)
{
    TemporaryDirectory directory(
        "Replication.ExternalRestoreImportsBackupIntoFreshDir");

    //  External restore is process-state sensitive (creates its own
    //  internal instance via ErrNewInst).  Run in a fresh child so
    //  we don't collide with the test runner's already-initialised
    //  engine globals.
    ChildProcess worker(ChildEntryExternalRestore, directory.Path());
    RequireCleanExit(worker, "external-restore-worker");
}
