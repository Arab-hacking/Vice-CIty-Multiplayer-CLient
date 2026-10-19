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
                               std::uint8_t health) {
    std::lock_guard lock(m_mutex);
    m_latestPosition = position;
    m_latestHealth = health;
}

void NetClient::SendChat(const std::string& text) {
    std::lock_guard lock(m_mutex);
    m_chatOutbox.push_back(text);
}

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
        Log("net: stream-out игрока id " + std::to_string(*streamOut) + "\n");
    } else if (auto update = vc::protocol::ParsePlayerSyncUpdate(payload)) {
        std::lock_guard lock(m_mutex);
        auto it = m_remotePlayers.find(update->playerId);
        if (it != m_remotePlayers.end()) {
            it->second.targetPosition = update->position;
            it->second.hasPosition = true;
            it->second.health = update->health;
            ++it->second.syncCount;
        }
    } else if (auto teleport = vc::protocol::ParseTeleport(payload)) {
        std::lock_guard lock(m_mutex);
        m_pendingTeleport = LocalTeleport{teleport->position, teleport->heading};
        Log("net: сервер телепортирует нас\n");
    }
}

std::optional<NetClient::LocalTeleport> NetClient::TakePendingTeleport() {
    std::lock_guard lock(m_mutex);
    auto result = m_pendingTeleport;
    m_pendingTeleport.reset();
    return result;
}

void NetClient::NotifyDeath() { m_deathPending.store(true); }

std::vector<NetClient::RemotePlayer> NetClient::RemotePlayers() const {
    std::lock_guard lock(m_mutex);
    std::vector<RemotePlayer> out;
    out.reserve(m_remotePlayers.size());
    for (const auto& [id, remote] : m_remotePlayers) out.push_back(remote);
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

        if (m_state.load() == State::Connected && now - lastSyncMs >= 100) {
            lastSyncMs = now;
            std::optional<vc::Vector3> position;
            std::uint8_t health = 100;
            {
                std::lock_guard lock(m_mutex);
                position = m_latestPosition;
                health = m_latestHealth;
            }
            if (position) {
                SendPayloadLockedPeer(vc::protocol::BuildOnFootSync({*position, health}),
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
