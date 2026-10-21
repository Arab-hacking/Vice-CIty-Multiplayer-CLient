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
// Настройки ленты чата (vc-client.ini, секция [Chat]).
float g_chatPosX = 12.0f;
float g_chatPosY = 12.0f;
float g_chatW = 520.0f;
float g_chatH = 220.0f;
float g_chatBgAlpha = 0.55f;
bool g_chatBorder = true;
bool g_chatEdit = false; // режим настройки ленты (двигать/размер)
// Открытый диалог от сервера (стиль SA-MP) и его локальное состояние.
std::optional<vc::protocol::ShowDialogData> g_dialog;
int g_dialogSel = 0;
char g_dialogInput[144] = {};
bool g_dialogFocus = false;
HWND g_hwnd = nullptr;
WNDPROC g_origWndProc = nullptr;

void Log(const char* text) {
    OutputDebugStringA(text);
    if (g_log) {
        std::fputs(text, g_log);
        std::fflush(g_log);
    }
}

void LoadChatSettings() {
    const std::string& path = vc::network::NetClient::Instance().IniPath();
    if (path.empty()) return;
    g_chatPosX = static_cast<float>(GetPrivateProfileIntA("Chat", "PosX", 12, path.c_str()));
    g_chatPosY = static_cast<float>(GetPrivateProfileIntA("Chat", "PosY", 12, path.c_str()));
    g_chatW = static_cast<float>(GetPrivateProfileIntA("Chat", "Width", 520, path.c_str()));
    g_chatH = static_cast<float>(GetPrivateProfileIntA("Chat", "Height", 220, path.c_str()));
    int alpha = GetPrivateProfileIntA("Chat", "BgAlpha", 55, path.c_str());
    if (alpha < 0) alpha = 0;
    if (alpha > 100) alpha = 100;
    g_chatBgAlpha = static_cast<float>(alpha) / 100.0f;
    g_chatBorder = GetPrivateProfileIntA("Chat", "Border", 1, path.c_str()) != 0;
    Log("overlay: настройки чата загружены из ini\n");
}

void SaveChatSettings() {
    const std::string& path = vc::network::NetClient::Instance().IniPath();
    if (path.empty()) return;
    char buf[32];
    auto writeInt = [&](const char* key, int value) {
        std::snprintf(buf, sizeof(buf), "%d", value);
        WritePrivateProfileStringA("Chat", key, buf, path.c_str());
    };
    writeInt("PosX", static_cast<int>(g_chatPosX));
    writeInt("PosY", static_cast<int>(g_chatPosY));
    writeInt("Width", static_cast<int>(g_chatW));
    writeInt("Height", static_cast<int>(g_chatH));
    writeInt("BgAlpha", static_cast<int>(g_chatBgAlpha * 100.0f));
    writeInt("Border", g_chatBorder ? 1 : 0);
    Log("overlay: настройки чата сохранены в ini\n");
}

LRESULT CALLBACK HookWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_imguiReady) {
        // Пока открыт чат, клавиатуру игре не отдаём (DirectInput это не остановит —
        // движение дополнительно замораживает хук CGame::Process через Teleport).
        if ((g_chatOpen || g_chatEdit || g_dialog.has_value()) && (msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_CHAR ||
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

// Никнеймы над куклами: точка над головой -> экран через CSprite::CalcScreenCoors.
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
        // HP-плашка под ником: чёрный фон + заливка красный->зелёный по процентам.
        constexpr float kBarW = 38.0f, kBarH = 5.0f;
        const float barX = sx - kBarW * 0.5f;
        const float barY = at.y + size.y + 2.0f;
        float pct = static_cast<float>(info.health) / 100.0f;
        if (pct < 0.0f) pct = 0.0f;
        if (pct > 1.0f) pct = 1.0f;
        draw->AddRectFilled(ImVec2(barX - 1, barY - 1), ImVec2(barX + kBarW + 1, barY + kBarH + 1),
                            IM_COL32(0, 0, 0, 190));
        const auto hpColor = IM_COL32(static_cast<int>(255.0f * (1.0f - pct)),
                                      static_cast<int>(220.0f * pct), 40, 255);
        draw->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + kBarW * pct, barY + kBarH),
                            hpColor);
    }
}

// Текст диалогов SA-MP: {RRGGBB} переключает цвет дальнейшего текста.
bool IsHexColorAt(const std::string& s, std::size_t i) {
    if (i + 8 > s.size() || s[i] != '{' || s[i + 7] != '}') return false;
    for (int k = 1; k <= 6; ++k) {
        const char c = s[i + static_cast<std::size_t>(k)];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
    }
    return true;
}

ImVec4 ParseDialogColor(const std::string& s, std::size_t i) {
    auto hexPair = [&](std::size_t idx) {
        auto v = [](char c) {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return c - 'A' + 10;
        };
        return v(s[i + idx]) * 16 + v(s[i + idx + 1]);
    };
    return ImVec4(hexPair(1) / 255.0f, hexPair(3) / 255.0f, hexPair(5) / 255.0f, 1.0f);
}

std::string StripDialogColors(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        if (IsHexColorAt(s, i)) i += 8;
        else out += s[i++];
    }
    return out;
}

std::vector<std::string> SplitDialogLines(const std::string& s, char delim) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= s.size()) {
        const std::size_t pos = s.find(delim, start);
        if (pos == std::string::npos) {
            parts.push_back(s.substr(start));
            break;
        }
        parts.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

// Многострочный текст диалога с цветами {RRGGBB} построчно (как в SA-MP).
void DrawColoredDialogText(const std::string& info) {
    for (const auto& line : SplitDialogLines(info, '\n')) {
        ImVec4 color(1.0f, 1.0f, 1.0f, 1.0f);
        bool first = true;
        std::size_t segStart = 0;
        auto flush = [&](std::size_t end) {
            if (end <= segStart) return;
            if (!first) ImGui::SameLine(0.0f, 0.0f);
            first = false;
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextUnformatted(line.c_str() + segStart, line.c_str() + end);
            ImGui::PopStyleColor();
        };
        for (std::size_t i = 0; i < line.size();) {
            if (IsHexColorAt(line, i)) {
                flush(i);
                color = ParseDialogColor(line, i);
                i += 8;
                segStart = i;
            } else {
                ++i;
            }
        }
        flush(line.size());
        if (first) ImGui::TextUnformatted(""); // пустая строка тоже занимает высоту
    }
}

void SetDialogCursor(bool show) {
    if (show) {
        for (int i = 0; i < 16 && ShowCursor(TRUE) < 0; ++i) {
        }
        ClipCursor(nullptr);
    } else {
        for (int i = 0; i < 16 && ShowCursor(FALSE) >= 0; ++i) {
        }
    }
}

// Окно диалога от сервера: стили SA-MP 0-5 (MsgBox/Input/List/Password/
// TabList/TabListHeaders). Enter - первая кнопка, Esc - вторая/закрыть,
// стрелки и двойной клик - выбор в списке.
void DrawDialogWindow() {
    auto& net = vc::network::NetClient::Instance();
    if (auto pending = net.TakePendingDialog()) {
        g_dialog = *pending;
        g_dialogSel = 0;
        g_dialogInput[0] = '\0';
        g_dialogFocus = true;
        SetDialogCursor(true);
        Log(("overlay: диалог '" + g_dialog->caption + "' открыт\n").c_str());
    }
    if (!g_dialog) return;
    const auto dlg = *g_dialog; // копия: ответ сбросит g_dialog в конце кадра
    const auto style = static_cast<vc::protocol::DialogStyle>(dlg.style);
    const bool isList = style == vc::protocol::DialogStyle::List ||
                        style == vc::protocol::DialogStyle::TabList ||
                        style == vc::protocol::DialogStyle::TabListHeaders;
    const bool isInput = style == vc::protocol::DialogStyle::Input ||
                         style == vc::protocol::DialogStyle::Password;

    std::vector<std::string> items;
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> headers;
    int itemCount = 0;
    if (style == vc::protocol::DialogStyle::List) {
        for (const auto& s : SplitDialogLines(dlg.info, '\n'))
            items.push_back(StripDialogColors(s));
        itemCount = static_cast<int>(items.size());
    } else if (isList) {
        const auto rawRows = SplitDialogLines(dlg.info, '\n');
        std::size_t start = 0;
        if (style == vc::protocol::DialogStyle::TabListHeaders && !rawRows.empty()) {
            for (const auto& c : SplitDialogLines(rawRows[0], '\t'))
                headers.push_back(StripDialogColors(c));
            start = 1;
        }
        for (std::size_t r = start; r < rawRows.size(); ++r) {
            std::vector<std::string> row;
            for (const auto& c : SplitDialogLines(rawRows[r], '\t'))
                row.push_back(StripDialogColors(c));
            rows.push_back(std::move(row));
        }
        itemCount = static_cast<int>(rows.size());
    }
    if (itemCount == 0) g_dialogSel = 0;
    else if (g_dialogSel > itemCount - 1) g_dialogSel = itemCount - 1;
    if (g_dialogSel < 0) g_dialogSel = 0;

    int wantResponse = -1; // -1 нет, 0/1 - отправить после End()
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_Appearing);
    ImGui::SetNextWindowBgAlpha(0.97f);
    const std::string title = dlg.caption + "##vcmp_dialog";
    if (ImGui::Begin(title.c_str(), nullptr,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
        if (style == vc::protocol::DialogStyle::MsgBox || isInput)
            DrawColoredDialogText(dlg.info);

        if (isInput) {
            ImGui::Spacing();
            ImGui::SetNextItemWidth(-1.0f);
            if (g_dialogFocus) {
                ImGui::SetKeyboardFocusHere();
                g_dialogFocus = false;
            }
            const ImGuiInputTextFlags inputFlags =
                ImGuiInputTextFlags_EnterReturnsTrue |
                (style == vc::protocol::DialogStyle::Password
                     ? ImGuiInputTextFlags_Password
                     : 0);
            if (ImGui::InputText("##dlg_input", g_dialogInput, sizeof(g_dialogInput),
                                 inputFlags))
                wantResponse = 1;
        }

        if (style == vc::protocol::DialogStyle::List) {
            ImGui::BeginChild("##dlg_list", ImVec2(0, 190), true);
            for (int i = 0; i < itemCount; ++i) {
                if (ImGui::Selectable((items[i] + "##" + std::to_string(i)).c_str(),
                                      g_dialogSel == i,
                                      ImGuiSelectableFlags_AllowDoubleClick)) {
                    g_dialogSel = i;
                    if (ImGui::IsMouseDoubleClicked(0)) wantResponse = 1;
                }
            }
            ImGui::EndChild();
        } else if (isList) {
            int cols = 1;
            for (const auto& row : rows)
                if (static_cast<int>(row.size()) > cols)
                    cols = static_cast<int>(row.size());
            if (static_cast<int>(headers.size()) > cols)
                cols = static_cast<int>(headers.size());
            ImGui::BeginChild("##dlg_tab", ImVec2(0, 190), false);
            if (ImGui::BeginTable("##dlg_table", cols,
                                  ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                for (int c = 0; c < cols; ++c)
                    ImGui::TableSetupColumn(
                        c < static_cast<int>(headers.size()) ? headers[c].c_str() : "");
                if (!headers.empty()) ImGui::TableHeadersRow();
                for (int i = 0; i < itemCount; ++i) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    const std::string first = rows[i].empty() ? "" : rows[i][0];
                    if (ImGui::Selectable(
                            (first + "##r" + std::to_string(i)).c_str(), g_dialogSel == i,
                            ImGuiSelectableFlags_SpanAllColumns |
                                ImGuiSelectableFlags_AllowDoubleClick)) {
                        g_dialogSel = i;
                        if (ImGui::IsMouseDoubleClicked(0)) wantResponse = 1;
                    }
                    for (int c = 1; c < cols && c < static_cast<int>(rows[i].size()); ++c) {
                        ImGui::TableSetColumnIndex(c);
                        ImGui::TextUnformatted(rows[i][c].c_str());
                    }
                }
                ImGui::EndTable();
            }
            ImGui::EndChild();
        }

        if (isList && itemCount > 0) {
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true) &&
                g_dialogSel < itemCount - 1)
                ++g_dialogSel;
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true) && g_dialogSel > 0)
                --g_dialogSel;
        }

        ImGui::Spacing();
        if (ImGui::Button(dlg.button1.empty() ? "OK" : dlg.button1.c_str(),
                          ImVec2(130, 0)))
            wantResponse = 1;
        if (!dlg.button2.empty()) {
            ImGui::SameLine();
            if (ImGui::Button(dlg.button2.c_str(), ImVec2(130, 0))) wantResponse = 0;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) wantResponse = 0;
        if (!isInput && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                         ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)))
            wantResponse = 1;
    }
    ImGui::End();

    if (wantResponse >= 0) {
        vc::protocol::DialogResponseData resp;
        resp.dialogId = dlg.dialogId;
        resp.response = static_cast<std::uint8_t>(wantResponse);
        resp.listItem = (isList && wantResponse == 1 && itemCount > 0)
                            ? static_cast<std::int16_t>(g_dialogSel)
                            : static_cast<std::int16_t>(-1);
        resp.inputText = isInput ? g_dialogInput : "";
        net.SendDialogResponse(resp);
        g_dialog.reset();
        SetDialogCursor(false);
    }
}

// Лента чата: отдельное всегда видимое окно (раньше жила в F7-панели).
// Позиция/размер/фон/рамка настраиваются (F7 -> "Настройка ленты") и
// сохраняются в vc-client.ini [Chat].
void DrawChatFeed() {
    const auto lines = vc::network::NetClient::Instance().ChatLines();
    if (lines.empty() && !g_chatEdit) return;

    // Позиция/размер применяются каждый кадр из настроек; в режиме настройки
    // окно можно таскать мышью и тянуть за края, а значения читаются обратно
    // (поэтому Always не мешает перетаскиванию).
    ImGui::SetNextWindowPos(ImVec2(g_chatPosX, g_chatPosY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(g_chatW, g_chatH), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(g_chatBgAlpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, g_chatBorder ? 1.0f : 0.0f);
    const ImGuiWindowFlags flags =
        g_chatEdit ? ImGuiWindowFlags_NoCollapse
                   : (ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs);
    if (ImGui::Begin("Чат##vcmp_chat_feed", nullptr, flags)) {
        if (g_chatEdit) {
            const ImVec2 pos = ImGui::GetWindowPos();
            const ImVec2 size = ImGui::GetWindowSize();
            g_chatPosX = pos.x;
            g_chatPosY = pos.y;
            g_chatW = size.x;
            g_chatH = size.y;
            // Стрелки - двигать, Ctrl+стрелки - размер, Shift - быстрее.
            const float step = (ImGui::IsKeyDown(ImGuiKey_LeftShift) ||
                                ImGui::IsKeyDown(ImGuiKey_RightShift))
                                   ? 10.0f
                                   : 2.0f;
            const bool ctrl = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) ||
                              ImGui::IsKeyDown(ImGuiKey_RightCtrl);
            auto clampf = [](float v, float lo, float hi) {
                return v < lo ? lo : (v > hi ? hi : v);
            };
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true)) {
                if (ctrl) g_chatW = clampf(g_chatW - step, 200.0f, 1600.0f);
                else g_chatPosX = clampf(g_chatPosX - step, 0.0f, 4000.0f);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) {
                if (ctrl) g_chatW = clampf(g_chatW + step, 200.0f, 1600.0f);
                else g_chatPosX = clampf(g_chatPosX + step, 0.0f, 4000.0f);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) {
                if (ctrl) g_chatH = clampf(g_chatH - step, 80.0f, 1200.0f);
                else g_chatPosY = clampf(g_chatPosY - step, 0.0f, 4000.0f);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) {
                if (ctrl) g_chatH = clampf(g_chatH + step, 80.0f, 1200.0f);
                else g_chatPosY = clampf(g_chatPosY + step, 0.0f, 4000.0f);
            }
        }
        if (ImGui::BeginChild("##feed_lines", ImVec2(0, 0), false,
                              ImGuiWindowFlags_NoScrollbar)) {
            if (g_chatEdit)
                ImGui::TextDisabled(
                    "Настройка ленты: мышь или стрелки; Ctrl+стрелки - размер");
            for (const std::string& line : lines)
                ImGui::TextWrapped("%s", line.c_str());
            ImGui::SetScrollHereY(1.0f); // свежие строки всегда снизу
        }
        ImGui::EndChild();
    }
    ImGui::End();
    ImGui::PopStyleVar();
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

    ImGui::SetNextWindowPos(ImVec2(g_chatPosX, g_chatPosY + g_chatH + 4.0f),
                            ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.85f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize;
    if (ImGui::Begin("##vcmp_chat_input", nullptr, flags)) {
        ImGui::TextDisabled("Чат");
        ImGui::SetNextItemWidth(g_chatW - 16.0f);
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
    LoadChatSettings();
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
                ImGui::Separator();
                ImGui::Text("Лента чата");
                const bool wasEdit = g_chatEdit;
                ImGui::Checkbox("Настройка (двигать/размер)", &g_chatEdit);
                if (g_chatEdit && !wasEdit) {
                    for (int i = 0; i < 16 && ShowCursor(TRUE) < 0; ++i) {
                    }
                    ClipCursor(nullptr);
                }
                if (!g_chatEdit && wasEdit) {
                    for (int i = 0; i < 16 && ShowCursor(FALSE) >= 0; ++i) {
                    }
                    SaveChatSettings();
                }
                ImGui::SliderFloat("Непрозрачность фона", &g_chatBgAlpha, 0.0f, 1.0f,
                                   "%.2f");
                if (ImGui::IsItemDeactivatedAfterEdit()) SaveChatSettings();
                if (ImGui::Checkbox("Рамка окна ленты", &g_chatBorder)) SaveChatSettings();
                ImGui::TextDisabled(
                    "В настройке: мышь или стрелки; Ctrl+стрелки - размер");
                ImGui::Separator();
                ImGui::TextDisabled("T - чат, G - сесть/выйти пассажиром, F7 - панель");
            }
            ImGui::End();
        }

        DrawChatFeed();
        DrawChatWindow();
        DrawDialogWindow();
        DrawNametags();

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
bool IsOverlayVisible() { return g_visible; }

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
