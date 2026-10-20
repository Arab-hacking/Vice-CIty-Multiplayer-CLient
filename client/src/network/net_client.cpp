#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <thread>

#include <shared/protocol/codec.h>
#include <shared/protocol/packets.h>
#include <shared/version.h>

#include "net_client.h"

namespace vc::network {
namespace {
std::uint64_t NowMs() { return GetTickCount64(); }
} // namespace

NetClient& NetClient::Instance() {
    static NetClient instance;
    return instance;
}

void NetClient::Log(const std::string& text) {
    OutputDebugStringA(text.c_str());
    if (m_log) {
        // НЕ m_mutex: Log() вызывают из обработчиков пакетов, уже держа m_mutex,
        // а std::mutex нерекурсивный — это был дедлок «игра не запускается с сервером».
        std::lock_guard lock(m_logMutex);
        std::fputs(text.c_str(), m_log);
        std::fflush(m_log);
    }
}

void NetClient::SetStatus(const std::string& text) {
    std::lock_guard lock(m_mutex);
    m_status = text;
}

std::string NetClient::StatusText() const {
    std::lock_guard lock(m_mutex);
    return m_status;
}

void NetClient::PushChatLine(const std::string& text) {
    std::lock_guard lock(m_mutex);
    m_chatLines.push_back(text);
    if (m_chatLines.size() > 8) m_chatLines.erase(m_chatLines.begin());
}

std::vector<std::string> NetClient::ChatLines() const {
    std::lock_guard lock(m_mutex);
    return m_chatLines;
}

void NetClient::Init(FILE* log, const std::string& iniPath) {
    if (m_started.exchange(true)) return;
    m_log = log;

    char ip[64] = "127.0.0.1";
    char nick[32] = "Player";
    const int port =
        static_cast<int>(GetPrivateProfileIntA("Server", "Port", 7777, iniPath.c_str()));
    GetPrivateProfileStringA("Server", "IP", ip, ip, sizeof(ip), iniPath.c_str());
    GetPrivateProfileStringA("Server", "Nick", nick, nick, sizeof(nick), iniPath.c_str());
    m_server = {ip, static_cast<std::uint16_t>(port)};
    m_nick = nick;

    if (!m_socket.IsValid() || !m_socket.Bind(0)) {
        m_state = State::Failed;
        SetStatus("не удалось создать UDP-сокет");
        Log("net: не удалось создать UDP-сокет\n");
        return;
    }
    m_state = State::Connecting;
    SetStatus("подключение к " + m_server.ip + ":" + std::to_string(m_server.port) + "...");
    Log("net: подключение к " + m_server.ip + ":" + std::to_string(m_server.port) +
        ", ник=" + m_nick + " (поток сети запущен)\n");
    std::thread(&NetClient::ThreadMain, this).detach();
}

void NetClient::ShutdownBestEffort() {
    if (!m_started.load()) return;
    m_stop = true;
    if (m_state.load() == State::Connected && m_socket.IsValid()) {
        // Собираем Disconnect руками: ReliablePeer принадлежит потоку сети.
        vc::net::InternalPacket packet;
        packet.messageNumber = 0;
        packet.reliability = vc::net::Reliability::Unreliable;
        packet.payload = {static_cast<std::uint8_t>(vc::protocol::ClientPacket::Disconnect)};
        vc::net::Datagram datagram;
        datagram.packets.push_back(packet);
        m_socket.SendTo(m_server, vc::net::EncodeDatagram(datagram));
    }
}

void NetClient::SetPlayerState(const std::optional<vc::Vector3>& position,
                               std::uint8_t health, std::uint8_t weapon) {
    std::lock_guard lock(m_mutex);
    m_latestPosition = position;
    m_latestHealth = health;
    m_latestWeapon = weapon;
}

void NetClient::SendChat(const std::string& text) {
    std::lock_guard lock(m_mutex);
    m_chatOutbox.push_back(text);
}

void NetClient::AddLocalChatLine(const std::string& text) { PushChatLine(text); }

void NetClient::SendPayloadLockedPeer(const std::vector<std::uint8_t>& payload,
                                      vc::net::Reliability reliability, std::uint8_t channel,
                                      std::uint64_t nowMs) {
    m_socket.SendTo(m_server, m_peer.BuildMessage(payload, reliability, channel, nowMs));
}

void NetClient::HandlePayload(const std::vector<std::uint8_t>& payload) {
    if (auto accepted = vc::protocol::ParseConnectionAccepted(payload)) {
        m_state = State::Connected;
        m_playerId = accepted->playerId;
        SetStatus("подключено к " + m_server.ip + ":" + std::to_string(m_server.port) +
                  " (id " + std::to_string(m_playerId.load()) + ")");
        Log("net: подключено, playerId=" + std::to_string(m_playerId.load()) + "\n");
    } else if (auto rejected = vc::protocol::ParseConnectionRejected(payload)) {
        m_state = State::Failed;
        SetStatus("сервер отказал: " + *rejected);
        Log("net: сервер отказал: " + *rejected + "\n");
    } else if (auto message = vc::protocol::ParseServerMessage(payload)) {
        PushChatLine(*message);
        Log("net[chat]: " + *message + "\n");
    } else if (auto streamIn = vc::protocol::ParsePlayerStreamIn(payload)) {
        std::lock_guard lock(m_mutex);
        RemotePlayer& remote = m_remotePlayers[streamIn->playerId];
        remote.id = streamIn->playerId;
        remote.skin = streamIn->skin;
        remote.nick = streamIn->nick;
        remote.targetPosition = streamIn->position;
        remote.hasPosition = true;
        Log("net: stream-in игрока " + streamIn->nick + " (id " +
            std::to_string(streamIn->playerId) + ", skin " +
            std::to_string(streamIn->skin) + ")\n");
    } else if (auto streamOut = vc::protocol::ParsePlayerStreamOut(payload)) {
        std::lock_guard lock(m_mutex);
        m_remotePlayers.erase(*streamOut);
        m_occupancy.erase(*streamOut);
        Log("net: stream-out игрока id " + std::to_string(*streamOut) + "\n");
    } else if (auto update = vc::protocol::ParsePlayerSyncUpdate(payload)) {
        std::lock_guard lock(m_mutex);
        auto it = m_remotePlayers.find(update->playerId);
        if (it != m_remotePlayers.end()) {
            it->second.targetPosition = update->position;
            it->second.hasPosition = true;
            it->second.health = update->health;
            it->second.weapon = update->weapon;
            ++it->second.syncCount;
        }
    } else if (auto teleport = vc::protocol::ParseTeleport(payload)) {
        std::lock_guard lock(m_mutex);
        m_pendingTeleport = LocalTeleport{teleport->position, teleport->heading};
        Log("net: сервер телепортирует нас\n");
    } else if (auto give = vc::protocol::ParseGiveWeapon(payload)) {
        std::lock_guard lock(m_mutex);
        m_pendingGiveWeapons.push_back(*give);
        Log("net: сервер выдал оружие id " + std::to_string(give->weapon) +
            " (патронов " + std::to_string(give->ammo) + ")\n");
    } else if (auto dmg = vc::protocol::ParseTakeDamage(payload)) {
        std::lock_guard lock(m_mutex);
        std::string nick = "#" + std::to_string(dmg->attackerId);
        if (auto it = m_remotePlayers.find(dmg->attackerId); it != m_remotePlayers.end())
            nick = it->second.nick;
        m_pendingDamage.push_back(PendingDamage{nick, dmg->amount, dmg->weapon});
        Log("net: урон " + std::to_string(static_cast<int>(dmg->amount)) +
            " от игрока " + nick + "\n");
    } else if (auto vehicleIn = vc::protocol::ParseVehicleStreamIn(payload)) {
        std::lock_guard lock(m_mutex);
        m_remoteVehicles[vehicleIn->vehicleId] = RemoteVehicle{
            vehicleIn->vehicleId, vehicleIn->model, vehicleIn->position, vehicleIn->heading};
        m_remoteVehicles[vehicleIn->vehicleId].health = vehicleIn->health;
        Log("net: stream-in машины id " + std::to_string(vehicleIn->vehicleId) +
            " (модель " + std::to_string(vehicleIn->model) + ")\n");
    } else if (auto vehicleOut = vc::protocol::ParseVehicleStreamOut(payload)) {
        std::lock_guard lock(m_mutex);
        m_remoteVehicles.erase(*vehicleOut);
        Log("net: stream-out машины id " + std::to_string(*vehicleOut) + "\n");
    } else if (auto occ = vc::protocol::ParseVehicleOccupant(payload)) {
        std::lock_guard lock(m_mutex);
        if (occ->seat == vc::protocol::kSeatVacated)
            m_occupancy.erase(occ->playerId);
        else
            m_occupancy[occ->playerId] = {occ->vehicleId, occ->seat};
        Log("net: occupant veh " + std::to_string(occ->vehicleId) + " player " +
            std::to_string(occ->playerId) + " seat " + std::to_string(occ->seat) + "\n");
    } else if (auto pose = vc::protocol::ParseVehicleSyncUpdate(payload)) {
        std::lock_guard lock(m_mutex);
        auto it = m_remoteVehicles.find(pose->vehicleId);
        if (it != m_remoteVehicles.end()) {
            it->second.position = pose->position;
            it->second.targetQuat = {pose->quatX, pose->quatY, pose->quatZ, pose->quatW};
            it->second.hasPose = true;
            it->second.health = pose->health;
            ++it->second.syncCount;
        }
    }
}

std::optional<NetClient::LocalTeleport> NetClient::TakePendingTeleport() {
    std::lock_guard lock(m_mutex);
    auto result = m_pendingTeleport;
    m_pendingTeleport.reset();
    return result;
}

std::vector<vc::protocol::GiveWeaponData> NetClient::TakePendingGiveWeapons() {
    std::lock_guard lock(m_mutex);
    std::vector<vc::protocol::GiveWeaponData> result;
    result.swap(m_pendingGiveWeapons);
    return result;
}

std::vector<NetClient::PendingDamage> NetClient::TakePendingDamage() {
    std::lock_guard lock(m_mutex);
    std::vector<PendingDamage> result;
    result.swap(m_pendingDamage);
    return result;
}

void NetClient::ReportDamage(std::uint16_t victimId, float amount) {
    std::lock_guard lock(m_mutex);
    m_damageOutbox.push_back(
        vc::protocol::PlayerDamageData{victimId, amount, m_latestWeapon});
}

void NetClient::NotifyDeath() { m_deathPending.store(true); }

void NetClient::SetLocalVehicleState(const std::optional<LocalVehicleState>& state) {
    std::lock_guard lock(m_mutex);
    m_localVehicle = state;
}

void NetClient::NotifyEnterVehicle(std::uint16_t vehicleId, std::uint8_t seat) {
    m_vehicleEvent.store(static_cast<int>(vehicleId) | (static_cast<int>(seat) << 16));
}

void NetClient::NotifyExitVehicle() { m_vehicleEvent.store(-1); }

bool NetClient::HasFreePassengerSeat(std::uint16_t vehicleId) const {
    std::lock_guard lock(m_mutex);
    bool taken[3] = {};
    for (const auto& [playerId, where] : m_occupancy)
        if (where.first == vehicleId && where.second >= 1 && where.second <= 3)
            taken[where.second - 1] = true;
    return !(taken[0] && taken[1] && taken[2]);
}

std::vector<NetClient::RemotePlayer> NetClient::RemotePlayers() const {
    std::lock_guard lock(m_mutex);
    std::vector<RemotePlayer> out;
    out.reserve(m_remotePlayers.size());
    for (const auto& [id, remote] : m_remotePlayers) {
        RemotePlayer copy = remote;
        if (auto it = m_occupancy.find(id); it != m_occupancy.end()) {
            copy.inVehicleId = it->second.first;
            copy.seat = it->second.second;
        }
        out.push_back(copy);
    }
    return out;
}

std::vector<NetClient::RemoteVehicle> NetClient::RemoteVehicles() const {
    std::lock_guard lock(m_mutex);
    std::vector<RemoteVehicle> out;
    out.reserve(m_remoteVehicles.size());
    for (const auto& [id, remote] : m_remoteVehicles) out.push_back(remote);
    return out;
}

void NetClient::ThreadMain() {
    bool requestSent = false;
    std::uint64_t connectStartedMs = 0;
    std::uint64_t lastSyncMs = 0;
    std::uint64_t lastKeepAliveMs = 0;

    while (!m_stop.load()) {
        const std::uint64_t now = NowMs();
        const State state = m_state.load();
        if (state == State::Failed) {
            Sleep(200);
            continue;
        }

        if (state == State::Connecting && !requestSent) {
            requestSent = true;
            connectStartedMs = now;
            SendPayloadLockedPeer(
                vc::protocol::BuildConnectionRequest({vc::PROTOCOL_VERSION, m_nick}),
                vc::net::Reliability::ReliableOrdered, 0, now);
        }
        if (state == State::Connecting && now - connectStartedMs > 10000) {
            m_state = State::Failed;
            SetStatus("сервер " + m_server.ip + ":" + std::to_string(m_server.port) +
                      " не отвечает (таймаут)");
            Log("net: таймаут подключения\n");
            continue;
        }

        if (auto received = m_socket.Receive(20)) {
            const auto incoming = m_peer.HandleDatagram(received->data);
            for (const auto& payload : incoming.payloads) HandlePayload(payload);
            if (auto ack = m_peer.BuildAckDatagram()) m_socket.SendTo(m_server, *ack);
        }
        for (const auto& resend : m_peer.CollectResends(now)) m_socket.SendTo(m_server, resend);

        // Смерть локального игрока (заметил игровой поток по HP<=0).
        if (m_state.load() == State::Connected && m_deathPending.exchange(false)) {
            SendPayloadLockedPeer(
                {static_cast<std::uint8_t>(vc::protocol::ClientPacket::Death)},
                vc::net::Reliability::ReliableOrdered, 0, now);
            Log("net: отправлено уведомление о смерти\n");
        }

        // Посадка/выход из серверной машины + поза водителя 10 Гц.
        if (m_state.load() == State::Connected) {
            const int event = m_vehicleEvent.exchange(-2);
            if (event >= 0) {
                const auto vehId = static_cast<std::uint16_t>(event & 0xFFFF);
                const auto seat = static_cast<std::uint8_t>((event >> 16) & 0xFF);
                SendPayloadLockedPeer(vc::protocol::BuildEnterVehicle({vehId, seat}),
                                      vc::net::Reliability::ReliableOrdered, 0, now);
                Log("net: сели в машину id " + std::to_string(vehId) + " место " +
                    std::to_string(seat) + "\n");
            } else if (event == -1) {
                SendPayloadLockedPeer(
                    {static_cast<std::uint8_t>(vc::protocol::ClientPacket::ExitVehicle)},
                    vc::net::Reliability::ReliableOrdered, 0, now);
                Log("net: вышли из машины\n");
            }
            static std::uint64_t lastVehicleSyncMs = 0;
            if (now - lastVehicleSyncMs >= 100) {
                std::optional<LocalVehicleState> local;
                {
                    std::lock_guard lock(m_mutex);
                    local = m_localVehicle;
                }
                if (local) {
                    lastVehicleSyncMs = now;
                    SendPayloadLockedPeer(
                        vc::protocol::BuildInVehicleSync(
                            {local->vehicleId, local->position, local->quat.x,
                             local->quat.y, local->quat.z, local->quat.w,
                             local->health}),
                        vc::net::Reliability::UnreliableSequenced, 2, now);
                }
            }
        }

        // Исходящий чат из игрового потока.
        {
            std::vector<std::string> outbox;
            {
                std::lock_guard lock(m_mutex);
                outbox.swap(m_chatOutbox);
            }
            if (m_state.load() == State::Connected)
                for (const std::string& text : outbox)
                    SendPayloadLockedPeer(vc::protocol::BuildChatMessage(text),
                                          vc::net::Reliability::ReliableOrdered, 0, now);
        }

        // Доклады об уроне по куклам из игрового потока (ReportDamage).
        {
            std::vector<vc::protocol::PlayerDamageData> outbox;
            {
                std::lock_guard lock(m_mutex);
                outbox.swap(m_damageOutbox);
            }
            if (m_state.load() == State::Connected)
                for (const auto& d : outbox)
                    SendPayloadLockedPeer(vc::protocol::BuildPlayerDamage(d),
                                          vc::net::Reliability::ReliableOrdered, 0, now);
        }

        if (m_state.load() == State::Connected && now - lastSyncMs >= 100) {
            lastSyncMs = now;
            std::optional<vc::Vector3> position;
            std::uint8_t health = 100;
            std::uint8_t weapon = 0;
            {
                std::lock_guard lock(m_mutex);
                position = m_latestPosition;
                health = m_latestHealth;
                weapon = m_latestWeapon;
            }
            if (position) {
                SendPayloadLockedPeer(vc::protocol::BuildOnFootSync({*position, health, weapon}),
                                      vc::net::Reliability::UnreliableSequenced, 1, now);
            } else if (now - lastKeepAliveMs >= 3000) {
                // Игрок не заспавнен (меню/пауза) — просто держим сессию живой.
                lastKeepAliveMs = now;
                SendPayloadLockedPeer(
                    {static_cast<std::uint8_t>(vc::protocol::ClientPacket::KeepAlive)},
                    vc::net::Reliability::Unreliable, 0, now);
            }
        }
    }
}

} // namespace vc::network
