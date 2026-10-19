#pragma once

#include <cstdint>

namespace vc {

// Версия сетевого протокола. Увеличивается при ЛЮБОМ несовместимом
// изменении формата пакетов (см. docs/NETWORK-PROTOCOL.md).
inline constexpr std::uint32_t PROTOCOL_VERSION = 2;

inline constexpr const char* CLIENT_VERSION = "0.6.0-dev";

} // namespace vc
