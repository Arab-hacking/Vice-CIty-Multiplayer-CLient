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
    void SetPlayerState(const std::optional<vc::Vector3>& position, std::uint8_t health);
    void SendChat(const std::string& text);

    [[nodiscard]] State GetState() const { return m_state.load(); }
    [[nodiscard]] std::uint16_t PlayerId() const { return m_playerId.load(); }
    [[nodiscard]] std::string StatusText() const;
    [[nodiscard]] std::vector<std::string> ChatLines() const;

    // Удалённые игроки, о которых сообщил сервер (для пула кукол в игровом потоке).
    struct RemotePlayer {
        std::uint16_t id = 0;
        std::uint16_t skin = 7;
        std::string nick;
        vc::Vector3 targetPosition{};
        bool hasPosition = false;
        std::uint8_t health = 100;
        std::uint32_t syncCount = 0; // диагностика: сколько SyncUpdate пришло
    };
    [[nodiscard]] std::vector<RemotePlayer> RemotePlayers() const;

    // Телепорт локального игрока от сервера (спавн, /tp, /kill, смерть).
    // Игровой поток забирает, когда мир готов и игрок жив.
    struct LocalTeleport {
        vc::Vector3 position{};
        float heading = 0.0f;
    };
    [[nodiscard]] std::optional<LocalTeleport> TakePendingTeleport();

    // Игровой поток заметил смерть (HP упало до 0) — сетевой поток шлёт Death.
    void NotifyDeath();

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
    mutable std::mutex m_logMutex; // отдельный: Log() зовут и под m_mutex (был дедлок!)
    std::string m_status = "сеть выключена";
    std::vector<std::string> m_chatLines;
    std::vector<std::string> m_chatOutbox;
    std::optional<vc::Vector3> m_latestPosition;
    std::uint8_t m_latestHealth = 100;
    std::map<std::uint16_t, RemotePlayer> m_remotePlayers;
    std::optional<LocalTeleport> m_pendingTeleport;
    std::atomic<bool> m_deathPending{false};
};

} // namespace vc::network
