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

private:
    VehiclePool() = default;
};

} // namespace vc::pools
