#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <shared/math/vector3.h>
#include <shared/net/peer.h>
#include <shared/net/udp_socket.h>

// Сетевой клиент внутри игры. Живёт в ОТДЕЛЬНОМ потоке: соединение не умирает,
// когда игра на паузе, свёрнута или показывает загрузку (CGame::Process в это
// время не крутится). Главный поток только складывает сюда свежую позицию
// игрока (SetPlayerPosition из хука кадра) и читает статус/чат для оверлея.
namespace vc::network {

enum class State { Disabled, Connecting, Connected, Failed };

class NetClient {
public:
    static NetClient& Instance();

    // iniPath: vc-client.ini рядом с DLL ([Server] IP/Port/Nick). Запускает поток сети.
    void Init(FILE* log, const std::string& iniPath);
    // Best-effort из DllMain(DETACH): послать Disconnect и остановить поток.
    void ShutdownBestEffort();

    // Вызывается из игрового потока каждый кадр.
    void SetPlayerPosition(const std::optional<vc::Vector3>& position);
    void SendChat(const std::string& text);

    [[nodiscard]] State GetState() const { return m_state.load(); }
    [[nodiscard]] std::uint16_t PlayerId() const { return m_playerId.load(); }
    [[nodiscard]] std::string StatusText() const;
    [[nodiscard]] std::vector<std::string> ChatLines() const;

    // Удалённые игроки, о которых сообщил сервер (для пула кукол в игровом потоке).
    struct RemotePlayer {
        std::uint16_t id = 0;
        std::string nick;
        vc::Vector3 targetPosition{};
        bool hasPosition = false;
    };
    [[nodiscard]] std::vector<RemotePlayer> RemotePlayers() const;

private:
    NetClient() = default;
    void ThreadMain();
    void Log(const std::string& text);
    void SetStatus(const std::string& text);
    void PushChatLine(const std::string& text);
    void SendPayloadLockedPeer(const std::vector<std::uint8_t>& payload,
                               vc::net::Reliability reliability, std::uint8_t channel,
                               std::uint64_t nowMs);
    void HandlePayload(const std::vector<std::uint8_t>& payload);

    FILE* m_log = nullptr;
    vc::net::UdpSocket m_socket;
    vc::net::ReliablePeer m_peer; // только из потока сети
    vc::net::Endpoint m_server{"127.0.0.1", 7777};
    std::string m_nick = "Player";

    std::atomic<State> m_state{State::Disabled};
    std::atomic<std::uint16_t> m_playerId{0};
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_started{false};

    mutable std::mutex m_mutex; // защищает поля ниже
    std::string m_status = "сеть выключена";
    std::vector<std::string> m_chatLines;
    std::vector<std::string> m_chatOutbox;
    std::optional<vc::Vector3> m_latestPosition;
    std::map<std::uint16_t, RemotePlayer> m_remotePlayers;
};

} // namespace vc::network
