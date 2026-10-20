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

// OnFootSync: [10][f32 x,y,z][u8 health][u8 weapon] — позиция, здоровье и
// оружие в руках локального игрока (weapon — протокол v9, eWeaponType).
struct OnFootSyncData {
    Vector3 position{};
    std::uint8_t health = 100;
    std::uint8_t weapon = 0; // протокол v9
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

// PlayerSyncUpdate: [107][u16 id][f32 x,y,z][u8 health][u8 weapon] — позиция,
// HP и оружие в руках чужого игрока (weapon — протокол v9).
struct PlayerSyncUpdateData {
    std::uint16_t playerId = 0;
    Vector3 position{};
    std::uint8_t health = 100;
    std::uint8_t weapon = 0; // протокол v9
};
[[nodiscard]] std::vector<std::uint8_t> BuildPlayerSyncUpdate(const PlayerSyncUpdateData& data);
[[nodiscard]] std::optional<PlayerSyncUpdateData> ParsePlayerSyncUpdate(
    const std::vector<std::uint8_t>& payload);

// GiveWeapon: [114][u8 weapon][u16 ammo] — сервер выдаёт оружие (eWeaponType).
struct GiveWeaponData {
    std::uint8_t weapon = 0;
    std::uint16_t ammo = 0;
};
[[nodiscard]] std::vector<std::uint8_t> BuildGiveWeapon(const GiveWeaponData& data);
[[nodiscard]] std::optional<GiveWeaponData> ParseGiveWeapon(
    const std::vector<std::uint8_t>& payload);

// PlayerDamage: [35][u16 victimId][f32 amount][u8 weapon] — стрелок докладывает
// серверу о попадании по кукле (детект по падению HP педа-куклы).
struct PlayerDamageData {
    std::uint16_t victimId = 0;
    float amount = 0.0f;
    std::uint8_t weapon = 0;
};
[[nodiscard]] std::vector<std::uint8_t> BuildPlayerDamage(const PlayerDamageData& data);
[[nodiscard]] std::optional<PlayerDamageData> ParsePlayerDamage(
    const std::vector<std::uint8_t>& payload);

// TakeDamage: [115][u16 attackerId][f32 amount][u8 weapon] — сервер говорит
// жертве убавить здоровье своего педа.
struct TakeDamageData {
    std::uint16_t attackerId = 0;
    float amount = 0.0f;
    std::uint8_t weapon = 0;
};
[[nodiscard]] std::vector<std::uint8_t> BuildTakeDamage(const TakeDamageData& data);
[[nodiscard]] std::optional<TakeDamageData> ParseTakeDamage(
    const std::vector<std::uint8_t>& payload);

// VehicleStreamIn: [110][u16 vehicleId][u16 model][f32 x,y,z][f32 heading] —
// серверная машина появилась в радиусе стриминга.
struct VehicleStreamInData {
    std::uint16_t vehicleId = 0;
    std::uint16_t model = 0;
    Vector3 position{};
    float heading = 0.0f;
    std::uint16_t health = 1000; // протокол v8: 1000 — целая, 0 — взорвана
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

// Поза машины: InVehicleSync [11] клиент->сервер и VehicleSyncUpdate [112]
// сервер->клиенты. Формат общий: [id][u16 vehicleId][f32 x,y,z][f32 qx,qy,qz,qw].
struct VehiclePoseData {
    std::uint16_t vehicleId = 0;
    Vector3 position{};
    float quatX = 0.0f;
    float quatY = 0.0f;
    float quatZ = 0.0f;
    float quatW = 1.0f;
    std::uint16_t health = 1000; // протокол v8
};
[[nodiscard]] std::vector<std::uint8_t> BuildInVehicleSync(const VehiclePoseData& data);
[[nodiscard]] std::optional<VehiclePoseData> ParseInVehicleSync(
    const std::vector<std::uint8_t>& payload);
[[nodiscard]] std::vector<std::uint8_t> BuildVehicleSyncUpdate(const VehiclePoseData& data);
[[nodiscard]] std::optional<VehiclePoseData> ParseVehicleSyncUpdate(
    const std::vector<std::uint8_t>& payload);

// EnterVehicle: [32][u16 vehicleId][u8 seat] (0 — водитель). ExitVehicle: [33].
struct EnterVehicleData {
    std::uint16_t vehicleId = 0;
    std::uint8_t seat = 0;
};
[[nodiscard]] std::vector<std::uint8_t> BuildEnterVehicle(const EnterVehicleData& data);
[[nodiscard]] std::optional<EnterVehicleData> ParseEnterVehicle(
    const std::vector<std::uint8_t>& payload);

// VehicleOccupant: [113][u16 vehicleId][u16 playerId][u8 seat] — кто сидит в
// машине (seat 0 — водитель, 1..3 — пассажиры, 255 — игрок вышел).
struct VehicleOccupantData {
    std::uint16_t vehicleId = 0;
    std::uint16_t playerId = 0;
    std::uint8_t seat = 0;
};
inline constexpr std::uint8_t kSeatVacated = 255;
[[nodiscard]] std::vector<std::uint8_t> BuildVehicleOccupant(const VehicleOccupantData& data);
[[nodiscard]] std::optional<VehicleOccupantData> ParseVehicleOccupant(
    const std::vector<std::uint8_t>& payload);

} // namespace vc::protocol
