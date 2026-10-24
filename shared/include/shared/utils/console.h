#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Русские строки в консоли Windows без ручного `chcp 65001`:
// весь вывод проекта — UTF-8, переключаем кодовую страницу процесса.
namespace vc {

inline void InitConsoleUtf8() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

} // namespace vc
