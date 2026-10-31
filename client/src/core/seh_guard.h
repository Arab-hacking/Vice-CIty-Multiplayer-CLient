#pragma once

// SEH-ловушки вокруг вызовов игры: падение внутри reVC (битый указатель,
// неготовый мир) превращается в запись в лог, а не в вылет процесса.
// MSVC: настоящий __try/__except (ловит access violation).
// Прочие компиляторы (локальная проверка Zig/Clang): обычный try/catch.
// ВАЖНО: функции с VC_TRY должны быть POD-only (без C++-объектов с деструкторами),
// иначе MSVC запрещает __try (C2712).
#ifdef _MSC_VER
#define VC_TRY __try
#define VC_EXCEPT __except (EXCEPTION_EXECUTE_HANDLER)
#else
#define VC_TRY try
#define VC_EXCEPT catch (...)
#endif
