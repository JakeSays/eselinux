// Copyright (c) Jake Helfert
// Licensed under the MIT License.
//
// ReplicationWire — TCP-loopback framed message channel used by the
// round-8 replication scenarios.  Each replica is its own OS process
// and they meet only on the wire (no shared memory, no shared on-disk
// state); this header defines the framing and the blocking
// server/client/channel helpers that wrap a connected socket.
//
// Frame layout on the wire (little-endian, no padding):
//
//   struct ReplicationFrameHeader {
//       uint32_t kind;          // ReplicationFrameKind
//       uint32_t payloadBytes;  // bytes immediately following
//   };
//   followed by `payloadBytes` raw bytes of payload (layout depends on
//   `kind`).
//
// Transport: SOCK_STREAM on 127.0.0.1.  Server binds to port 0 so the
// kernel picks an ephemeral port; caller reads the assigned port back
// via Server::Port() and passes it to its peers out-of-band (CLI
// argument or per-child config file).
//
// Everything is blocking with an explicit timeout — replication
// scenarios run in well-defined phases and blocking I/O is easier to
// reason about than async.  Cancellation = close the socket; the
// peer's next Recv returns a connection-closed error.

#pragma once

#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ese::tests
{

enum class ReplicationFrameKind : uint32_t
{
    //  Active -> passive log shipping.
    LogData = 1,            // payload: JET_EMITDATACTX (40 B) + raw log bytes (cbLogData)
    StreamComplete = 2,     // payload: empty — emit stream is shutting down

    //  Active <-> passive identity + checkpoint queries.
    LogSignatureQuery = 10, // payload: empty
    LogSignatureReply = 11, // payload: JET_SIGNATURE (28 B)
    CheckpointQuery = 12,   // payload: empty
    CheckpointReply = 13,   // payload: int64 (lGenMinRequired)

    //  Active -> passive page repair.
    PageReadRequest = 20,   // payload: pgnoStart (u32) + cpg (u32)
    PageReadReply = 21,     // payload: cpg (u32) + raw page bytes
    OnlinePatchRequest = 22,// payload: pgno (u32) + cbToken (u32) + token + page bytes

    //  Incremental reseed handshake.
    DivergedLogReport = 30, // payload: divergedGen (u32)
    ReseedPatchRequest = 31,// payload: pgno (u32)
    ReseedComplete = 32,    // payload: empty

    //  Baseline file transfer — used to ship a consistent snapshot of
    //  the active's directory before the live-tail emit stream starts.
    FileTransferBegin = 40, // payload: name-length (u32) + utf8 path + file size (u64)
    FileTransferChunk = 41, // payload: raw bytes (size implied by header)
    FileTransferEnd = 42,   // payload: empty
    BaselineComplete = 43,  // payload: empty — sender finished shipping baseline files

    //  Test signalling.
    Ready = 100,            // generic readiness ping between peers
    AssertionFailure = 101, // payload: UTF-8 error message
};

//  Header that precedes every frame on the wire.  POD; serialised in
//  little-endian byte order by the channel helpers.
struct ReplicationFrameHeader
{
    uint32_t Kind = 0;
    uint32_t PayloadBytes = 0;
};

//  An accepted/connected fd plus blocking SendFrame/RecvFrame helpers.
//  Move-only — owns the fd.
class ReplicationChannel
{
public:
    ReplicationChannel() = default;
    explicit ReplicationChannel(int fileDescriptor);
    ~ReplicationChannel();

    ReplicationChannel(const ReplicationChannel&) = delete;
    ReplicationChannel& operator=(const ReplicationChannel&) = delete;

    ReplicationChannel(ReplicationChannel&& other) noexcept;
    ReplicationChannel& operator=(ReplicationChannel&& other) noexcept;

    //  Send one framed message.  Blocks until the entire frame is
    //  written or the connection breaks.  Throws on error.
    void SendFrame(ReplicationFrameKind kind,
                   std::span<const std::byte> payload);

    //  Convenience overload with no payload.
    void SendFrame(ReplicationFrameKind kind)
    {
        SendFrame(kind, std::span<const std::byte>{});
    }

    //  Result of a successful RecvFrame call.  `Payload` owns its
    //  bytes; lifetime extends to the next RecvFrame on the same
    //  channel.
    struct ReceivedFrame
    {
        ReplicationFrameKind Kind = ReplicationFrameKind::Ready;
        std::vector<std::byte> Payload;
    };

    //  Block until one frame is available or the timeout elapses.
    //  Throws on timeout or peer-closed connection.
    ReceivedFrame RecvFrame(std::chrono::milliseconds timeout);

    //  Close the socket explicitly; further Send/Recv calls throw.
    void Close();

    //  Underlying file descriptor; -1 if the channel is closed.
    int FileDescriptor() const
    {
        return _fileDescriptor;
    }

private:
    int _fileDescriptor = -1;
};

//  Listening TCP socket bound to 127.0.0.1 on an ephemeral port.  The
//  ctor binds + listens; AcceptOne blocks for the next connection.
class ReplicationServer
{
public:
    ReplicationServer();
    ~ReplicationServer();

    ReplicationServer(const ReplicationServer&) = delete;
    ReplicationServer& operator=(const ReplicationServer&) = delete;

    //  The TCP port the kernel assigned, in host byte order.  Stable
    //  for the lifetime of the server.
    uint16_t Port() const
    {
        return _port;
    }

    //  Block until exactly one peer connects.  Throws on timeout.
    ReplicationChannel AcceptOne(std::chrono::milliseconds timeout);

private:
    int _listenFileDescriptor = -1;
    uint16_t _port = 0;
};

//  Outbound TCP client.  Connects to 127.0.0.1:port.
class ReplicationClient
{
public:
    //  Block until connected or the timeout elapses.  Throws on
    //  timeout or refusal.
    static ReplicationChannel Connect(uint16_t port,
                                      std::chrono::milliseconds timeout);
};

} // namespace ese::tests
