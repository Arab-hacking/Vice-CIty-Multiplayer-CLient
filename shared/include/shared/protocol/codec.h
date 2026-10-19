#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <shared/math/vector3.h>
#include <shared/protocol/packets.h>

// Сериализация тел игровых пакетов (то, что лежит внутри RakNet InternalPacket.payload).
// Все числа — little-endian, строки с длиной-префиксом. Первый байт тела — ID пакета.
// Форматы зафиксированы в docs/NETWORK-PROTOCOL.md; изменение формата = PROTOCOL_VERSION+1.
namespace vc::protocol {

// ConnectionRequest: [0][u32 protocol][u8 nickLen][nick]
struct ConnectionRequestData {
    std::uint32_t protocolVersion = 0;
    std::string nick;
};
[[nodiscard]] std::vector<std::uint8_t> BuildConnectionRequest(const ConnectionRequestData& data);
[[nodiscard]] std::optional<ConnectionRequestData> ParseConnectionRequest(
    const std::vector<std::uint8_t>& payload);

// ConnectionAccepted: [100][u16 playerId][u16 maxPlayers]
struct ConnectionAcceptedData {
    std::uint16_t playerId = 0;
    std::uint16_t maxPlayers = 0;
};
[[nodiscard]] std::vector<std::uint8_t> BuildConnectionAccepted(const ConnectionAcceptedData& data);
[[nodiscard]] std::optional<ConnectionAcceptedData> ParseConnectionAccepted(
    const std::vector<std::uint8_t>& payload);

// ConnectionRejected: [104][u16 len][reason utf8]
[[nodiscard]] std::vector<std::uint8_t> BuildConnectionRejected(const std::string& reason);
[[nodiscard]] std::optional<std::string> ParseConnectionRejected(
    const std::vector<std::uint8_t>& payload);

// ServerMessage: [130][u16 len][text utf8]
[[nodiscard]] std::vector<std::uint8_t> BuildServerMessage(const std::string& text);
[[nodiscard]] std::optional<std::string> ParseServerMessage(
    const std::vector<std::uint8_t>& payload);

// ChatMessage (клиент -> сервер): [20][u16 len][text utf8]
[[nodiscard]] std::vector<std::uint8_t> BuildChatMessage(const std::string& text);
[[nodiscard]] std::optional<std::string> ParseChatMessage(
    const std::vector<std::uint8_t>& payload);

// OnFootSync: [10][f32 x,y,z][u8 health] — позиция и здоровье локального игрока.
struct OnFootSyncData {
    Vector3 position{};
    std::uint8_t health = 100;
};
[[nodiscard]] std::vector<std::uint8_t> BuildOnFootSync(const OnFootSyncData& data);
[[nodiscard]] std::optional<OnFootSyncData> ParseOnFootSync(
    const std::vector<std::uint8_t>& payload);

// PlayerStreamIn: [105][u16 id][u16 skin][u8 nickLen][nick][f32 x,y,z] — кукла игрока.
struct PlayerStreamInData {
    std::uint16_t playerId = 0;
    std::uint16_t skin = 7;
    std::string nick;
    Vector3 position{};
};
[[nodiscard]] std::vector<std::uint8_t> BuildPlayerStreamIn(const PlayerStreamInData& data);
[[nodiscard]] std::optional<PlayerStreamInData> ParsePlayerStreamIn(
    const std::vector<std::uint8_t>& payload);

// PlayerStreamOut: [106][u16 id] — убрать куклу.
[[nodiscard]] std::vector<std::uint8_t> BuildPlayerStreamOut(std::uint16_t playerId);
[[nodiscard]] std::optional<std::uint16_t> ParsePlayerStreamOut(
    const std::vector<std::uint8_t>& payload);

// PlayerSyncUpdate: [107][u16 id][f32 x,y,z][u8 health] — позиция и HP чужого игрока.
struct PlayerSyncUpdateData {
    std::uint16_t playerId = 0;
    Vector3 position{};
    std::uint8_t health = 100;
};
[[nodiscard]] std::vector<std::uint8_t> BuildPlayerSyncUpdate(const PlayerSyncUpdateData& data);
[[nodiscard]] std::optional<PlayerSyncUpdateData> ParsePlayerSyncUpdate(
    const std::vector<std::uint8_t>& payload);

// VehicleStreamIn: [110][u16 vehicleId][u16 model][f32 x,y,z][f32 heading] —
// серверная машина появилась в радиусе стриминга.
struct VehicleStreamInData {
    std::uint16_t vehicleId = 0;
    std::uint16_t model = 0;
    Vector3 position{};
    float heading = 0.0f;
};
[[nodiscard]] std::vector<std::uint8_t> BuildVehicleStreamIn(const VehicleStreamInData& data);
[[nodiscard]] std::optional<VehicleStreamInData> ParseVehicleStreamIn(
    const std::vector<std::uint8_t>& payload);

// VehicleStreamOut: [111][u16 vehicleId] — убрать машину.
[[nodiscard]] std::vector<std::uint8_t> BuildVehicleStreamOut(std::uint16_t vehicleId);
[[nodiscard]] std::optional<std::uint16_t> ParseVehicleStreamOut(
    const std::vector<std::uint8_t>& payload);

// Teleport: [132][f32 x,y,z][f32 heading] — сервер перемещает локального игрока
// (спавн-пойнт при входе, /tp, /kill и респавн после смерти).
struct TeleportData {
    Vector3 position{};
    float heading = 0.0f;
};
[[nodiscard]] std::vector<std::uint8_t> BuildTeleport(const TeleportData& data);
[[nodiscard]] std::optional<TeleportData> ParseTeleport(
    const std::vector<std::uint8_t>& payload);

} // namespace vc::protocol
