#include <shared/net/offline.h>

#include <cstdio>
#include <cstring>

namespace vc::net {
namespace {

void AppendU64Le(std::vector<std::uint8_t>& out, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}
void AppendU32Le(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}
void AppendU16Le(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
}
std::uint64_t ReadU64Le(const std::uint8_t* p) {
    std::uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}
std::uint32_t ReadU32Le(const std::uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
std::uint16_t ReadU16Le(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

bool HasMagicAt(std::span<const std::uint8_t> data, std::size_t offset) {
    return data.size() >= offset + kOfflineMagic.size() &&
           std::memcmp(data.data() + offset, kOfflineMagic.data(), kOfflineMagic.size()) == 0;
}

// Заголовок SA-MP query: "SAMP" + IPv4 (4 октета) + порт LE + opcode = 11 байт.
std::vector<std::uint8_t> SampHeader(const Endpoint& server, char opcode) {
    std::vector<std::uint8_t> out{'S', 'A', 'M', 'P'};
    unsigned a = 127, b = 0, c = 0, d = 1;
    std::sscanf(server.ip.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d);
    out.push_back(static_cast<std::uint8_t>(a));
    out.push_back(static_cast<std::uint8_t>(b));
    out.push_back(static_cast<std::uint8_t>(c));
    out.push_back(static_cast<std::uint8_t>(d));
    AppendU16Le(out, server.port);
    out.push_back(static_cast<std::uint8_t>(opcode));
    return out;
}

void AppendSampString(std::vector<std::uint8_t>& out, std::string_view s) {
    AppendU32Le(out, static_cast<std::uint32_t>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}

std::optional<std::string> ReadSampString(std::span<const std::uint8_t> data, std::size_t& pos) {
    if (pos + 4 > data.size()) return std::nullopt;
    const std::uint32_t len = ReadU32Le(data.data() + pos);
    pos += 4;
    if (pos + len > data.size()) return std::nullopt;
    std::string s(reinterpret_cast<const char*>(data.data() + pos), len);
    pos += len;
    return s;
}

} // namespace

std::vector<std::uint8_t> BuildUnconnectedPing(std::uint64_t timeMs, std::uint64_t clientGuid) {
    std::vector<std::uint8_t> out;
    out.push_back(kIdUnconnectedPing);
    AppendU64Le(out, timeMs);
    out.insert(out.end(), kOfflineMagic.begin(), kOfflineMagic.end());
    AppendU64Le(out, clientGuid);
    return out;
}

std::vector<std::uint8_t> BuildUnconnectedPong(std::uint64_t timeMs, std::uint64_t serverGuid,
                                               std::string_view motd) {
    std::vector<std::uint8_t> out;
    out.push_back(kIdUnconnectedPong);
    AppendU64Le(out, timeMs);
    AppendU64Le(out, serverGuid);
    out.insert(out.end(), kOfflineMagic.begin(), kOfflineMagic.end());
    // Длина строки в RakNet pong — big-endian u16.
    out.push_back(static_cast<std::uint8_t>((motd.size() >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(motd.size() & 0xFF));
    out.insert(out.end(), motd.begin(), motd.end());
    return out;
}

bool IsUnconnectedPing(std::span<const std::uint8_t> data) {
    return data.size() >= 1 + 8 + 16 && data[0] == kIdUnconnectedPing && HasMagicAt(data, 9);
}

std::optional<std::uint64_t> ParsePingTime(std::span<const std::uint8_t> data) {
    if (!IsUnconnectedPing(data)) return std::nullopt;
    return ReadU64Le(data.data() + 1);
}

std::optional<UnconnectedPong> ParseUnconnectedPong(std::span<const std::uint8_t> data) {
    if (data.size() < 1 + 8 + 8 + 16 + 2 || data[0] != kIdUnconnectedPong ||
        !HasMagicAt(data, 17))
        return std::nullopt;
    UnconnectedPong pong;
    pong.timeMs = ReadU64Le(data.data() + 1);
    pong.serverGuid = ReadU64Le(data.data() + 9);
    const std::uint16_t len =
        static_cast<std::uint16_t>((data[33] << 8) | data[34]);
    if (data.size() < 35u + len) return std::nullopt;
    pong.motd.assign(reinterpret_cast<const char*>(data.data() + 35), len);
    return pong;
}

std::vector<std::uint8_t> BuildSampQuery(const Endpoint& server, char opcode) {
    return SampHeader(server, opcode);
}

bool IsSampQuery(std::span<const std::uint8_t> data, char opcode) {
    return data.size() >= 11 && data[0] == 'S' && data[1] == 'A' && data[2] == 'M' &&
           data[3] == 'P' && data[10] == static_cast<std::uint8_t>(opcode);
}

std::vector<std::uint8_t> BuildSampInfoReply(const Endpoint& server, const SampServerInfo& info) {
    std::vector<std::uint8_t> out = SampHeader(server, kSampQueryInfo);
    out.push_back(info.passworded ? 1 : 0);
    AppendU16Le(out, info.players);
    AppendU16Le(out, info.maxPlayers);
    AppendSampString(out, info.hostname);
    AppendSampString(out, info.gamemode);
    AppendSampString(out, info.language);
    return out;
}

std::optional<SampServerInfo> ParseSampInfoReply(std::span<const std::uint8_t> data) {
    if (!IsSampQuery(data, kSampQueryInfo) || data.size() < 11 + 1 + 2 + 2)
        return std::nullopt;
    SampServerInfo info;
    std::size_t pos = 11;
    info.passworded = data[pos++] != 0;
    info.players = ReadU16Le(data.data() + pos);
    pos += 2;
    info.maxPlayers = ReadU16Le(data.data() + pos);
    pos += 2;
    auto host = ReadSampString(data, pos);
    auto mode = ReadSampString(data, pos);
    auto lang = ReadSampString(data, pos);
    if (!host || !mode || !lang) return std::nullopt;
    info.hostname = *host;
    info.gamemode = *mode;
    info.language = *lang;
    return info;
}

} // namespace vc::net
