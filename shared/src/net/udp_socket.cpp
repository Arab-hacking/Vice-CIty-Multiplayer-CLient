#include <shared/net/udp_socket.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using SocketFd = SOCKET;
static constexpr SocketFd kInvalidFd = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketFd = int;
static constexpr SocketFd kInvalidFd = -1;
#endif

#include <array>
#include <cstring>

namespace vc::net {
namespace {

#ifdef _WIN32
bool EnsureWinsock() {
    static const bool ok = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return ok;
}
#endif

sockaddr_in MakeAddress(const Endpoint& ep) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ep.port);
    if (ep.ip.empty() || ep.ip == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_pton(AF_INET, ep.ip.c_str(), &addr.sin_addr);
    }
    return addr;
}

Endpoint FromAddress(const sockaddr_in& addr) {
    std::array<char, INET_ADDRSTRLEN> buf{};
    inet_ntop(AF_INET, &addr.sin_addr, buf.data(), buf.size());
    return Endpoint{buf.data(), ntohs(addr.sin_port)};
}

} // namespace

UdpSocket::UdpSocket() {
#ifdef _WIN32
    if (!EnsureWinsock()) return;
#endif
    const SocketFd fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd != kInvalidFd) m_fd = static_cast<std::intptr_t>(fd);
}

UdpSocket::~UdpSocket() {
    if (m_fd != kInvalid) {
#ifdef _WIN32
        ::closesocket(static_cast<SocketFd>(m_fd));
#else
        ::close(static_cast<int>(m_fd));
#endif
    }
}

bool UdpSocket::Bind(std::uint16_t port) {
    if (!IsValid()) return false;
    const sockaddr_in addr = MakeAddress({"0.0.0.0", port});
    return ::bind(static_cast<SocketFd>(m_fd),
                  reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == 0;
}

bool UdpSocket::SendTo(const Endpoint& to, std::span<const std::uint8_t> data) {
    if (!IsValid()) return false;
    const sockaddr_in addr = MakeAddress(to);
    const auto sent = ::sendto(static_cast<SocketFd>(m_fd),
                               reinterpret_cast<const char*>(data.data()),
                               static_cast<int>(data.size()), 0,
                               reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    return sent == static_cast<decltype(sent)>(data.size());
}

std::optional<ReceivedDatagram> UdpSocket::Receive(int timeoutMs) {
    if (!IsValid()) return std::nullopt;
    const SocketFd fd = static_cast<SocketFd>(m_fd);
    if (timeoutMs >= 0) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(fd, &readSet);
        timeval tv{};
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
        const int ready = ::select(static_cast<int>(fd) + 1, &readSet, nullptr, nullptr, &tv);
        if (ready <= 0) return std::nullopt;
    }
    std::array<std::uint8_t, 65536> buffer{};
    sockaddr_in from{};
#ifdef _WIN32
    int fromLen = sizeof(from);
#else
    socklen_t fromLen = sizeof(from);
#endif
    const auto received = ::recvfrom(fd, reinterpret_cast<char*>(buffer.data()),
                                     static_cast<int>(buffer.size()), 0,
                                     reinterpret_cast<sockaddr*>(&from), &fromLen);
    if (received <= 0) return std::nullopt;
    ReceivedDatagram result;
    result.from = FromAddress(from);
    result.data.assign(buffer.begin(), buffer.begin() + received);
    return result;
}

} // namespace vc::net
