#pragma once

#include <cstdio>

// Пул сетевых педов-«кукол» (Этап 3): по данным NetClient создаёт в игре
// CCivilianPed на каждого удалённого игрока и плавно двигает их
// (Teleport каждый кадр к интерполированной позиции). ИИ кукол отключён
// хуком CPed::ProcessControl. Работает только в игровом потоке (Tick из
// хука CGame::Process).
namespace vc::pools {

class PlayerPool {
public:
    static PlayerPool& Instance();

    void Init(FILE* log);
    void Tick();

    [[nodiscard]] bool IsPuppet(const void* ped) const;

private:
    PlayerPool() = default;
    struct Impl;
};

} // namespace vc::pools
