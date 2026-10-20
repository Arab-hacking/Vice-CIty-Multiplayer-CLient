#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <shared/math/quaternion.h>
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
    // Локальная служебная строка в окно чата (не уходит на сервер).
    void AddLocalChatLine(const std::string& text);

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
        std::uint16_t inVehicleId = 0; // серверная машина, где сидит игрок (0 — пешком)
        std::uint8_t seat = 0;         // 0 — водитель, 1..3 — пассажир
    };
    [[nodiscard]] std::vector<RemotePlayer> RemotePlayers() const;

    // Серверные машины в стриме (игровой поток рисует их через VehiclePool).
    struct RemoteVehicle {
        std::uint16_t id = 0;
        std::uint16_t model = 0;
        vc::Vector3 position{};
        float heading = 0.0f;
        vc::Quaternion targetQuat{}; // последняя поза от водителя (hasPose)
        bool hasPose = false;
        std::uint64_t syncCount = 0;
    };
    [[nodiscard]] std::vector<RemoteVehicle> RemoteVehicles() const;

    // Локальный игрок за рулём серверной машины: поза уходит на сервер 10 Гц.
    struct LocalVehicleState {
        std::uint16_t vehicleId = 0;
        vc::Vector3 position{};
        vc::Quaternion quat{};
    };
    void SetLocalVehicleState(const std::optional<LocalVehicleState>& state);
    // Края посадки/выхода (игровой поток заметил смену FindPlayerVehicle).
    void NotifyEnterVehicle(std::uint16_t vehicleId, std::uint8_t seat);
    void NotifyExitVehicle();
    // Есть ли свободное пассажирское место (1..3) в серверной машине по
    // данным VehicleOccupant. Посадка локального игрока пассажиром (G).
    [[nodiscard]] bool HasFreePassengerSeat(std::uint16_t vehicleId) const;

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
    std::map<std::uint16_t, RemoteVehicle> m_remoteVehicles;
    // playerId -> (vehicleId, seat) из VehicleOccupant (может прийти раньше
    // StreamIn игрока, поэтому отдельная карта).
    std::map<std::uint16_t, std::pair<std::uint16_t, std::uint8_t>> m_occupancy;
    std::optional<LocalVehicleState> m_localVehicle;
    std::atomic<int> m_vehicleEvent{-2}; // -2 нет, -1 выход, >=0: id | (seat << 16)
    std::optional<LocalTeleport> m_pendingTeleport;
    std::atomic<bool> m_deathPending{false};
};

} // namespace vc::network
