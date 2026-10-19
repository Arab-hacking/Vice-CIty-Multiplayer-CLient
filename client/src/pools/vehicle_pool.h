#pragma once

#include <cstdio>

// Пул серверных машин (Этап 4, шаг 1): по данным NetClient создаёт в игре
// CAutomobile на каждую серверную машину в стриме (CPool<CVehicle>::New +
// конструктор + матрица позы + CWorld::Add, всё под SEH). Машины пока
// статичны; синк водителя — следующий шаг. Игровой поток (Tick из хука
// CGame::Process).
namespace vc::pools {

class VehiclePool {
public:
    static VehiclePool& Instance();

    void Init(FILE* log);
    void Tick();

    // Серверный id по указателю на игровую машину (0 — не серверная).
    // Нужен хуку кадра, чтобы понять, что локальный игрок за рулём нашей машины.
    [[nodiscard]] std::uint16_t ServerIdOf(const void* vehicle) const;
    // Игровая машина по серверному id (nullptr — ещё не заспавнена локально).
    // PlayerPool сажает в неё кукол сидящих игроков.
    [[nodiscard]] void* GameVehicleOf(std::uint16_t serverId) const;

private:
    VehiclePool() = default;
};

} // namespace vc::pools
