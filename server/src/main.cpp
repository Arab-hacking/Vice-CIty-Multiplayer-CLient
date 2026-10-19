// Игровой сервер VC:MP — Этап 2: сессии игроков поверх RakNet-фрейминга.
//
// Умеет:
//  - отвечать на RakNet unconnected ping и SA-MP query 'i' (браузеры/мониторинг);
//  - принимать ConnectionRequest (проверка версии протокола, выдача playerId);
//  - надёжную доставку через vc::net::ReliablePeer (ACK/переотправки);
//  - встречать игрока ServerMessage, рассылать чат, логировать позиции из OnFootSync;
//  - отключать по таймауту 15 секунд молчания.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
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

constexpr std::uint64_t kSessionTimeoutMs = 15000;

// Настройки из server.cfg (переопределяются CLI-аргументами).
struct ServerConfig {
    std::string hostname = "Vice City Multiplayer | reVC test server";
    std::uint16_t port = 7777;
    std::uint16_t maxPlayers = 100;
    std::string gamemode = "freeroam";
};
ServerConfig g_config;

std::string Trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

// Формат как в SA-MP: "ключ значение" построчно, # и // — комментарии.
bool LoadConfig(const std::string& path, ServerConfig& cfg) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#' || line.starts_with("//")) continue;
        std::istringstream iss(line);
        std::string key;
        iss >> key;
        std::string rest;
        std::getline(iss, rest);
        rest = Trim(rest);
        if (key == "hostname" && !rest.empty()) cfg.hostname = rest;
        else if (key == "gamemode" && !rest.empty()) cfg.gamemode = rest;
        else if (key == "port") cfg.port = static_cast<std::uint16_t>(std::atoi(rest.c_str()));
        else if (key == "maxplayers")
            cfg.maxPlayers = static_cast<std::uint16_t>(std::atoi(rest.c_str()));
        // password/rcon_password/announce/weburl пока игнорируются (см. DEPLOY-SERVER.md)
    }
    if (cfg.maxPlayers == 0) cfg.maxPlayers = 100;
    return true;
}

struct Session {
    vc::net::Endpoint endpoint;
    vc::net::ReliablePeer peer;
    bool joined = false;
    std::uint16_t playerId = 0;
    std::string nick;
    std::uint64_t lastSeenMs = 0;
    vc::Vector3 position{};
    bool hasPosition = false;
    float moveDirX = 0.0f; // последнее направление движения (бот встаёт перед игроком)
    float moveDirY = 1.0f;
    std::uint8_t health = 100;
    std::set<std::uint16_t> streamedIn; // кого этот клиент сейчас «видит» (радиус-стриминг)
    vc::Vector3 lastLoggedPosition{1e9f, 1e9f, 1e9f};
};

// Серверный NPC-бот (команда /bot): фейковый «игрок» без соединения.
// Сервер сам двигает его по кругу и рассылает SyncUpdate всем клиентам —
// клиенты рисуют обычную куклу, vc-bot.exe больше не нужен.
struct FakeBot {
    std::uint16_t id = 0;
    std::uint16_t ownerId = 0;
    std::string nick;
    vc::Vector3 center{};
    vc::Vector3 position{};
    float phase = 0.0f;
};
std::vector<FakeBot> g_bots;

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
               const std::vector<std::uint8_t>& payload, const Session* except = nullptr,
               vc::net::Reliability reliability = vc::net::Reliability::ReliableOrdered,
               std::uint8_t channel = 0) {
    for (auto& [key, session] : sessions) {
        if (!session.joined || &session == except) continue;
        Send(socket, session, payload, reliability, channel);
    }
}

// --- Стриминг по радиусу: клиент видит только тех, кто ближе 250 м. ---
constexpr float kStreamRadius = 250.0f;

struct VisibleEntity {
    std::uint16_t id;
    std::string nick;
    vc::Vector3 position;
};

std::vector<VisibleEntity> BuildEntities(const std::map<std::string, Session>& sessions) {
    std::vector<VisibleEntity> out;
    for (const auto& [key, s] : sessions)
        if (s.joined) out.push_back({s.playerId, s.nick, s.position});
    for (const auto& b : g_bots) out.push_back({b.id, b.nick, b.position});
    return out;
}

// SyncUpdate только тем, у кого объект в стриме.
void SendSyncToViewers(vc::net::UdpSocket& socket, std::map<std::string, Session>& sessions,
                       std::uint16_t moverId, const std::vector<std::uint8_t>& payload) {
    for (auto& [key, session] : sessions) {
        if (!session.joined || session.playerId == moverId) continue;
        if (!session.streamedIn.contains(moverId)) continue;
        Send(socket, session, payload, vc::net::Reliability::UnreliableSequenced, 1);
    }
}

// Раз в 500 мс сверяем видимость: новые в радиусе -> StreamIn, ушедшие -> StreamOut.
void StreamTick(vc::net::UdpSocket& socket, std::map<std::string, Session>& sessions) {
    const auto entities = BuildEntities(sessions);
    for (auto& [key, session] : sessions) {
        if (!session.joined) continue;
        std::set<std::uint16_t> desired;
        for (const auto& e : entities) {
            if (e.id == session.playerId) continue;
            const float dx = e.position.x - session.position.x;
            const float dy = e.position.y - session.position.y;
            const float dz = e.position.z - session.position.z;
            if (dx * dx + dy * dy + dz * dz <= kStreamRadius * kStreamRadius)
                desired.insert(e.id);
        }
        for (std::uint16_t id : desired) {
            if (session.streamedIn.contains(id)) continue;
            const auto it = std::find_if(entities.begin(), entities.end(),
                                         [&](const VisibleEntity& e) { return e.id == id; });
            if (it != entities.end())
                Send(socket, session,
                     vc::protocol::BuildPlayerStreamIn({it->id, it->nick, it->position}),
                     vc::net::Reliability::ReliableOrdered, 0);
        }
        for (std::uint16_t id : session.streamedIn)
            if (!desired.contains(id))
                Send(socket, session, vc::protocol::BuildPlayerStreamOut(id),
                     vc::net::Reliability::ReliableOrdered, 0);
        session.streamedIn = desired;
    }
}

std::size_t JoinedCount(const std::map<std::string, Session>& sessions) {
    std::size_t n = 0;
    for (const auto& [key, s] : sessions) n += s.joined ? 1 : 0;
    return n;
}

void RemoveBotOf(vc::net::UdpSocket& socket, std::map<std::string, Session>& sessions,
                 std::uint16_t ownerId) {
    const auto it = std::find_if(g_bots.begin(), g_bots.end(),
                                 [&](const FakeBot& b) { return b.ownerId == ownerId; });
    if (it == g_bots.end()) return;
    const std::uint16_t botId = it->id;
    std::printf("[server] Бот %s (id %u) убран\n", it->nick.c_str(), botId);
    std::fflush(stdout);
    g_bots.erase(it);
    for (auto& [key, session] : sessions) {
        if (session.streamedIn.erase(botId) > 0 && session.joined)
            Send(socket, session, vc::protocol::BuildPlayerStreamOut(botId),
                 vc::net::Reliability::ReliableOrdered, 0);
    }
}

// Чат-команды (текст начинается с '/'): не ретранслируются, а выполняются сервером.
// Шаг 2: только /help, /bot, /unbot.
void HandleCommand(vc::net::UdpSocket& socket, std::map<std::string, Session>& sessions,
                   Session& session, std::uint16_t& nextPlayerId, const std::string& text) {
    auto reply = [&](const std::string& msg) {
        Send(socket, session, vc::protocol::BuildServerMessage(msg),
             vc::net::Reliability::ReliableOrdered, 0);
    };
    std::printf("[cmd] %s: %s\n", session.nick.c_str(), text.c_str());
    std::fflush(stdout);

    if (text == "/help") {
        reply("Команды: /bot - бот перед тобой, /unbot - убрать бота");
    } else if (text == "/bot") {
        if (!session.hasPosition) {
            reply("Позиция ещё неизвестна: сделай пару шагов в игре и повтори /bot");
            return;
        }
        // Бот появляется в 4 метрах перед игроком и ходит по кругу r=3м.
        const vc::Vector3 front{session.position.x + session.moveDirX * 4.0f,
                                session.position.y + session.moveDirY * 4.0f,
                                session.position.z};
        auto it = std::find_if(g_bots.begin(), g_bots.end(), [&](const FakeBot& b) {
            return b.ownerId == session.playerId;
        });
        if (it == g_bots.end()) {
            FakeBot bot;
            bot.id = nextPlayerId++;
            bot.ownerId = session.playerId;
            bot.nick = "Bot_" + session.nick.substr(0, 12);
            bot.center = front;
            bot.position = front;
            g_bots.push_back(bot);
            StreamTick(socket, sessions); // кукла появится сразу, не через 500 мс
            reply("Бот " + bot.nick + " появился перед тобой! /unbot - убрать");
            std::printf("[server] Создан бот %s (id %u) для %s\n", bot.nick.c_str(), bot.id,
                        session.nick.c_str());
            std::fflush(stdout);
        } else {
            it->center = front;
            it->position = front;
            reply("Бот перемещён перед тобой");
        }
    } else if (text == "/unbot") {
        RemoveBotOf(socket, sessions, session.playerId);
        reply("Бот убран");
    } else {
        reply("Неизвестная команда. /help - список команд");
    }
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
            Broadcast(socket, sessions,
                      vc::protocol::BuildPlayerStreamOut(session.playerId), &session);
            RemoveBotOf(socket, sessions, session.playerId);
            for (auto& [key, other] : sessions) other.streamedIn.erase(session.playerId);
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
        if (!session.joined && JoinedCount(sessions) >= g_config.maxPlayers) {
            Send(socket, session,
                 vc::protocol::BuildConnectionRejected("Сервер заполнен, попробуйте позже."),
                 vc::net::Reliability::ReliableOrdered, 0);
            std::printf("[server] %s отклонён: сервер заполнен\n",
                        KeyOf(session.endpoint).c_str());
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
            // Видимость (игроки и боты в радиусе 250 м) раздаёт StreamTick.
            StreamTick(socket, sessions);
        }
        Send(socket, session,
             vc::protocol::BuildConnectionAccepted({session.playerId, g_config.maxPlayers}),
             vc::net::Reliability::ReliableOrdered, 0);
        Send(socket, session,
             vc::protocol::BuildServerMessage(
                 "Добро пожаловать на Vice City Multiplayer, " + session.nick +
                 "! Ты игрок #" + std::to_string(session.playerId) +
                 ". Онлайн: " + std::to_string(JoinedCount(sessions)) + "/" +
                 std::to_string(g_config.maxPlayers)),
             vc::net::Reliability::ReliableOrdered, 0);
        Send(socket, session,
             vc::protocol::BuildServerMessage("Команды в чате (T): /bot - бот перед тобой, "
                                              "/unbot - убрать, /help"),
             vc::net::Reliability::ReliableOrdered, 0);
        return false;
    }

    if (!session.joined) return false;

    if (id == static_cast<std::uint8_t>(vc::protocol::ClientPacket::OnFootSync)) {
        if (const auto sync = vc::protocol::ParseOnFootSync(payload)) {
            const vc::Vector3& pos = sync->position;
            {
                const float mdx = pos.x - session.position.x;
                const float mdy = pos.y - session.position.y;
                const float len = std::sqrt(mdx * mdx + mdy * mdy);
                if (session.hasPosition && len > 0.25f && len < 30.0f) {
                    session.moveDirX = mdx / len;
                    session.moveDirY = mdy / len;
                }
            }
            session.position = pos;
            session.health = sync->health;
            session.hasPosition = true;
            // Ретрансляция зрителям в радиусе — из этих пакетов клиенты двигают кукол.
            SendSyncToViewers(
                socket, sessions, session.playerId,
                vc::protocol::BuildPlayerSyncUpdate({session.playerId, pos, sync->health}));
            const float dx = pos.x - session.lastLoggedPosition.x;
            const float dy = pos.y - session.lastLoggedPosition.y;
            const float dz = pos.z - session.lastLoggedPosition.z;
            if (dx * dx + dy * dy + dz * dz > 25.0f) {
                session.lastLoggedPosition = pos;
                std::printf("[sync] %s (id %u) pos=(%.1f, %.1f, %.1f) HP=%u\n",
                            session.nick.c_str(), session.playerId, pos.x, pos.y, pos.z,
                            sync->health);
                std::fflush(stdout);
            }
        }
        return false;
    }

    if (id == static_cast<std::uint8_t>(vc::protocol::ClientPacket::ChatMessage)) {
        if (const auto text = vc::protocol::ParseChatMessage(payload)) {
            if (!text->empty() && text->front() == '/') {
                HandleCommand(socket, sessions, session, nextPlayerId, *text);
            } else {
                const std::string line = session.nick + ": " + *text;
                std::printf("[chat] %s\n", line.c_str());
                std::fflush(stdout);
                Broadcast(socket, sessions, vc::protocol::BuildServerMessage(line));
            }
        }
        return false;
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    vc::InitConsoleUtf8();
    std::string configPath = "server.cfg";
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "--config") configPath = argv[i + 1];
    const bool configLoaded = LoadConfig(configPath, g_config);

    // CLI переопределяет конфиг.
    for (int i = 1; i + 1 < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--port")
            g_config.port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        if (arg == "--name") g_config.hostname = argv[++i];
        if (arg == "--config") ++i;
    }
    const std::uint16_t port = g_config.port;
    const std::string& hostname = g_config.hostname;
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
    if (configLoaded)
        std::printf("Конфиг загружен из %s\n", configPath.c_str());
    else
        std::printf("Конфиг %s не найден — использую значения по умолчанию\n",
                    configPath.c_str());
    std::printf("Слушаю UDP 0.0.0.0:%u  name=\"%s\"  mode=\"%s\"  maxplayers=%u  (Ctrl+C — стоп)\n",
                port, hostname.c_str(), g_config.gamemode.c_str(), g_config.maxPlayers);
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
                                         std::to_string(g_config.maxPlayers) + ";" + g_config.gamemode + ";ru";
                socket.SendTo(from, vc::net::BuildUnconnectedPong(*pingTime, kServerGuid, motd));
            } else if (vc::net::IsSampQuery(data, vc::net::kSampQueryInfo)) {
                vc::net::SampServerInfo info;
                info.players = static_cast<std::uint16_t>(JoinedCount(sessions));
                info.maxPlayers = g_config.maxPlayers;
                info.hostname = hostname;
                info.gamemode = g_config.gamemode;
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
                    Broadcast(socket, sessions,
                              vc::protocol::BuildPlayerStreamOut(session.playerId), &session);
                    RemoveBotOf(socket, sessions, session.playerId);
                    const std::uint16_t goneId = session.playerId;
                    for (auto& [otherKey, other] : sessions) other.streamedIn.erase(goneId);
                }
                it = sessions.erase(it);
            } else {
                ++it;
            }
        }

        // Стриминг по радиусу — раз в 500 мс.
        static std::uint64_t lastStreamTick = 0;
        if (now - lastStreamTick >= 500) {
            lastStreamTick = now;
            StreamTick(socket, sessions);
        }

        // Серверные боты (/bot): круг радиусом 3 м вокруг точки спавна, 10 Гц.
        static std::uint64_t lastBotTick = 0;
        if (now - lastBotTick >= 100 && !g_bots.empty()) {
            lastBotTick = now;
            for (auto& bot : g_bots) {
                bot.phase += 0.12f; // ~1.2 рад/с -> скорость ~3.6 м/с
                bot.position = {bot.center.x + std::cos(bot.phase) * 3.0f,
                                bot.center.y + std::sin(bot.phase) * 3.0f, bot.center.z};
                SendSyncToViewers(
                    socket, sessions, bot.id,
                    vc::protocol::BuildPlayerSyncUpdate({bot.id, bot.position, 100}));
            }
        }
    }
    std::printf("vc-server остановлен.\n");
    return 0;
}
