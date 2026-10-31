#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <shared/net/udp_socket.h>

// Офлайн-сообщения RakNet (до установления соединения) — ими сервер-браузеры
// и мониторинг проверяют доступность сервера. RakNet magic GUID/форматы —
// публичные константы оригинального RakNet.
//
// Плюс классический SA-MP query-протокол ("SAMP....i") — публичный протокол,
// которым пользуются все сервер-браузеры SA-MP; наш сервер на него отвечает,
// чтобы vc-netprobe и стандартные инструменты видели его одинаково.
namespace vc::net {

// OFFLINE_MESSAGE_DATA_ID из RakNet.
inline constexpr std::array<std::uint8_t, 16> kOfflineMagic = {
    0x00, 0xFF, 0xFF, 0x00, 0xFE, 0xFE, 0xFE, 0xFE,
    0xFD, 0xFD, 0xFD, 0xFD, 0x12, 0x34, 0x56, 0x78};

inline constexpr std::uint8_t kIdUnconnectedPing = 0x01;
inline constexpr std::uint8_t kIdUnconnectedPong = 0x1C;

// Ping:  [0x01][time u64 LE][magic 16][client guid u64 LE]
[[nodiscard]] std::vector<std::uint8_t> BuildUnconnectedPing(std::uint64_t timeMs,
                                                             std::uint64_t clientGuid);
// Pong:  [0x1C][time u64 LE][server guid u64 LE][magic 16][len u16 BE][motd utf8]
[[nodiscard]] std::vector<std::uint8_t> BuildUnconnectedPong(std::uint64_t timeMs,
                                                             std::uint64_t serverGuid,
                                                             std::string_view motd);

struct UnconnectedPong {
    std::uint64_t timeMs = 0;
    std::uint64_t serverGuid = 0;
    std::string motd;
};

[[nodiscard]] bool IsUnconnectedPing(std::span<const std::uint8_t> data);
[[nodiscard]] std::optional<std::uint64_t> ParsePingTime(std::span<const std::uint8_t> data);
[[nodiscard]] std::optional<UnconnectedPong> ParseUnconnectedPong(
    std::span<const std::uint8_t> data);

// --- SA-MP query ("SAMP" + ip[4] + port u16 LE + opcode) ---
inline constexpr char kSampQueryInfo = 'i'; // базовая информация о сервере

[[nodiscard]] std::vector<std::uint8_t> BuildSampQuery(const Endpoint& server, char opcode);
[[nodiscard]] bool IsSampQuery(std::span<const std::uint8_t> data, char opcode);

struct SampServerInfo {
    bool passworded = false;
    std::uint16_t players = 0;
    std::uint16_t maxPlayers = 0;
    std::string hostname;
    std::string gamemode;
    std::string language;
};

// Ответ сервера на 'i': SAMP-заголовок повторяется, далее поля query-протокола SA-MP.
[[nodiscard]] std::vector<std::uint8_t> BuildSampInfoReply(const Endpoint& server,
                                                           const SampServerInfo& info);
[[nodiscard]] std::optional<SampServerInfo> ParseSampInfoReply(
    std::span<const std::uint8_t> data);

} // namespace vc::net
