// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "WireProtocol.hxx"

#include <algorithm>
#include <bit>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <format>
#include <stdexcept>
#include <thread>
#include <utility>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace ese::tests
{

namespace
{

//  htole32 / le32toh for the on-wire integer fields.  Linux defines
//  these in <endian.h>; spell them out here so the header doesn't
//  drag extra system includes on people just including
//  ReplicationWire.hxx.
constexpr uint32_t EncodeLittleEndianU32(uint32_t value)
{
    if constexpr (std::endian::native == std::endian::little)
    {
        return value;
    }
    else
    {
        return (value >> 24) |
               ((value & 0x00FF0000u) >> 8) |
               ((value & 0x0000FF00u) << 8) |
               (value << 24);
    }
}

constexpr uint32_t DecodeLittleEndianU32(uint32_t value)
{
    return EncodeLittleEndianU32(value);  // symmetric
}

//  Wait for the given fd to become readable or writable.  Returns
//  true on success, false on timeout.  Throws on poll() error.
bool WaitForFileDescriptor(int fileDescriptor,
                           short eventMask,
                           std::chrono::milliseconds timeout)
{
    pollfd pollEntry = {};
    pollEntry.fd = fileDescriptor;
    pollEntry.events = eventMask;

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true)
    {
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());
        const int pollTimeout = remaining.count() <= 0
                                    ? 0
                                    : static_cast<int>(remaining.count());

        const int pollResult = ::poll(&pollEntry, 1, pollTimeout);
        if (pollResult > 0)
        {
            return true;
        }
        if (pollResult == 0)
        {
            return false;
        }
        if (errno == EINTR)
        {
            continue;
        }
        throw std::runtime_error(std::format("poll() failed: {}",
                                             std::strerror(errno)));
    }
}

//  Blocking write of exactly `bytes.size()` bytes.  Retries on EINTR
//  and short writes.  Throws on peer-closed or other I/O error.
void WriteAll(int fileDescriptor, std::span<const std::byte> bytes)
{
    size_t offset = 0;
    while (offset < bytes.size())
    {
        const auto written = ::send(fileDescriptor,
                                    bytes.data() + offset,
                                    bytes.size() - offset,
                                    MSG_NOSIGNAL);
        if (written < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            throw std::runtime_error(std::format("send() failed: {}",
                                                 std::strerror(errno)));
        }
        if (written == 0)
        {
            throw std::runtime_error("send() returned 0 — peer closed");
        }
        offset += static_cast<size_t>(written);
    }
}

//  Blocking read of exactly `bytes.size()` bytes.  Returns false if
//  the peer closes mid-frame BEFORE any byte has been read on this
//  call (clean shutdown); throws if the close happens mid-frame.
bool ReadAll(int fileDescriptor,
             std::span<std::byte> bytes,
             std::chrono::milliseconds timeout)
{
    size_t offset = 0;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (offset < bytes.size())
    {
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0)
        {
            throw std::runtime_error("recv() timed out");
        }
        if (!WaitForFileDescriptor(fileDescriptor, POLLIN, remaining))
        {
            throw std::runtime_error("recv() timed out");
        }
        const auto received = ::recv(fileDescriptor,
                                     bytes.data() + offset,
                                     bytes.size() - offset,
                                     0);
        if (received < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            throw std::runtime_error(std::format("recv() failed: {}",
                                                 std::strerror(errno)));
        }
        if (received == 0)
        {
            if (offset == 0)
            {
                return false;  // clean close at frame boundary
            }
            throw std::runtime_error("recv() returned 0 mid-frame");
        }
        offset += static_cast<size_t>(received);
    }
    return true;
}

} // namespace

ReplicationChannel::ReplicationChannel(int fileDescriptor)
    : _fileDescriptor(fileDescriptor)
{
}

ReplicationChannel::~ReplicationChannel()
{
    Close();
}

ReplicationChannel::ReplicationChannel(ReplicationChannel&& other) noexcept
    : _fileDescriptor(other._fileDescriptor)
{
    other._fileDescriptor = -1;
}

ReplicationChannel&
ReplicationChannel::operator=(ReplicationChannel&& other) noexcept
{
    if (this != &other)
    {
        Close();
        _fileDescriptor = other._fileDescriptor;
        other._fileDescriptor = -1;
    }
    return *this;
}

void ReplicationChannel::SendFrame(ReplicationFrameKind kind,
                                   std::span<const std::byte> payload)
{
    if (_fileDescriptor < 0)
    {
        throw std::runtime_error("SendFrame on closed channel");
    }

    //  Write header + payload as a single contiguous buffer so the
    //  framing arrives atomically (within TCP's segmentation
    //  constraints).
    std::vector<std::byte> buffer;
    buffer.resize(sizeof(uint32_t) * 2 + payload.size());

    const uint32_t kindEncoded =
        EncodeLittleEndianU32(static_cast<uint32_t>(kind));
    const uint32_t lengthEncoded =
        EncodeLittleEndianU32(static_cast<uint32_t>(payload.size()));

    std::memcpy(buffer.data(), &kindEncoded, sizeof(kindEncoded));
    std::memcpy(buffer.data() + sizeof(kindEncoded),
                &lengthEncoded, sizeof(lengthEncoded));
    if (!payload.empty())
    {
        std::memcpy(buffer.data() + sizeof(uint32_t) * 2,
                    payload.data(), payload.size());
    }

    WriteAll(_fileDescriptor, buffer);
}

ReplicationChannel::ReceivedFrame
ReplicationChannel::RecvFrame(std::chrono::milliseconds timeout)
{
    if (_fileDescriptor < 0)
    {
        throw std::runtime_error("RecvFrame on closed channel");
    }

    std::byte headerBytes[sizeof(uint32_t) * 2] = {};
    if (!ReadAll(_fileDescriptor, std::span(headerBytes), timeout))
    {
        throw std::runtime_error("RecvFrame: peer closed before frame");
    }

    uint32_t kindEncoded = 0;
    uint32_t lengthEncoded = 0;
    std::memcpy(&kindEncoded, headerBytes, sizeof(kindEncoded));
    std::memcpy(&lengthEncoded,
                headerBytes + sizeof(kindEncoded),
                sizeof(lengthEncoded));

    ReceivedFrame received;
    received.Kind = static_cast<ReplicationFrameKind>(
        DecodeLittleEndianU32(kindEncoded));
    const auto payloadSize = DecodeLittleEndianU32(lengthEncoded);

    if (payloadSize > 0)
    {
        received.Payload.resize(payloadSize);
        const bool readOk = ReadAll(_fileDescriptor,
                                    std::span(received.Payload),
                                    timeout);
        if (!readOk)
        {
            throw std::runtime_error("RecvFrame: peer closed mid-payload");
        }
    }
    return received;
}

void ReplicationChannel::Close()
{
    if (_fileDescriptor >= 0)
    {
        ::close(_fileDescriptor);
        _fileDescriptor = -1;
    }
}

ReplicationServer::ReplicationServer()
{
    _listenFileDescriptor = ::socket(AF_INET,
                                     SOCK_STREAM | SOCK_CLOEXEC,
                                     0);
    if (_listenFileDescriptor < 0)
    {
        throw std::runtime_error(std::format("socket() failed: {}",
                                             std::strerror(errno)));
    }

    //  TCP_NODELAY keeps small control frames from sitting in Nagle's
    //  send buffer.  Log shipping moves chunks of kilobytes so the
    //  bandwidth penalty is negligible.
    const int nodelayEnabled = 1;
    (void)::setsockopt(_listenFileDescriptor, IPPROTO_TCP, TCP_NODELAY,
                       &nodelayEnabled, sizeof(nodelayEnabled));

    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(0);  // ephemeral

    if (::bind(_listenFileDescriptor,
               reinterpret_cast<const sockaddr*>(&address),
               sizeof(address)) < 0)
    {
        const auto savedErrno = errno;
        ::close(_listenFileDescriptor);
        _listenFileDescriptor = -1;
        throw std::runtime_error(std::format("bind() failed: {}",
                                             std::strerror(savedErrno)));
    }

    if (::listen(_listenFileDescriptor, 4) < 0)
    {
        const auto savedErrno = errno;
        ::close(_listenFileDescriptor);
        _listenFileDescriptor = -1;
        throw std::runtime_error(std::format("listen() failed: {}",
                                             std::strerror(savedErrno)));
    }

    //  Read back the kernel-assigned port.
    sockaddr_in bound = {};
    socklen_t boundLength = sizeof(bound);
    if (::getsockname(_listenFileDescriptor,
                      reinterpret_cast<sockaddr*>(&bound),
                      &boundLength) < 0)
    {
        const auto savedErrno = errno;
        ::close(_listenFileDescriptor);
        _listenFileDescriptor = -1;
        throw std::runtime_error(std::format("getsockname() failed: {}",
                                             std::strerror(savedErrno)));
    }
    _port = ntohs(bound.sin_port);
}

ReplicationServer::~ReplicationServer()
{
    if (_listenFileDescriptor >= 0)
    {
        ::close(_listenFileDescriptor);
    }
}

ReplicationChannel
ReplicationServer::AcceptOne(std::chrono::milliseconds timeout)
{
    if (!WaitForFileDescriptor(_listenFileDescriptor, POLLIN, timeout))
    {
        throw std::runtime_error(std::format(
            "AcceptOne timed out on port {}", _port));
    }
    sockaddr_in peer = {};
    socklen_t peerLength = sizeof(peer);
    const int acceptedFileDescriptor = ::accept4(
        _listenFileDescriptor,
        reinterpret_cast<sockaddr*>(&peer),
        &peerLength,
        SOCK_CLOEXEC);
    if (acceptedFileDescriptor < 0)
    {
        throw std::runtime_error(std::format("accept4() failed: {}",
                                             std::strerror(errno)));
    }

    const int nodelayEnabled = 1;
    (void)::setsockopt(acceptedFileDescriptor, IPPROTO_TCP, TCP_NODELAY,
                       &nodelayEnabled, sizeof(nodelayEnabled));

    return ReplicationChannel(acceptedFileDescriptor);
}

ReplicationChannel
ReplicationClient::Connect(uint16_t port,
                           std::chrono::milliseconds timeout)
{
    const int clientFileDescriptor = ::socket(AF_INET,
                                              SOCK_STREAM | SOCK_CLOEXEC,
                                              0);
    if (clientFileDescriptor < 0)
    {
        throw std::runtime_error(std::format("socket() failed: {}",
                                             std::strerror(errno)));
    }

    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);

    //  Retry connect() until the listener accepts or the timeout
    //  elapses — children start in parallel, so an early connect
    //  before the peer's listen completes is normal.
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true)
    {
        const int connectResult = ::connect(
            clientFileDescriptor,
            reinterpret_cast<const sockaddr*>(&address),
            sizeof(address));
        if (connectResult == 0)
        {
            const int nodelayEnabled = 1;
            (void)::setsockopt(clientFileDescriptor,
                               IPPROTO_TCP, TCP_NODELAY,
                               &nodelayEnabled, sizeof(nodelayEnabled));
            return ReplicationChannel(clientFileDescriptor);
        }
        if (errno == EINTR)
        {
            continue;
        }
        if ((errno == ECONNREFUSED || errno == EAGAIN) &&
            std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        const auto savedErrno = errno;
        ::close(clientFileDescriptor);
        throw std::runtime_error(std::format(
            "connect(127.0.0.1:{}) failed: {}",
            port, std::strerror(savedErrno)));
    }
}

} // namespace ese::tests
