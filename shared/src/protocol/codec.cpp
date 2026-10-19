#include <shared/protocol/codec.h>

#include <cstring>

namespace vc::protocol {
namespace {

struct Writer {
    std::vector<std::uint8_t> out;
    void U8(std::uint8_t v) { out.push_back(v); }
    void U16(std::uint16_t v) {
        out.push_back(static_cast<std::uint8_t>(v & 0xFF));
        out.push_back(static_cast<std::uint8_t>(v >> 8));
    }
    void U32(std::uint32_t v) {
        for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
    void F32(float v) {
        std::uint32_t raw;
        std::memcpy(&raw, &v, 4);
        U32(raw);
    }
    void Str8(const std::string& s) {
        U8(static_cast<std::uint8_t>(s.size()));
        out.insert(out.end(), s.begin(), s.end());
    }
    void Str16(const std::string& s) {
        U16(static_cast<std::uint16_t>(s.size()));
        out.insert(out.end(), s.begin(), s.end());
    }
};

struct Reader {
    const std::vector<std::uint8_t>& in;
    std::size_t pos = 0;
    bool ok = true;
    std::uint8_t U8() {
        if (pos + 1 > in.size()) { ok = false; return 0; }
        return in[pos++];
    }
    std::uint16_t U16() {
        if (pos + 2 > in.size()) { ok = false; return 0; }
        const auto v = static_cast<std::uint16_t>(in[pos] | (in[pos + 1] << 8));
        pos += 2;
        return v;
    }
    std::uint32_t U32() {
        if (pos + 4 > in.size()) { ok = false; return 0; }
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(in[pos + i]) << (8 * i);
        pos += 4;
        return v;
    }
    float F32() {
        const std::uint32_t raw = U32();
        float v = 0;
        std::memcpy(&v, &raw, 4);
        return v;
    }
    std::string Str(std::size_t len) {
        if (pos + len > in.size()) { ok = false; return {}; }
        std::string s(reinterpret_cast<const char*>(in.data() + pos), len);
        pos += len;
        return s;
    }
    std::string Str8() { return Str(U8()); }
    std::string Str16() { return Str(U16()); }
    bool ExpectId(std::uint8_t id) { return U8() == id; }
};

std::vector<std::uint8_t> BuildTextPacket(std::uint8_t id, const std::string& text) {
    Writer w;
    w.U8(id);
    w.Str16(text);
    return w.out;
}

std::optional<std::string> ParseTextPacket(const std::vector<std::uint8_t>& payload,
                                           std::uint8_t id) {
    Reader r{payload};
    if (!r.ExpectId(id)) return std::nullopt;
    std::string s = r.Str16();
    if (!r.ok) return std::nullopt;
    return s;
}

} // namespace

std::vector<std::uint8_t> BuildConnectionRequest(const ConnectionRequestData& data) {
    Writer w;
    w.U8(static_cast<std::uint8_t>(ClientPacket::ConnectionRequest));
    w.U32(data.protocolVersion);
    w.Str8(data.nick);
    return w.out;
}

std::optional<ConnectionRequestData> ParseConnectionRequest(
    const std::vector<std::uint8_t>& payload) {
    Reader r{payload};
    if (!r.ExpectId(static_cast<std::uint8_t>(ClientPacket::ConnectionRequest)))
        return std::nullopt;
    ConnectionRequestData data;
    data.protocolVersion = r.U32();
    data.nick = r.Str8();
    if (!r.ok || data.nick.empty()) return std::nullopt;
    return data;
}

std::vector<std::uint8_t> BuildConnectionAccepted(const ConnectionAcceptedData& data) {
    Writer w;
    w.U8(static_cast<std::uint8_t>(ServerRpc::ConnectionAccepted));
    w.U16(data.playerId);
    w.U16(data.maxPlayers);
    return w.out;
}

std::optional<ConnectionAcceptedData> ParseConnectionAccepted(
    const std::vector<std::uint8_t>& payload) {
    Reader r{payload};
    if (!r.ExpectId(static_cast<std::uint8_t>(ServerRpc::ConnectionAccepted)))
        return std::nullopt;
    ConnectionAcceptedData data;
    data.playerId = r.U16();
    data.maxPlayers = r.U16();
    if (!r.ok) return std::nullopt;
    return data;
}

std::vector<std::uint8_t> BuildConnectionRejected(const std::string& reason) {
    return BuildTextPacket(static_cast<std::uint8_t>(ServerRpc::ConnectionRejected), reason);
}

std::optional<std::string> ParseConnectionRejected(const std::vector<std::uint8_t>& payload) {
    return ParseTextPacket(payload, static_cast<std::uint8_t>(ServerRpc::ConnectionRejected));
}

std::vector<std::uint8_t> BuildServerMessage(const std::string& text) {
    return BuildTextPacket(static_cast<std::uint8_t>(ServerRpc::ServerMessage), text);
}

std::optional<std::string> ParseServerMessage(const std::vector<std::uint8_t>& payload) {
    return ParseTextPacket(payload, static_cast<std::uint8_t>(ServerRpc::ServerMessage));
}

std::vector<std::uint8_t> BuildChatMessage(const std::string& text) {
    return BuildTextPacket(static_cast<std::uint8_t>(ClientPacket::ChatMessage), text);
}

std::optional<std::string> ParseChatMessage(const std::vector<std::uint8_t>& payload) {
    return ParseTextPacket(payload, static_cast<std::uint8_t>(ClientPacket::ChatMessage));
}

std::vector<std::uint8_t> BuildOnFootSync(const OnFootSyncData& data) {
    Writer w;
    w.U8(static_cast<std::uint8_t>(ClientPacket::OnFootSync));
    w.F32(data.position.x);
    w.F32(data.position.y);
    w.F32(data.position.z);
    w.U8(data.health);
    return w.out;
}

std::optional<OnFootSyncData> ParseOnFootSync(const std::vector<std::uint8_t>& payload) {
    Reader r{payload};
    if (!r.ExpectId(static_cast<std::uint8_t>(ClientPacket::OnFootSync))) return std::nullopt;
    OnFootSyncData data;
    data.position = Vector3{r.F32(), r.F32(), r.F32()};
    data.health = r.U8();
    if (!r.ok) return std::nullopt;
    return data;
}

std::vector<std::uint8_t> BuildPlayerStreamIn(const PlayerStreamInData& data) {
    Writer w;
    w.U8(static_cast<std::uint8_t>(ServerRpc::PlayerStreamIn));
    w.U16(data.playerId);
    w.U16(data.skin);
    w.Str8(data.nick);
    w.F32(data.position.x);
    w.F32(data.position.y);
    w.F32(data.position.z);
    return w.out;
}

std::optional<PlayerStreamInData> ParsePlayerStreamIn(const std::vector<std::uint8_t>& payload) {
    Reader r{payload};
    if (!r.ExpectId(static_cast<std::uint8_t>(ServerRpc::PlayerStreamIn))) return std::nullopt;
    PlayerStreamInData data;
    data.playerId = r.U16();
    data.skin = r.U16();
    data.nick = r.Str8();
    data.position = Vector3{r.F32(), r.F32(), r.F32()};
    if (!r.ok) return std::nullopt;
    return data;
}

std::vector<std::uint8_t> BuildPlayerStreamOut(std::uint16_t playerId) {
    Writer w;
    w.U8(static_cast<std::uint8_t>(ServerRpc::PlayerStreamOut));
    w.U16(playerId);
    return w.out;
}

std::optional<std::uint16_t> ParsePlayerStreamOut(const std::vector<std::uint8_t>& payload) {
    Reader r{payload};
    if (!r.ExpectId(static_cast<std::uint8_t>(ServerRpc::PlayerStreamOut))) return std::nullopt;
    const std::uint16_t id = r.U16();
    if (!r.ok) return std::nullopt;
    return id;
}

std::vector<std::uint8_t> BuildVehicleStreamIn(const VehicleStreamInData& data) {
    Writer w;
    w.U8(static_cast<std::uint8_t>(ServerRpc::CreateVehicle));
    w.U16(data.vehicleId);
    w.U16(data.model);
    w.F32(data.position.x);
    w.F32(data.position.y);
    w.F32(data.position.z);
    w.F32(data.heading);
    return w.out;
}

std::optional<VehicleStreamInData> ParseVehicleStreamIn(
    const std::vector<std::uint8_t>& payload) {
    Reader r{payload};
    if (!r.ExpectId(static_cast<std::uint8_t>(ServerRpc::CreateVehicle))) return std::nullopt;
    VehicleStreamInData data;
    data.vehicleId = r.U16();
    data.model = r.U16();
    data.position = Vector3{r.F32(), r.F32(), r.F32()};
    data.heading = r.F32();
    if (!r.ok) return std::nullopt;
    return data;
}

std::vector<std::uint8_t> BuildVehicleStreamOut(std::uint16_t vehicleId) {
    Writer w;
    w.U8(static_cast<std::uint8_t>(ServerRpc::DestroyVehicle));
    w.U16(vehicleId);
    return w.out;
}

std::optional<std::uint16_t> ParseVehicleStreamOut(
    const std::vector<std::uint8_t>& payload) {
    Reader r{payload};
    if (!r.ExpectId(static_cast<std::uint8_t>(ServerRpc::DestroyVehicle))) return std::nullopt;
    const std::uint16_t id = r.U16();
    if (!r.ok) return std::nullopt;
    return id;
}

std::vector<std::uint8_t> BuildPlayerSyncUpdate(const PlayerSyncUpdateData& data) {
    Writer w;
    w.U8(static_cast<std::uint8_t>(ServerRpc::PlayerSyncUpdate));
    w.U16(data.playerId);
    w.F32(data.position.x);
    w.F32(data.position.y);
    w.F32(data.position.z);
    w.U8(data.health);
    return w.out;
}

std::optional<PlayerSyncUpdateData> ParsePlayerSyncUpdate(
    const std::vector<std::uint8_t>& payload) {
    Reader r{payload};
    if (!r.ExpectId(static_cast<std::uint8_t>(ServerRpc::PlayerSyncUpdate))) return std::nullopt;
    PlayerSyncUpdateData data;
    data.playerId = r.U16();
    data.position = Vector3{r.F32(), r.F32(), r.F32()};
    data.health = r.U8();
    if (!r.ok) return std::nullopt;
    return data;
}

std::vector<std::uint8_t> BuildTeleport(const TeleportData& data) {
    Writer w;
    w.U8(static_cast<std::uint8_t>(ServerRpc::Teleport));
    w.F32(data.position.x);
    w.F32(data.position.y);
    w.F32(data.position.z);
    w.F32(data.heading);
    return w.out;
}

std::optional<TeleportData> ParseTeleport(const std::vector<std::uint8_t>& payload) {
    Reader r{payload};
    if (!r.ExpectId(static_cast<std::uint8_t>(ServerRpc::Teleport))) return std::nullopt;
    TeleportData data;
    data.position = Vector3{r.F32(), r.F32(), r.F32()};
    data.heading = r.F32();
    if (!r.ok) return std::nullopt;
    return data;
}

} // namespace vc::protocol
