// Игровой сервер VC:MP — Этап 2: сессии игроков поверх RakNet-фрейминга.
//
// Умеет:
//  - отвечать на RakNet unconnected ping и SA-MP query 'i' (браузеры/мониторинг);
//  - принимать ConnectionRequest (проверка версии протокола, выдача playerId);
//  - надёжную доставку через vc::net::ReliablePeer (ACK/переотправки);
//  - встречать игрока ServerMessage, рассылать чат, логировать позиции из OnFootSync;
//  - отключать по таймауту 15 секунд молчания.

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

#include <shared/net/offline.h>
#include <shared/net/peer.h>
#include <shared/net/raknet.h>
#include <shared/net/udp_socket.h>
#include <shared/protocol/codec.h>
#include <shared/utils/console.h>
#include <shared/version.h>

namespace {

std::atomic<bool> g_running{true};
void OnSignal(int) { g_running = false; }

std::uint64_t NowMs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

constexpr std::uint16_t kMaxPlayers = 100;
constexpr std::uint64_t kSessionTimeoutMs = 15000;

struct Session {
    vc::net::Endpoint endpoint;
    vc::net::ReliablePeer peer;
    bool joined = false;
    std::uint16_t playerId = 0;
    std::string nick;
    std::uint64_t lastSeenMs = 0;
    vc::Vector3 position{};
    vc::Vector3 lastLoggedPosition{1e9f, 1e9f, 1e9f};
};

std::string KeyOf(const vc::net::Endpoint& ep) {
    return ep.ip + ":" + std::to_string(ep.port);
}

void Send(vc::net::UdpSocket& socket, Session& session,
          const std::vector<std::uint8_t>& payload, vc::net::Reliability reliability,
          std::uint8_t channel) {
    socket.SendTo(session.endpoint,
                  session.peer.BuildMessage(payload, reliability, channel, NowMs()));
}

void Broadcast(vc::net::UdpSocket& socket, std::map<std::string, Session>& sessions,
               const std::vector<std::uint8_t>& payload, const Session* except = nullptr) {
    for (auto& [key, session] : sessions) {
        if (!session.joined || &session == except) continue;
        Send(socket, session, payload, vc::net::Reliability::ReliableOrdered, 0);
    }
}

std::size_t JoinedCount(const std::map<std::string, Session>& sessions) {
    std::size_t n = 0;
    for (const auto& [key, s] : sessions) n += s.joined ? 1 : 0;
    return n;
}

// Возвращает true, если сессию надо закрыть немедленно (вежливый Disconnect).
bool HandlePayload(vc::net::UdpSocket& socket, std::map<std::string, Session>& sessions,
                   Session& session, std::uint16_t& nextPlayerId,
                   const std::vector<std::uint8_t>& payload) {
    if (payload.empty()) return false;
    const auto id = payload[0];

    if (id == static_cast<std::uint8_t>(vc::protocol::ClientPacket::Disconnect)) {
        if (session.joined) {
            std::printf("[server] Игрок %s (id %u) вышел с сервера\n", session.nick.c_str(),
                        session.playerId);
            std::fflush(stdout);
            Broadcast(socket, sessions,
                      vc::protocol::BuildServerMessage(session.nick + " вышел с сервера"),
                      &session);
        }
        return true;
    }
    if (id == static_cast<std::uint8_t>(vc::protocol::ClientPacket::KeepAlive))
        return false; // lastSeen уже обновлён по датаграмме

    if (id == static_cast<std::uint8_t>(vc::protocol::ClientPacket::ConnectionRequest)) {
        const auto request = vc::protocol::ParseConnectionRequest(payload);
        if (!request) return false;
        if (request->protocolVersion != vc::PROTOCOL_VERSION) {
            Send(socket, session,
                 vc::protocol::BuildConnectionRejected(
                     "Несовместимая версия протокола клиента. Обновите клиент VC:MP."),
                 vc::net::Reliability::ReliableOrdered, 0);
            std::printf("[server] %s отклонён: протокол %u != %u\n",
                        KeyOf(session.endpoint).c_str(), request->protocolVersion,
                        vc::PROTOCOL_VERSION);
            std::fflush(stdout);
            return false;
        }
        if (!session.joined) {
            session.joined = true;
            session.playerId = nextPlayerId++;
            session.nick = request->nick;
            std::printf("[server] Игрок %s (id %u) подключился с %s. Онлайн: %zu\n",
                        session.nick.c_str(), session.playerId,
                        KeyOf(session.endpoint).c_str(), JoinedCount(sessions));
            std::fflush(stdout);
            Broadcast(socket, sessions,
                      vc::protocol::BuildServerMessage(session.nick + " подключился к серверу"),
                      &session);
        }
        Send(socket, session,
             vc::protocol::BuildConnectionAccepted({session.playerId, kMaxPlayers}),
             vc::net::Reliability::ReliableOrdered, 0);
        Send(socket, session,
             vc::protocol::BuildServerMessage(
                 "Добро пожаловать на Vice City Multiplayer, " + session.nick +
                 "! Ты игрок #" + std::to_string(session.playerId) +
                 ". Онлайн: " + std::to_string(JoinedCount(sessions)) + "/" +
                 std::to_string(kMaxPlayers)),
             vc::net::Reliability::ReliableOrdered, 0);
        return false;
    }

    if (!session.joined) return false;

    if (id == static_cast<std::uint8_t>(vc::protocol::ClientPacket::OnFootSync)) {
        if (const auto pos = vc::protocol::ParseOnFootSync(payload)) {
            session.position = *pos;
            const float dx = pos->x - session.lastLoggedPosition.x;
            const float dy = pos->y - session.lastLoggedPosition.y;
            const float dz = pos->z - session.lastLoggedPosition.z;
            if (dx * dx + dy * dy + dz * dz > 25.0f) {
                session.lastLoggedPosition = *pos;
                std::printf("[sync] %s (id %u) pos=(%.1f, %.1f, %.1f)\n",
                            session.nick.c_str(), session.playerId, pos->x, pos->y, pos->z);
                std::fflush(stdout);
            }
        }
        return false;
    }

    if (id == static_cast<std::uint8_t>(vc::protocol::ClientPacket::ChatMessage)) {
        if (const auto text = vc::protocol::ParseChatMessage(payload)) {
            const std::string line = session.nick + ": " + *text;
            std::printf("[chat] %s\n", line.c_str());
            std::fflush(stdout);
            Broadcast(socket, sessions, vc::protocol::BuildServerMessage(line));
        }
        return false;
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    vc::InitConsoleUtf8();
    std::uint16_t port = 7777;
    std::string hostname = "Vice City Multiplayer | reVC test server";
    for (int i = 1; i + 1 < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--port") port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        if (arg == "--name") hostname = argv[++i];
    }
    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);

    vc::net::UdpSocket socket;
    if (!socket.IsValid() || !socket.Bind(port)) {
        std::fprintf(stderr, "vc-server: не удалось занять UDP-порт %u\n", port);
        return 1;
    }
    constexpr std::uint64_t kServerGuid = 0x56434D5000000001ull; // "VCMP"
    std::printf("Vice City Multiplayer Server %s (protocol %u)\n",
                vc::CLIENT_VERSION, vc::PROTOCOL_VERSION);
    std::printf("Слушаю UDP 0.0.0.0:%u  name=\"%s\"  (Ctrl+C — стоп)\n", port,
                hostname.c_str());
    std::fflush(stdout);

    std::map<std::string, Session> sessions;
    std::uint16_t nextPlayerId = 0;

    while (g_running) {
        auto received = socket.Receive(50);
        if (received) {
            const auto& data = received->data;
            const vc::net::Endpoint& from = received->from;

            if (auto pingTime = vc::net::ParsePingTime(data)) {
                const std::string motd = hostname + ";" +
                                         std::to_string(JoinedCount(sessions)) + ";" +
                                         std::to_string(kMaxPlayers) + ";freeroam;ru";
                socket.SendTo(from, vc::net::BuildUnconnectedPong(*pingTime, kServerGuid, motd));
            } else if (vc::net::IsSampQuery(data, vc::net::kSampQueryInfo)) {
                vc::net::SampServerInfo info;
                info.players = static_cast<std::uint16_t>(JoinedCount(sessions));
                info.maxPlayers = kMaxPlayers;
                info.hostname = hostname;
                info.gamemode = "freeroam";
                info.language = "Russian";
                socket.SendTo(from, vc::net::BuildSampInfoReply({"0.0.0.0", port}, info));
            } else if (vc::net::DecodeDatagram(data).has_value()) {
                const std::string key = KeyOf(from);
                Session& session = sessions[key];
                session.endpoint = from;
                session.lastSeenMs = NowMs();
                const auto incoming = session.peer.HandleDatagram(data);
                bool closeSession = false;
                for (const auto& payload : incoming.payloads)
                    closeSession = HandlePayload(socket, sessions, session, nextPlayerId,
                                                 payload) ||
                                   closeSession;
                if (auto ack = session.peer.BuildAckDatagram())
                    socket.SendTo(from, *ack);
                if (closeSession) sessions.erase(key);
            } else {
                std::printf("[net] %s:%u unknown datagram, %zu bytes, first=0x%02X\n",
                            from.ip.c_str(), from.port, data.size(),
                            data.empty() ? 0 : data[0]);
                std::fflush(stdout);
            }
        }

        // Переотправки и таймауты.
        const std::uint64_t now = NowMs();
        for (auto it = sessions.begin(); it != sessions.end();) {
            Session& session = it->second;
            for (const auto& resend : session.peer.CollectResends(now))
                socket.SendTo(session.endpoint, resend);
            if (now - session.lastSeenMs > kSessionTimeoutMs) {
                if (session.joined) {
                    std::printf("[server] Игрок %s (id %u) отключился (таймаут)\n",
                                session.nick.c_str(), session.playerId);
                    std::fflush(stdout);
                    Broadcast(socket, sessions,
                              vc::protocol::BuildServerMessage(session.nick +
                                                                " вышел с сервера"),
                              &session);
                }
                it = sessions.erase(it);
            } else {
                ++it;
            }
        }
    }
    std::printf("vc-server остановлен.\n");
    return 0;
}
