#pragma once

#include <cstdint>

// ID пакетов протокола. Полное описание полей — docs/NETWORK-PROTOCOL.md.
// ВАЖНО: значения уже выпущенных ID не меняем, новые добавляем в свободные слоты.
namespace vc::protocol {

// Клиент -> Сервер
enum class ClientPacket : std::uint8_t {
    ConnectionRequest = 0,
    OnFootSync        = 10,
    InVehicleSync     = 11,
    PassengerSync     = 12,
    ChatMessage       = 20,
    Command           = 21,
    KeepAlive         = 22,
    BulletSync        = 30,
    Death             = 31,
    EnterVehicle      = 32,
    ExitVehicle       = 33,
    Disconnect        = 34,
};

// Сервер -> Клиент (RPC)
enum class ServerRpc : std::uint8_t {
    ConnectionAccepted = 100,
    PlayerJoin         = 101,
    PlayerQuit         = 102,
    PlayerSpawn        = 103,
    ConnectionRejected = 104,
    PlayerStreamIn     = 105,
    PlayerStreamOut    = 106,
    PlayerSyncUpdate   = 107,
    CreateVehicle      = 110,
    DestroyVehicle     = 111,
    VehicleSyncUpdate  = 112,
    CreateObject       = 120,
    DestroyObject      = 121,
    ServerMessage      = 130,
    SetTimeWeather     = 131,
    Teleport           = 132,
};

} // namespace vc::protocol
