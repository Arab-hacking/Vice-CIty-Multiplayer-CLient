#pragma once

#include <cmath>

#include "vector3.h"

// Кватернион для синка поз транспорта. Конвенция: базисные векторы матрицы
// сущности reVC (right, forward, up — строки RwMatrix) трактуются как строки
// матрицы поворота R; FromBasis/ToBasis взаимно обратны (проверено тестом),
// Nlerp даёт плавный промежуточный поворот.
namespace vc {

struct Quaternion {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

inline Quaternion QuatNormalize(Quaternion q) {
    const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (len <= 0.0001f) return {0.0f, 0.0f, 0.0f, 1.0f};
    const float inv = 1.0f / len;
    return {q.x * inv, q.y * inv, q.z * inv, q.w * inv};
}

inline Quaternion QuatFromBasis(const Vector3& right, const Vector3& forward,
                                const Vector3& up) {
    const float m00 = right.x, m01 = right.y, m02 = right.z;
    const float m10 = forward.x, m11 = forward.y, m12 = forward.z;
    const float m20 = up.x, m21 = up.y, m22 = up.z;
    Quaternion q;
    const float trace = m00 + m11 + m22;
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (m21 - m12) / s;
        q.y = (m02 - m20) / s;
        q.z = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q.w = (m21 - m12) / s;
        q.x = 0.25f * s;
        q.y = (m01 + m10) / s;
        q.z = (m02 + m20) / s;
    } else if (m11 > m22) {
        const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q.w = (m02 - m20) / s;
        q.x = (m01 + m10) / s;
        q.y = 0.25f * s;
        q.z = (m12 + m21) / s;
    } else {
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q.w = (m10 - m01) / s;
        q.x = (m02 + m20) / s;
        q.y = (m12 + m21) / s;
        q.z = 0.25f * s;
    }
    return QuatNormalize(q);
}

inline void QuatToBasis(const Quaternion& qq, Vector3& right, Vector3& forward,
                        Vector3& up) {
    const Quaternion q = QuatNormalize(qq);
    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    right = {1.0f - 2.0f * (yy + zz), 2.0f * (xy - wz), 2.0f * (xz + wy)};
    forward = {2.0f * (xy + wz), 1.0f - 2.0f * (xx + zz), 2.0f * (yz - wx)};
    up = {2.0f * (xz - wy), 2.0f * (yz + wx), 1.0f - 2.0f * (xx + yy)};
}

// Нормализованая линейная интерполяция (кратчайший путь) — достаточно для
// плавного сетевого движения на 10 Гц.
inline Quaternion QuatNlerp(const Quaternion& a, const Quaternion& b, float t) {
    Quaternion other = b;
    const float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (dot < 0.0f) other = {-b.x, -b.y, -b.z, -b.w};
    return QuatNormalize({a.x + (other.x - a.x) * t, a.y + (other.y - a.y) * t,
                          a.z + (other.z - a.z) * t, a.w + (other.w - a.w) * t});
}

} // namespace vc
