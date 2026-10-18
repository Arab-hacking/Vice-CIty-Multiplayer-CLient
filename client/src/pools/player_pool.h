#pragma once

#include <cstdio>
#include <string>
#include <vector>

#include <shared/math/vector3.h>

// Пул сетевых педов-«кукол»: по данным NetClient создаёт в игре CCivilianPed
// на каждого удалённого игрока (скин из StreamIn), плавно двигает и поворачивает
// их, ставит анимацию бега. ИИ кукол отключён хуком CPed::ProcessControl.
// Работает только в игровом потоке (Tick из хука CGame::Process).
namespace vc::pools {

struct PuppetRenderInfo {
    std::string nick;
    vc::Vector3 position{}; // текущая (интерполированная) позиция куклы
};

class PlayerPool {
public:
    static PlayerPool& Instance();

    void Init(FILE* log);
    void Tick();

    [[nodiscard]] bool IsPuppet(const void* ped) const;
    // Для никнеймов в оверлее. Вызывать из того же (игрового) потока.
    [[nodiscard]] std::vector<PuppetRenderInfo> RenderInfos() const;

private:
    PlayerPool() = default;
};

} // namespace vc::pools
