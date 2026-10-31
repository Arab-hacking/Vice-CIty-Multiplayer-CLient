// Реализации shared появятся здесь по мере роста библиотеки
// (bitstream, сериализация пакетов). Файл нужен, чтобы статическая
// библиотека vc-shared собиралась уже на этапе каркаса.
#include <shared/version.h>

namespace vc {
namespace {
// Якорь линковки до появления первых настоящих .cpp в shared/src.
[[maybe_unused]] constexpr std::uint32_t kAnchor = PROTOCOL_VERSION;
} // namespace
} // namespace vc
