#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// RakNet-совместимый BitStream.
// Семантика битов повторяет core/bitstream.py из PyRakBR (MIT, Sahiro01),
// который в свою очередь повторяет оригинальный RakNet BitStream:
//  - биты пишутся от старшего бита байта (позиция 7-(pos%8));
//  - целые (writeUintXX) кладутся как little-endian байты;
//  - writeCompressed — классический алгоритм RakNet WriteCompressed.
namespace vc::net {

class BitStream {
public:
    BitStream() = default;
    explicit BitStream(std::span<const std::uint8_t> data);

    // --- запись ---
    void WriteBit(bool value);
    void WriteBool(bool value) { WriteBit(value); }
    void WriteBits(std::uint64_t value, int count);
    void WriteBytes(std::span<const std::uint8_t> data);
    void WriteUint8(std::uint8_t value);
    void WriteUint16(std::uint16_t value);
    void WriteUint32(std::uint32_t value);
    void WriteFloat(float value);
    // RakNet WriteCompressed: sizeBits = 8/16/32 (размер типа в битах).
    void WriteCompressedUnsigned(std::uint32_t value, int sizeBits);
    void AlignWriteToByte();

    // --- чтение ---
    bool ReadBit();
    bool ReadBool() { return ReadBit(); }
    std::uint64_t ReadBits(int count);
    void ReadBytes(std::span<std::uint8_t> out);
    std::uint8_t ReadUint8();
    std::uint16_t ReadUint16();
    std::uint32_t ReadUint32();
    float ReadFloat();
    std::uint32_t ReadCompressedUnsigned(int sizeBits);
    void AlignReadToByte();

    // --- состояние ---
    [[nodiscard]] const std::vector<std::uint8_t>& Data() const { return m_buffer; }
    [[nodiscard]] std::size_t BitLength() const { return m_writePos; }
    [[nodiscard]] std::size_t BitPosition() const { return m_readPos; }
    [[nodiscard]] std::size_t RemainingBits() const {
        return m_buffer.size() * 8 - m_readPos;
    }
    [[nodiscard]] bool EndOfStream() const { return RemainingBits() == 0; }
    // Чтение за пределами буфера выставляет флаг и возвращает нули (без исключений
    // на сетевом пути: битый пакет отбрасывается вызывающим кодом по этому флагу).
    [[nodiscard]] bool Overflowed() const { return m_overflow; }

    void ResetRead() { m_readPos = 0; m_overflow = false; }

private:
    void EnsureWriteCapacity(std::size_t bits);

    std::vector<std::uint8_t> m_buffer;
    std::size_t m_writePos = 0; // в битах
    std::size_t m_readPos = 0;  // в битах
    bool m_overflow = false;
};

} // namespace vc::net
