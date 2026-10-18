#pragma once

namespace vc {

// Тройка координат в системе GTA VC (метры, Z — вверх).
// Раскладка совпадает с игровым CVector — это осознанно.
struct Vector3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

} // namespace vc
