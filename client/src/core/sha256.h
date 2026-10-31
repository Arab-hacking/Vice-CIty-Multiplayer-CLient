#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Компактный SHA-256 (FIPS 180-4) без внешних зависимостей.
// Нужен клиенту для проверки, что reVC.exe в процессе — именно целевой билд,
// под который сгенерированы RVA (kTargetExeSha256).
namespace vc::crypto {

class Sha256 {
public:
    Sha256() { Reset(); }

    void Reset() {
        m_state = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                   0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
        m_bufferSize = 0;
        m_totalSize = 0;
    }

    void Update(const std::uint8_t* data, std::size_t size) {
        m_totalSize += size;
        while (size > 0) {
            const std::size_t take = 64 - m_bufferSize < size ? 64 - m_bufferSize : size;
            std::memcpy(m_buffer.data() + m_bufferSize, data, take);
            m_bufferSize += take;
            data += take;
            size -= take;
            if (m_bufferSize == 64) {
                ProcessBlock(m_buffer.data());
                m_bufferSize = 0;
            }
        }
    }

    std::array<std::uint8_t, 32> Final() {
        const std::uint64_t bitLength = m_totalSize * 8;
        const std::uint8_t padStart = 0x80;
        Update(&padStart, 1);
        const std::uint8_t zero = 0;
        while (m_bufferSize != 56) Update(&zero, 1); // Update меняет m_totalSize — длина уже сохранена
        std::uint8_t lenBytes[8];
        for (int i = 0; i < 8; ++i)
            lenBytes[i] = static_cast<std::uint8_t>(bitLength >> (56 - 8 * i));
        // Пишем длину напрямую в буфер и обрабатываем последний блок вручную,
        // чтобы не зациклиться через Update.
        std::memcpy(m_buffer.data() + 56, lenBytes, 8);
        ProcessBlock(m_buffer.data());
        std::array<std::uint8_t, 32> digest{};
        for (int i = 0; i < 8; ++i)
            for (int j = 0; j < 4; ++j)
                digest[i * 4 + j] = static_cast<std::uint8_t>(m_state[i] >> (24 - 8 * j));
        return digest;
    }

private:
    static std::uint32_t RotR(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void ProcessBlock(const std::uint8_t* block) {
        static constexpr std::uint32_t K[64] = {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
            0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
            0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
            0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
            0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
            0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
            0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
            0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
            0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
                   (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
                   block[i * 4 + 3];
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = RotR(w[i - 15], 7) ^ RotR(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = RotR(w[i - 2], 17) ^ RotR(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        auto [a, b, c, d, e, f, g, h] = m_state;
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t S1 = RotR(e, 6) ^ RotR(e, 11) ^ RotR(e, 25);
            const std::uint32_t ch = (e & f) ^ (~e & g);
            const std::uint32_t t1 = h + S1 + ch + K[i] + w[i];
            const std::uint32_t S0 = RotR(a, 2) ^ RotR(a, 13) ^ RotR(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = S0 + maj;
            h = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        m_state = {m_state[0] + a, m_state[1] + b, m_state[2] + c, m_state[3] + d,
                   m_state[4] + e, m_state[5] + f, m_state[6] + g, m_state[7] + h};
    }

    std::array<std::uint32_t, 8> m_state{};
    std::array<std::uint8_t, 64> m_buffer{};
    std::size_t m_bufferSize = 0;
    std::uint64_t m_totalSize = 0;
};

} // namespace vc::crypto
