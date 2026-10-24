// Снятие границ мира и лимитов пулов (0.6.23). См. world_unlock.h.
// Раскладка CPool и размеры слотов - из дизасма CPools::Initialise целевого
// билда (CUSTOM_MODELS: пулы растут стабильными блоками ExtraBlock, адреса
// существующих сущностей при росте не меняются). Всё под SEH.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../core/seh_guard.h"
#include "../network/net_client.h"
#include "revc.h"
#include "world_unlock.h"

namespace vc::game {
namespace {

using WorldAddRemoveFn = void (*)(void*);
using UpdateRwFrameFn = void (*)(void*);
using GetModelInfoFn = void* (*)(const char*, int*);

// CPool (templates.h целевого форка, проверено дизасмом конструкторов):
// m_entries +0x00, m_flags +0x08, m_name +0x10, m_size +0x18,
// m_initialSize +0x1C, m_growBy +0x20, m_allocPtr +0x24, m_used +0x28,
// m_storedSize +0x2C, m_extraHead +0x30, m_extraTail +0x38 (всего 0x40).
constexpr std::uintptr_t kPoolEntries = 0x00;
constexpr std::uintptr_t kPoolFlags = 0x08;
constexpr std::uintptr_t kPoolInitialSize = 0x1C;
constexpr std::uintptr_t kPoolGrowBy = 0x20;
constexpr std::uintptr_t kPoolExtraHead = 0x30;
// ExtraBlock: entries +0x00, flags +0x08, start +0x10, size +0x14, next +0x18.
constexpr std::uintptr_t kBlockSize = 0x14;
constexpr std::uintptr_t kBlockNext = 0x18;
constexpr std::uint8_t kPoolFlagFree = 0x80; // POOLFLAG_ISFREE

constexpr std::uintptr_t kEntityModelOffset = 0x6C; // int16 m_modelIndex
constexpr std::uintptr_t kEntityPosXOffset = 0x38;  // матрица +0x08 -> pos
constexpr std::uintptr_t kEntityPosYOffset = 0x3C;
constexpr std::uintptr_t kEntityPosZOffset = 0x40;
// Биты флагов CEntity (dword +0x60): m_type 0-2, m_status 3-7, дальше flagsA/B.
constexpr std::uint32_t kFlagUsesCollision = 1u << 8;  // flagsA bUsesCollision
constexpr std::uint32_t kFlagIsVisible = 1u << 18;     // flagsB bIsVisible

constexpr std::size_t kBuildingStride = 120; // sizeof слота CBuilding в пуле
constexpr std::size_t kDummyStride = 136;    // sizeof слота CDummy в пуле
constexpr float kSinkDepth = 80.0f;          // на сколько топим баррикады
constexpr std::uint64_t kStreamingMemory = 1ull << 30; // 1 GiB

// Сюжетные перекрытия из main.scm (таблица имён моделей SCM): баррикады
// мостов между островами и закрытые ворота районов. IDE id резолвятся по
// именам в рантайме (CModelInfo::GetModelInfo) - раскладка IDE не важна.
constexpr const char* kBarrierModels[] = {
    "NT_ROADBLOCKCI", "NT_ROADBLOCKGF", "WSH_ROADBLOCK",
    "COMGATE1CLOSED", "COMGATE2CLOSED", "GOLF_GATESCLOSED",
    "CI_GATESCLOSED", "CI_BACKGATECLOSE", "CI_JETYGATESCLOSED",
};
constexpr int kBarrierModelCount =
    static_cast<int>(sizeof(kBarrierModels) / sizeof(kBarrierModels[0]));

struct PoolBoost {
    std::uintptr_t ptrRva;
    int stock;
    int boosted;
    const char* name;
};
// Стоковые growBy целевого билда (дизасм CPools::Initialise). Пишем только
// если значение ещё стоковое: после рестарта игры пулы пересоздаются и
// подъём применится заново, чужие значения не трогаем.
constexpr PoolBoost kBoosts[] = {
    {revc::kCPoolsPedPool, 64, 1024, "Peds"},
    {revc::kCPoolsVehiclePool, 64, 1024, "Vehicles"},
    {revc::kCPoolsObjectPool, 256, 4096, "Objects"},
    {revc::kCPoolsEntryInfoNodePool, 2048, 16384, "EntryInfoNodes"},
    {revc::kCPoolsDummyPool, 512, 4096, "Dummys"},
};

FILE* g_log = nullptr;
long long g_tick = 0;
int g_barrierIds[kBarrierModelCount]; // 0 - не разрешено, -1 - нет модели
bool g_barriersAnnounced = false;

void Log(const char* text) {
    OutputDebugStringA(text);
    if (g_log) {
        std::fputs(text, g_log);
        std::fflush(g_log);
    }
}

// Поднять лимит памяти стриминга (сток этой сборки - 65 MiB): модели машин
// и педов перестанут выгружаться под давлением памяти. True - подняли сейчас.
bool RaiseStreamingMemoryRaw(std::uint64_t* oldOut) {
    VC_TRY {
        auto* avail = Resolve<std::uint64_t*>(revc::kCStreamingMemoryAvailable);
        *oldOut = *avail;
        if (*avail >= kStreamingMemory) return false;
        *avail = kStreamingMemory;
        return true;
    } VC_EXCEPT {
        return false;
    }
}

// 0 - пула ещё нет, 1 - уже поднят/не сток, 2 - поднят прямо сейчас.
int BoostPoolRaw(std::uintptr_t ptrRva, int stock, int boosted) {
    VC_TRY {
        void* pool = *Resolve<void**>(ptrRva);
        if (!pool) return 0;
        auto* growBy = reinterpret_cast<int*>(static_cast<char*>(pool) + kPoolGrowBy);
        if (*growBy != stock) return 1;
        *growBy = boosted;
        return 2;
    } VC_EXCEPT {
        return 1;
    }
}

// IDE id модели по имени; -1 - модели нет в сборке, 0 - ошибка (повторить).
int ResolveModelRaw(const char* name) {
    VC_TRY {
        auto getInfo = Resolve<GetModelInfoFn>(revc::kCModelInfoGetByName);
        int id = -1;
        void* info = getInfo(name, &id);
        return info ? id : -1;
    } VC_EXCEPT {
        return 0;
    }
}

bool PlayerInWorldRaw() {
    VC_TRY {
        auto findPed = Resolve<FindPlayerPedFn>(revc::kFindPlayerPed);
        return findPed && findPed() != nullptr;
    } VC_EXCEPT {
        return false;
    }
}

struct SinkJob {
    void* ent = nullptr;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    std::int16_t model = 0;
};

// Собрать живые сущности пула с моделями из списка (начальный блок +
// ExtraBlock-цепочка). Уже утопленные (z глубоко под землёй) пропускаем.
int CollectBarriersRaw(void* pool, std::size_t stride, const int* ids, int idCount,
                       SinkJob* out, int cap) {
    int found = 0;
    VC_TRY {
        auto* base = static_cast<char*>(pool);
        char* entries = *reinterpret_cast<char**>(base + kPoolEntries);
        auto* flags = *reinterpret_cast<std::uint8_t**>(base + kPoolFlags);
        int count = *reinterpret_cast<int*>(base + kPoolInitialSize);
        void* extra = *reinterpret_cast<void**>(base + kPoolExtraHead);
        for (;;) {
            if (entries && flags && count > 0) {
                for (int i = 0; i < count && found < cap; ++i) {
                    if (flags[i] & kPoolFlagFree) continue;
                    char* ent = entries + static_cast<std::size_t>(i) * stride;
                    const auto model =
                        *reinterpret_cast<std::int16_t*>(ent + kEntityModelOffset);
                    bool match = false;
                    for (int k = 0; k < idCount; ++k)
                        if (ids[k] > 0 && ids[k] == model) {
                            match = true;
                            break;
                        }
                    if (!match) continue;
                    SinkJob job;
                    job.ent = ent;
                    job.model = model;
                    job.x = *reinterpret_cast<float*>(ent + kEntityPosXOffset);
                    job.y = *reinterpret_cast<float*>(ent + kEntityPosYOffset);
                    job.z = *reinterpret_cast<float*>(ent + kEntityPosZOffset);
                    if (job.z < -20.0f) continue; // уже утоплена
                    out[found++] = job;
                }
            }
            if (!extra || found >= cap) break;
            auto* eb = static_cast<char*>(extra);
            entries = *reinterpret_cast<char**>(eb + kPoolEntries);
            flags = *reinterpret_cast<std::uint8_t**>(eb + kPoolFlags);
            count = *reinterpret_cast<int*>(eb + kBlockSize);
            extra = *reinterpret_cast<void**>(eb + kBlockNext);
        }
    } VC_EXCEPT {
    }
    return found;
}

// Утопить баррикаду: из мира, на 80 м вниз, без коллизии и рендера, обратно
// в мир (секторы/списки больших зданий CWorld::Add/Remove ведут сами).
bool SinkEntityRaw(void* ent) {
    VC_TRY {
        auto* base = static_cast<char*>(ent);
        auto* posZ = reinterpret_cast<float*>(base + kEntityPosZOffset);
        if (*posZ < -20.0f) return false;
        Resolve<WorldAddRemoveFn>(revc::kCWorldRemove)(ent);
        *posZ -= kSinkDepth;
        auto* flags = reinterpret_cast<std::uint32_t*>(base + kEntityFlagsOffset);
        *flags &= ~(kFlagIsVisible | kFlagUsesCollision);
        void* frame = *reinterpret_cast<void**>(base + kFrameMatrixPtrOffset);
        if (frame) std::memcpy(frame, base + kEmbeddedMatrixOffset, 0x40);
        Resolve<UpdateRwFrameFn>(revc::kCEntityUpdateRwFrame)(ent);
        Resolve<WorldAddRemoveFn>(revc::kCWorldAdd)(ent);
        return true;
    } VC_EXCEPT {
        return false;
    }
}

} // namespace

void InitWorldUnlock(FILE* log) { g_log = log; }

void WorldUnlockTick() {
    ++g_tick;

    // Память стриминга проверяем раз в ~0.5 с (игра пишет её при инициализации).
    if (g_tick % 30 == 0) {
        std::uint64_t old = 0;
        if (RaiseStreamingMemoryRaw(&old)) {
            char buf[128];
            std::snprintf(buf, sizeof(buf),
                          "limits: память стриминга %llu MiB -> 1024 MiB\n",
                          static_cast<unsigned long long>(old >> 20));
            Log(buf);
        }
    }

    // Пулы и баррикады - раз в ~5 с и только когда игрок уже в мире.
    if (g_tick % 300 != 0) return;
    if (!PlayerInWorldRaw()) return;

    for (const auto& b : kBoosts) {
        if (BoostPoolRaw(b.ptrRva, b.stock, b.boosted) == 2) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "limits: пул %s: шаг роста %d -> %d\n",
                          b.name, b.stock, b.boosted);
            Log(buf);
        }
    }

    // Резолв IDE id баррикад по именам (один раз на имя).
    for (int i = 0; i < kBarrierModelCount; ++i) {
        if (g_barrierIds[i] != 0) continue;
        g_barrierIds[i] = ResolveModelRaw(kBarrierModels[i]);
        if (g_barrierIds[i] > 0) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "limits: модель %s = IDE %d\n",
                          kBarrierModels[i], g_barrierIds[i]);
            Log(buf);
        }
    }

    // Баррикады живут в пулах зданий/дамми (IPL-объекты). Скан периодический:
    // стриминг островов может пересоздавать их при подгрузке района.
    struct PoolScan {
        std::uintptr_t ptrRva;
        std::size_t stride;
    };
    constexpr PoolScan scans[] = {
        {revc::kCPoolsBuildingPool, kBuildingStride},
        {revc::kCPoolsDummyPool, kDummyStride},
    };
    SinkJob jobs[64];
    int sunk = 0;
    for (const auto& s : scans) {
        void* pool = *Resolve<void**>(s.ptrRva);
        if (!pool) continue;
        const int n = CollectBarriersRaw(pool, s.stride, g_barrierIds,
                                         kBarrierModelCount, jobs, 64);
        for (int i = 0; i < n; ++i) {
            if (!SinkEntityRaw(jobs[i].ent)) continue;
            ++sunk;
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                          "limits: баррикада (модель %d) в (%.0f, %.0f, %.0f) убрана\n",
                          jobs[i].model, static_cast<double>(jobs[i].x),
                          static_cast<double>(jobs[i].y),
                          static_cast<double>(jobs[i].z));
            Log(buf);
        }
    }
    if (sunk > 0 && !g_barriersAnnounced) {
        g_barriersAnnounced = true;
        vc::network::NetClient::Instance().AddLocalChatLine(
            "Мосты открыты: сюжетные баррикады убраны");
    }
}

} // namespace vc::game
