#pragma once

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include <shared/math/vector3.h>
#include <shared/net/peer.h>
#include <shared/net/udp_socket.h>

// Сетевой клиент внутри игры (Этап 2): подключение к нашему vc-server,
// статус для оверлея, серверные сообщения (чат-лог), поток OnFootSync.
// Tick() вызывается из хука CGame::Process — т.е. в главном потоке игры,
// поэтому блокировки не нужны: сокет опрашивается без ожидания.
namespace vc::network {

enum class State { Disabled, Connecting, Connected, Failed };

class NetClient {
public:
    static NetClient& Instance();

    // iniPath: vc-client.ini рядом с DLL ([Server] IP/Port/Nick).
    void Init(FILE* log, const std::string& iniPath);
    void Tick(std::uint64_t nowMs, const std::optional<vc::Vector3>& playerPosition);

    [[nodiscard]] State GetState() const { return m_state; }
    [[nodiscard]] std::uint16_t PlayerId() const { return m_playerId; }
    [[nodiscard]] const std::string& StatusText() const { return m_status; }
    [[nodiscard]] const std::vector<std::string>& ChatLines() const { return m_chatLines; }

    void SendChat(const std::string& text);

private:
    NetClient() = default;
    void Log(const std::string& text);
    void SendPayload(const std::vector<std::uint8_t>& payload, vc::net::Reliability reliability,
                     std::uint8_t channel, std::uint64_t nowMs);
    void HandlePayload(const std::vector<std::uint8_t>& payload);

    FILE* m_log = nullptr;
    vc::net::UdpSocket m_socket;
    vc::net::ReliablePeer m_peer;
    vc::net::Endpoint m_server{"127.0.0.1", 7777};
    std::string m_nick = "Player";

    State m_state = State::Disabled;
    std::uint16_t m_playerId = 0;
    std::string m_status = "сеть выключена";
    std::vector<std::string> m_chatLines;
    std::uint64_t m_connectStartedMs = 0;
    std::uint64_t m_lastSyncMs = 0;
    bool m_requestSent = false;
};

} // namespace vc::network
