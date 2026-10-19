// Оверлей Этапа 1.5: небольшая информационная ImGui-панель поверх игры.
//
// Как устроено:
//  1) Создаём временное D3D9-устройство, берём из его vtable адреса
//     EndScene (42) и Reset (16) — они общие для всех устройств процесса.
//  2) MinHook ставит хуки; в первом EndScene настоящего устройства игры
//     инициализируем ImGui (Win32 + DX9 backends), перехватываем WndProc.
//  3) Каждый EndScene рисуем панель статуса: версия, кадр хука игры,
//     координаты игрока (FindPlayerCoors — вызов из главного потока).
//  F7 — показать/скрыть панель. Чат и ввод появятся на следующих этапах.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <d3d9.h>

#include <cstdio>

#include <MinHook.h>
#include <imgui.h>
#include <backends/imgui_impl_dx9.h>
#include <backends/imgui_impl_win32.h>

#include <shared/version.h>

#include "../core/seh_guard.h"
#include "../game/hooks/hook_cgame_process.h"
#include "../game/revc.h"
#include "../network/net_client.h"
#include "../pools/player_pool.h"
#include "overlay.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace vc::render {
namespace {

using EndSceneFn = HRESULT(__stdcall*)(IDirect3DDevice9*);
using ResetFn = HRESULT(__stdcall*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
using FindPlayerCoorsFn = game::CVector (*)();

EndSceneFn g_origEndScene = nullptr;
ResetFn g_origReset = nullptr;
FILE* g_log = nullptr;

bool g_imguiReady = false;
bool g_visible = true;
bool g_chatOpen = false;
bool g_chatFocusRequest = false;
char g_chatBuffer[144] = {};
HWND g_hwnd = nullptr;
WNDPROC g_origWndProc = nullptr;

void Log(const char* text) {
    OutputDebugStringA(text);
    if (g_log) {
        std::fputs(text, g_log);
        std::fflush(g_log);
    }
}

LRESULT CALLBACK HookWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_imguiReady) {
        // Пока открыт чат, клавиатуру игре не отдаём (DirectInput это не остановит —
        // движение дополнительно замораживает хук CGame::Process через Teleport).
        if (g_chatOpen && (msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_CHAR ||
                           msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP)) {
            ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);
            return 0;
        }
        if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
            return 1;
    }
    return CallWindowProc(g_origWndProc, hwnd, msg, wParam, lParam);
}

// POD SEH-обёртка: проекция мир->экран не должна ронять игру.
bool SafeCalcScreenCoors(float x, float y, float z, float* outX, float* outY) {
    using CalcScreenCoorsFn =
        bool (*)(const game::CVector&, game::CVector*, float*, float*, bool);
    VC_TRY {
        auto calc = game::Resolve<CalcScreenCoorsFn>(game::revc::kCSpriteCalcScreenCoors);
        if (!calc) return false;
        const game::CVector in{x, y, z};
        game::CVector out{};
        float w = 0.0f, h = 0.0f;
        if (!calc(in, &out, &w, &h, true)) return false;
        *outX = out.x;
        *outY = out.y;
        return true;
    } VC_EXCEPT {
        return false;
    }
}

// Никнеймы над куклами: мировая точка над головой -> экран через CSprite::CalcScreenCoors.
void DrawNametags() {
    auto findPed = game::Resolve<game::FindPlayerPedFn>(game::revc::kFindPlayerPed);
    auto findCoors = game::Resolve<FindPlayerCoorsFn>(game::revc::kFindPlayerCoors);
    if (!findPed || !findPed()) return;
    const game::CVector local = findCoors();

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    for (const auto& info : vc::pools::PlayerPool::Instance().RenderInfos()) {
        const float dx = info.position.x - local.x;
        const float dy = info.position.y - local.y;
        const float dz = info.position.z - local.z;
        if (dx * dx + dy * dy + dz * dz > 120.0f * 120.0f) continue; // дальше не читаемо

        float sx = 0.0f, sy = 0.0f;
        if (!SafeCalcScreenCoors(info.position.x, info.position.y, info.position.z + 2.1f,
                                 &sx, &sy))
            continue; // за камерой/экраном или исключение игры

        const ImVec2 size = ImGui::CalcTextSize(info.nick.c_str());
        const ImVec2 at(sx - size.x * 0.5f, sy - size.y);
        draw->AddText(ImVec2(at.x + 1, at.y + 1), IM_COL32(0, 0, 0, 220), info.nick.c_str());
        draw->AddText(at, IM_COL32(255, 255, 255, 255), info.nick.c_str());
    }
}

void DrawChatWindow() {
    // Открытие по T (когда не печатаем).
    if (!g_chatOpen && ImGui::IsKeyPressed(ImGuiKey_T, false)) {
        g_chatOpen = true;
        g_chatFocusRequest = true;
        g_chatBuffer[0] = '\0';
        ImGui::GetIO().ClearInputCharacters(); // чтобы сама T не попала в текст
    }
    if (!g_chatOpen) return;

    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        g_chatOpen = false;
        return;
    }

    ImGui::SetNextWindowPos(ImVec2(12, 178), ImGuiCond_Appearing);
    ImGui::SetNextWindowBgAlpha(0.85f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize;
    if (ImGui::Begin("##vcmp_chat_input", nullptr, flags)) {
        ImGui::TextDisabled("Чат");
        ImGui::SetNextItemWidth(420);
        if (g_chatFocusRequest) {
            ImGui::SetKeyboardFocusHere();
            g_chatFocusRequest = false;
        }
        if (ImGui::InputTextWithHint("##chat_text", "Сообщение... (Enter - отправить)",
                                     g_chatBuffer, sizeof(g_chatBuffer),
                                     ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (g_chatBuffer[0] != '\0')
                vc::network::NetClient::Instance().SendChat(g_chatBuffer);
            g_chatBuffer[0] = '\0';
            g_chatOpen = false;
        }
    }
    ImGui::End();
}

HWND ResolveDeviceWindow(IDirect3DDevice9* device) {
    IDirect3DSwapChain9* swapChain = nullptr;
    if (SUCCEEDED(device->GetSwapChain(0, &swapChain)) && swapChain) {
        D3DPRESENT_PARAMETERS pp{};
        if (SUCCEEDED(swapChain->GetPresentParameters(&pp)) && pp.hDeviceWindow) {
            swapChain->Release();
            return pp.hDeviceWindow;
        }
        swapChain->Release();
    }
    D3DDEVICE_CREATION_PARAMETERS cp{};
    if (SUCCEEDED(device->GetCreationParameters(&cp))) return cp.hFocusWindow;
    return nullptr;
}

bool InitImGuiOnce(IDirect3DDevice9* device) {
    g_hwnd = ResolveDeviceWindow(device);
    if (!g_hwnd) {
        Log("overlay: не удалось получить HWND устройства\n");
        return false;
    }
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // не мусорить imgui.ini в папке игры
    // Шрифт с кириллицей из Windows; если файла нет — останется дефолтный.
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arial.ttf", 16.0f, nullptr,
                                 io.Fonts->GetGlyphRangesCyrillic());
    ImGui::StyleColorsDark();
    if (!ImGui_ImplWin32_Init(g_hwnd) || !ImGui_ImplDX9_Init(device)) {
        Log("overlay: ImGui_Impl*_Init не удался\n");
        return false;
    }
    g_origWndProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtr(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&HookWndProc)));
    g_imguiReady = true;
    Log("overlay: ImGui инициализирован, панель активна (F7 — вкл/выкл)\n");
    return true;
}

HRESULT __stdcall HookReset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* params) {
    if (g_imguiReady) ImGui_ImplDX9_InvalidateDeviceObjects();
    const HRESULT hr = g_origReset(device, params);
    if (g_imguiReady && SUCCEEDED(hr)) ImGui_ImplDX9_CreateDeviceObjects();
    return hr;
}

HRESULT __stdcall HookEndScene(IDirect3DDevice9* device) {
    if (!g_imguiReady) InitImGuiOnce(device);

    if (g_imguiReady) {
        if (GetAsyncKeyState(VK_F7) & 1) g_visible = !g_visible;

        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        if (g_visible) {
            ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_Appearing);
            ImGui::SetNextWindowBgAlpha(0.75f);
            const ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove |
                                           ImGuiWindowFlags_NoResize |
                                           ImGuiWindowFlags_NoCollapse |
                                           ImGuiWindowFlags_AlwaysAutoResize;
            if (ImGui::Begin("Vice City Multiplayer", nullptr, flags)) {
                ImGui::Text("VC:MP client %s", vc::CLIENT_VERSION);
                ImGui::Separator();
                ImGui::Text("Кадр CGame::Process: %llu",
                            static_cast<unsigned long long>(game::hooks::FrameCounter()));
                auto findPed = game::Resolve<game::FindPlayerPedFn>(game::revc::kFindPlayerPed);
                auto findCoors = game::Resolve<FindPlayerCoorsFn>(game::revc::kFindPlayerCoors);
                void* ped = findPed ? findPed() : nullptr;
                if (ped) {
                    const game::CVector pos = findCoors();
                    ImGui::Text("Игрок: %.2f, %.2f, %.2f", pos.x, pos.y, pos.z);
                } else {
                    ImGui::TextDisabled("Игрок не заспавнен");
                }
                ImGui::Separator();
                const auto& net = vc::network::NetClient::Instance();
                const bool connected =
                    net.GetState() == vc::network::State::Connected;
                ImGui::TextColored(connected ? ImVec4(0.45f, 0.95f, 0.45f, 1.0f)
                                             : ImVec4(0.95f, 0.75f, 0.35f, 1.0f),
                                   "Сеть: %s", net.StatusText().c_str());
                const auto chatLines = net.ChatLines();
                for (const std::string& line : chatLines)
                    ImGui::TextWrapped("%s", line.c_str());
                ImGui::Separator();
                ImGui::TextDisabled("T - чат, F7 - панель");
            }
            ImGui::End();
        }

        DrawChatWindow();

        ImGui::EndFrame();
        ImGui::Render();
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    }
    return g_origEndScene(device);
}

bool GetVtableAddresses(void** outEndScene, void** outReset) {
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) return false;

    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.hDeviceWindow = GetDesktopWindow();

    IDirect3DDevice9* device = nullptr;
    HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, pp.hDeviceWindow,
                                   D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device);
    if (FAILED(hr)) {
        hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_REF, pp.hDeviceWindow,
                               D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device);
    }
    if (FAILED(hr) || !device) {
        d3d->Release();
        return false;
    }
    void** vtable = *reinterpret_cast<void***>(device);
    *outEndScene = vtable[42]; // IDirect3DDevice9::EndScene
    *outReset = vtable[16];    // IDirect3DDevice9::Reset
    device->Release();
    d3d->Release();
    return true;
}

} // namespace

bool IsChatOpen() { return g_chatOpen; }

bool InstallOverlayHooks(FILE* log) {
    g_log = log;
    void* endScene = nullptr;
    void* reset = nullptr;
    if (!GetVtableAddresses(&endScene, &reset)) {
        Log("overlay: не удалось создать dummy-устройство D3D9\n");
        return false;
    }
    if (MH_Initialize() != MH_OK) {
        Log("overlay: MH_Initialize не удался\n");
        return false;
    }
    if (MH_CreateHook(endScene, reinterpret_cast<void*>(&HookEndScene),
                      reinterpret_cast<void**>(&g_origEndScene)) != MH_OK ||
        MH_CreateHook(reset, reinterpret_cast<void*>(&HookReset),
                      reinterpret_cast<void**>(&g_origReset)) != MH_OK) {
        Log("overlay: MH_CreateHook не удался\n");
        return false;
    }
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        Log("overlay: MH_EnableHook не удался\n");
        return false;
    }
    Log("overlay: хуки EndScene/Reset установлены\n");
    return true;
}

} // namespace vc::render
