// vc-bot — консольный тестовый клиент НАШЕГО сервера без игры.
// Подключается по протоколу VC:MP, шлёт OnFootSync (идёт по прямой),
// печатает сообщения сервера. Нужен для автотестов и проверки сервера.
// Использование: vc-bot [--ip 127.0.0.1] [--port 7777] [--nick Bot] [--seconds 5]

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include <shared/net/peer.h>
#include <shared/net/udp_socket.h>
#include <shared/protocol/codec.h>
#include <shared/utils/console.h>
#include <shared/version.h>

namespace {
std::uint64_t NowMs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}
} // namespace

int main(int argc, char** argv) {
    vc::InitConsoleUtf8();
    vc::net::Endpoint server{"127.0.0.1", 7777};
    std::string nick = "TestBot";
    std::string say = "Привет! Я тестовый бот vc-bot.";
    int seconds = 5;
    for (int i = 1; i + 1 < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--ip") server.ip = argv[++i];
        if (arg == "--port") server.port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        if (arg == "--nick") nick = argv[++i];
        if (arg == "--say") say = argv[++i];
        if (arg == "--seconds") seconds = std::atoi(argv[++i]);
    }

    vc::net::UdpSocket socket;
    if (!socket.IsValid() || !socket.Bind(0)) {
        std::fprintf(stderr, "vc-bot: не создать сокет\n");
        return 1;
    }
    vc::net::ReliablePeer peer;
    auto send = [&](const std::vector<std::uint8_t>& payload, vc::net::Reliability rel,
                    std::uint8_t channel) {
        socket.SendTo(server, peer.BuildMessage(payload, rel, channel, NowMs()));
    };

    send(vc::protocol::BuildConnectionRequest({vc::PROTOCOL_VERSION, nick}),
         vc::net::Reliability::ReliableOrdered, 0);
    std::printf("vc-bot: подключаюсь к %s:%u как %s...\n", server.ip.c_str(), server.port,
                nick.c_str());
    std::fflush(stdout);

    bool accepted = false;
    bool chatSent = false;
    std::uint16_t myId = 0;
    const std::uint64_t started = NowMs();
    std::uint64_t lastSync = 0;
    // Ходим туда-сюда перед особняком Версетти (ровная площадка, z ~17.3):
    // игрок спавнится рядом (-378, -589) и сразу видит куклу бота.
    float walkX = -395.0f;
    float walkDir = 1.0f;

    while (NowMs() - started < static_cast<std::uint64_t>(seconds) * 1000 + 5000) {
        if (auto received = socket.Receive(50)) {
            const auto incoming = peer.HandleDatagram(received->data);
            for (const auto& payload : incoming.payloads) {
                if (payload.empty()) continue;
                if (auto acc = vc::protocol::ParseConnectionAccepted(payload)) {
                    accepted = true;
                    myId = acc->playerId;
                    std::printf("vc-bot: ПОДКЛЮЧЕНО, playerId=%u\n", myId);
                    std::fflush(stdout);
                } else if (auto msg = vc::protocol::ParseServerMessage(payload)) {
                    std::printf("[server] %s\n", msg->c_str());
                    std::fflush(stdout);
                } else if (auto vi = vc::protocol::ParseVehicleStreamIn(payload)) {
                    std::printf("[bot] вижу машину id %u (модель %u) в (%.1f, %.1f, %.1f)\n",
                                vi->vehicleId, vi->model, vi->position.x, vi->position.y,
                                vi->position.z);
                    std::fflush(stdout);
                } else if (auto si = vc::protocol::ParsePlayerStreamIn(payload)) {
                    std::printf("[bot] вижу игрока %s (id %u) в (%.1f, %.1f, %.1f)\n",
                                si->nick.c_str(), si->playerId, si->position.x,
                                si->position.y, si->position.z);
                    std::fflush(stdout);
                } else if (auto su = vc::protocol::ParsePlayerSyncUpdate(payload)) {
                    static int syncPrinted = 0;
                    if (syncPrinted < 8) {
                        ++syncPrinted;
                        std::printf("[bot] sync id %u -> (%.1f, %.1f, %.1f)\n", su->playerId,
                                    su->position.x, su->position.y, su->position.z);
                        std::fflush(stdout);
                    }
                } else if (auto rej = vc::protocol::ParseConnectionRejected(payload)) {
                    std::printf("vc-bot: ОТКАЗ: %s\n", rej->c_str());
                    return 1;
                }
            }
            if (auto ack = peer.BuildAckDatagram()) socket.SendTo(server, *ack);
        }
        for (const auto& resend : peer.CollectResends(NowMs()))
            socket.SendTo(server, resend);

        // Чат — через 1.5 c после старта: сервер уже получил синки и знает позицию
        // (иначе /bot отклоняется гардом hasPosition).
        if (accepted && !chatSent && NowMs() - started >= 1500) {
            chatSent = true;
            send(vc::protocol::BuildChatMessage(say),
                 vc::net::Reliability::ReliableOrdered, 0);
        }
        if (accepted && NowMs() - lastSync >= 100) {
            lastSync = NowMs();
            walkX += walkDir * 0.55f; // ~5.5 м/с — скорость бега
            if (walkX > -360.0f) walkDir = -1.0f;
            if (walkX < -395.0f) walkDir = 1.0f;
            send(vc::protocol::BuildOnFootSync({{walkX, -542.0f, 17.3f}, 100}),
                 vc::net::Reliability::UnreliableSequenced, 1);
            if (NowMs() - started > static_cast<std::uint64_t>(seconds) * 1000) break;
        }
    }

    if (!accepted) {
        std::printf("vc-bot: сервер не принял подключение за отведённое время\n");
        return 1;
    }
    // Вежливый дисконнект, чтобы сервер сразу закрыл сессию, а не по таймауту.
    send({static_cast<std::uint8_t>(vc::protocol::ClientPacket::Disconnect)},
         vc::net::Reliability::Unreliable, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::printf("vc-bot: готово (playerId=%u)\n", myId);
    return 0;
}
