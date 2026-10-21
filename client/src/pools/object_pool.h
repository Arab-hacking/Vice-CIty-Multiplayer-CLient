#pragma once

#include <cstdio>

// Пул серверных объектов и пикапов (Этап 5, шаг 4): по данным NetClient
// создаёт в игре CObject на каждый серверный объект/пикап в стриме
// (CPool<CObject>::New + CObject(model, true) + матрица позы + CWorld::Add,
// всё под SEH). Поза объектов переприменяется каждый кадр (локальная физика
// не утаскивает), пикапы вращаются/парят над землёй и подбираются по близости
// локального игрока (рапорт PlayerPickupPickup на сервер). Игровой поток
// (Tick из хука CGame::Process).
namespace vc::pools {

class ObjectPool {
public:
    static ObjectPool& Instance();

    void Init(FILE* log);
    void Tick();
};

} // namespace vc::pools
