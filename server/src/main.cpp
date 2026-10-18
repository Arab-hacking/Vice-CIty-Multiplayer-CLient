// Игровой сервер VC:MP — первая сетевая версия (Этап 2, шаг 1).
//
// Пока умеет:
//  - отвечать на RakNet unconnected ping (pong + MOTD);
//  - отвечать на SA-MP query 'i' (имя/онлайн/режим) для сервер-браузеров;
//  - принимать RakNet-датаграммы, логировать пакеты и подтверждать надёжные (ACK).
// Игровая логика (подключение игроков, синхронизация) — следующие шаги Этапа 2-3.

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <shared/net/offline.h>
#include <shared/net/raknet.h>
#include <shared/net/udp_socket.h>
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
    std::printf("Слушаю UDP 0.0.0.0:%u  name=\"%s\"  (Ctrl+C — стоп)\n", port, hostname.c_str());
    std::fflush(stdout);

    while (g_running) {
        auto received = socket.Receive(500);
        if (!received) continue;
        const auto& data = received->data;
        const vc::net::Endpoint& from = received->from;

        if (auto pingTime = vc::net::ParsePingTime(data)) {
            const std::string motd = hostname + ";0;100;freeroam;ru";
            socket.SendTo(from, vc::net::BuildUnconnectedPong(*pingTime, kServerGuid, motd));
            continue;
        }
        if (vc::net::IsSampQuery(data, vc::net::kSampQueryInfo)) {
            vc::net::SampServerInfo info;
            info.players = 0;
            info.maxPlayers = 100;
            info.hostname = hostname;
            info.gamemode = "freeroam";
            info.language = "Russian";
            socket.SendTo(from, vc::net::BuildSampInfoReply({"0.0.0.0", port}, info));
            continue;
        }
        if (auto datagram = vc::net::DecodeDatagram(data)) {
            std::printf("[net] %s:%u datagram: packets=%zu acks=%zu (%llu ms)\n",
                        from.ip.c_str(), from.port, datagram->packets.size(),
                        datagram->acks.size(),
                        static_cast<unsigned long long>(NowMs()));
            std::fflush(stdout);
            // Подтвердить надёжные пакеты одной ACK-датаграммой.
            std::vector<std::uint16_t> toAck;
            for (const auto& p : datagram->packets)
                if (vc::net::IsReliable(p.reliability)) toAck.push_back(p.messageNumber);
            if (!toAck.empty()) {
                vc::net::Datagram ack;
                ack.acks = vc::net::BuildAckRanges(std::move(toAck));
                socket.SendTo(from, vc::net::EncodeDatagram(ack));
            }
            continue;
        }
        std::printf("[net] %s:%u unknown datagram, %zu bytes, first=0x%02X\n",
                    from.ip.c_str(), from.port, data.size(), data.empty() ? 0 : data[0]);
        std::fflush(stdout);
    }
    std::printf("vc-server остановлен.\n");
    return 0;
}
