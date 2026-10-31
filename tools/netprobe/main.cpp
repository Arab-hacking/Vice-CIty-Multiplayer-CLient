// vc-netprobe — проверка доступности игрового сервера без запуска игры.
// Делает только то, что делает любой сервер-браузер/мониторинг:
//   1) RakNet unconnected ping -> pong (RTT + MOTD);
//   2) SA-MP query 'i' -> имя, онлайн, режим.
// Использование:
//   vc-netprobe <ip> <port> [--timeout-ms 3000]
//   vc-netprobe --file tools/netprobe/servers.example.txt
// Формат файла: строки "ip port название...", '#' — комментарий.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <sstream>
#include <string>

#include <shared/net/offline.h>
#include <shared/net/udp_socket.h>
#include <shared/utils/console.h>

namespace {

std::uint64_t NowMs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

void Probe(const vc::net::Endpoint& server, const std::string& label, int timeoutMs) {
    std::printf("=== %s (%s:%u) ===\n", label.c_str(), server.ip.c_str(), server.port);
    vc::net::UdpSocket socket;
    if (!socket.IsValid() || !socket.Bind(0)) {
        std::printf("  [err] не удалось создать UDP-сокет\n");
        return;
    }
    std::mt19937_64 rng(std::random_device{}());
    const std::uint64_t guid = rng();

    // 1) RakNet unconnected ping
    const std::uint64_t t0 = NowMs();
    socket.SendTo(server, vc::net::BuildUnconnectedPing(t0, guid));
    bool pongSeen = false;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        auto r = socket.Receive(200);
        if (!r) continue;
        if (auto pong = vc::net::ParseUnconnectedPong(r->data)) {
            std::printf("  [raknet-ping] pong за ~%llu мс, guid=%016llX, motd=\"%s\"\n",
                        static_cast<unsigned long long>(NowMs() - pong->timeMs),
                        static_cast<unsigned long long>(pong->serverGuid),
                        pong->motd.c_str());
            pongSeen = true;
            break;
        }
        std::printf("  [raknet-ping] получен не-pong ответ: %zu байт, first=0x%02X\n",
                    r->data.size(), r->data.empty() ? 0 : r->data[0]);
        break;
    }
    if (!pongSeen) std::printf("  [raknet-ping] ответа нет за %d мс\n", timeoutMs);

    // 2) SA-MP query
    socket.SendTo(server, vc::net::BuildSampQuery(server, vc::net::kSampQueryInfo));
    bool infoSeen = false;
    const auto deadline2 = std::chrono::steady_clock::now() +
                           std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline2) {
        auto r = socket.Receive(200);
        if (!r) continue;
        if (auto info = vc::net::ParseSampInfoReply(r->data)) {
            std::printf("  [samp-query] \"%s\" | игроков %u/%u | режим \"%s\" | язык \"%s\"%s\n",
                        info->hostname.c_str(), info->players, info->maxPlayers,
                        info->gamemode.c_str(), info->language.c_str(),
                        info->passworded ? " | пароль" : "");
            infoSeen = true;
            break;
        }
    }
    if (!infoSeen) std::printf("  [samp-query] ответа нет за %d мс\n", timeoutMs);
    std::fflush(stdout);
}

} // namespace

int main(int argc, char** argv) {
    vc::InitConsoleUtf8();
    int timeoutMs = 3000;
    std::string filePath;
    std::string ip;
    std::uint16_t port = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--timeout-ms" && i + 1 < argc) {
            timeoutMs = std::atoi(argv[++i]);
        } else if (arg == "--file" && i + 1 < argc) {
            filePath = argv[++i];
        } else if (ip.empty()) {
            ip = arg;
        } else if (port == 0) {
            port = static_cast<std::uint16_t>(std::atoi(arg.c_str()));
        }
    }

    if (!filePath.empty()) {
        std::ifstream in(filePath);
        if (!in) {
            std::fprintf(stderr, "vc-netprobe: не открыть файл %s\n", filePath.c_str());
            return 1;
        }
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream iss(line);
            std::string sip, name;
            std::uint16_t p = 0;
            if (!(iss >> sip >> p)) continue;
            std::getline(iss, name);
            const std::size_t nameStart = name.find_first_not_of(' ');
            Probe({sip, p},
                  nameStart == std::string::npos ? sip : name.substr(nameStart),
                  timeoutMs);
        }
        return 0;
    }
    if (ip.empty() || port == 0) {
        std::printf("Использование:\n  vc-netprobe <ip> <port> [--timeout-ms N]\n"
                    "  vc-netprobe --file servers.txt [--timeout-ms N]\n");
        return 2;
    }
    Probe({ip, port}, ip, timeoutMs);
    return 0;
}
