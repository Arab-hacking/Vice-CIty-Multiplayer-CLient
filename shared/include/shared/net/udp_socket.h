#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Минимальная кроссплатформенная UDP-обёртка (Winsock2 / POSIX sockets).
// Достаточно для RakNet-датаграмм клиента, сервера и netprobe.
namespace vc::net {

struct Endpoint {
    std::string ip;     // IPv4 dotted-quad
    std::uint16_t port = 0;
    bool operator==(const Endpoint&) const = default;
};

struct ReceivedDatagram {
    Endpoint from;
    std::vector<std::uint8_t> data;
};

class UdpSocket {
public:
    UdpSocket();
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    [[nodiscard]] bool IsValid() const { return m_fd != kInvalid; }
    // port 0 = произвольный порт от ОС (для клиента/netprobe).
    bool Bind(std::uint16_t port);
    bool SendTo(const Endpoint& to, std::span<const std::uint8_t> data);
    // timeoutMs < 0 — блокирующее чтение; 0 — опрос без ожидания.
    std::optional<ReceivedDatagram> Receive(int timeoutMs);

private:
    static constexpr std::intptr_t kInvalid = -1;
    std::intptr_t m_fd = kInvalid;
};

} // namespace vc::net
