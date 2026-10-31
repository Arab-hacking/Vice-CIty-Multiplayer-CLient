#include <shared/net/bitstream.h>

#include <algorithm>
#include <cstring>

namespace vc::net {

BitStream::BitStream(std::span<const std::uint8_t> data)
    : m_buffer(data.begin(), data.end()), m_writePos(data.size() * 8) {}

void BitStream::EnsureWriteCapacity(std::size_t bits) {
    const std::size_t needed = (m_writePos + bits + 7) / 8;
    if (needed > m_buffer.size()) m_buffer.resize(needed, 0);
}

void BitStream::WriteBit(bool value) {
    EnsureWriteCapacity(1);
    const std::size_t byteIndex = m_writePos / 8;
    const int bitIndex = 7 - static_cast<int>(m_writePos % 8);
    if (value)
        m_buffer[byteIndex] |= static_cast<std::uint8_t>(1u << bitIndex);
    else
        m_buffer[byteIndex] &= static_cast<std::uint8_t>(~(1u << bitIndex));
    ++m_writePos;
}

void BitStream::WriteBits(std::uint64_t value, int count) {
    // Старший бит значения — первым (как write_bits в PyRakBR).
    for (int i = count - 1; i >= 0; --i) WriteBit(((value >> i) & 1u) != 0);
}

void BitStream::WriteBytes(std::span<const std::uint8_t> data) {
    if (m_writePos % 8 == 0) {
        EnsureWriteCapacity(data.size() * 8);
        std::memcpy(m_buffer.data() + m_writePos / 8, data.data(), data.size());
        m_writePos += data.size() * 8;
    } else {
        for (std::uint8_t b : data) WriteBits(b, 8);
    }
}

void BitStream::WriteUint8(std::uint8_t value) { WriteBytes({&value, 1}); }

void BitStream::WriteUint16(std::uint16_t value) {
    const std::uint8_t raw[2] = {static_cast<std::uint8_t>(value & 0xFF),
                                  static_cast<std::uint8_t>(value >> 8)};
    WriteBytes(raw);
}

void BitStream::WriteUint32(std::uint32_t value) {
    const std::uint8_t raw[4] = {
        static_cast<std::uint8_t>(value & 0xFF),
        static_cast<std::uint8_t>((value >> 8) & 0xFF),
        static_cast<std::uint8_t>((value >> 16) & 0xFF),
        static_cast<std::uint8_t>(value >> 24)};
    WriteBytes(raw);
}

void BitStream::WriteFloat(float value) {
    std::uint32_t raw;
    std::memcpy(&raw, &value, 4);
    WriteUint32(raw);
}

void BitStream::WriteCompressedUnsigned(std::uint32_t value, int sizeBits) {
    const int numBytes = sizeBits >> 3;
    std::uint8_t raw[4] = {};
    for (int i = 0; i < numBytes; ++i) raw[i] = static_cast<std::uint8_t>(value >> (8 * i));

    int currentByte = numBytes - 1;
    while (currentByte > 0) {
        if (raw[currentByte] == 0x00) {
            WriteBool(true);
        } else {
            WriteBool(false);
            for (int i = 0; i <= currentByte; ++i) WriteBits(raw[i], 8);
            return;
        }
        --currentByte;
    }
    if ((raw[0] & 0xF0) == 0x00) {
        WriteBool(true);
        WriteBits(raw[0] & 0x0F, 4);
    } else {
        WriteBool(false);
        WriteBits(raw[0], 8);
    }
}

void BitStream::AlignWriteToByte() {
    if (m_writePos % 8 != 0) m_writePos += 8 - (m_writePos % 8);
}

bool BitStream::ReadBit() {
    if (m_readPos >= m_buffer.size() * 8) {
        m_overflow = true;
        return false;
    }
    const std::size_t byteIndex = m_readPos / 8;
    const int bitIndex = 7 - static_cast<int>(m_readPos % 8);
    ++m_readPos;
    return ((m_buffer[byteIndex] >> bitIndex) & 1u) != 0;
}

std::uint64_t BitStream::ReadBits(int count) {
    std::uint64_t value = 0;
    for (int i = 0; i < count; ++i) value = (value << 1) | (ReadBit() ? 1u : 0u);
    return value;
}

void BitStream::ReadBytes(std::span<std::uint8_t> out) {
    if (m_readPos % 8 == 0) {
        const std::size_t start = m_readPos / 8;
        if (start + out.size() > m_buffer.size()) {
            m_overflow = true;
            std::fill(out.begin(), out.end(), std::uint8_t{0});
            m_readPos = m_buffer.size() * 8;
            return;
        }
        std::memcpy(out.data(), m_buffer.data() + start, out.size());
        m_readPos += out.size() * 8;
    } else {
        for (auto& b : out) b = static_cast<std::uint8_t>(ReadBits(8));
    }
}

std::uint8_t BitStream::ReadUint8() {
    std::uint8_t v = 0;
    ReadBytes({&v, 1});
    return v;
}

std::uint16_t BitStream::ReadUint16() {
    std::uint8_t raw[2] = {};
    ReadBytes(raw);
    return static_cast<std::uint16_t>(raw[0] | (raw[1] << 8));
}

std::uint32_t BitStream::ReadUint32() {
    std::uint8_t raw[4] = {};
    ReadBytes(raw);
    return raw[0] | (raw[1] << 8) | (raw[2] << 16) |
           (static_cast<std::uint32_t>(raw[3]) << 24);
}

float BitStream::ReadFloat() {
    const std::uint32_t raw = ReadUint32();
    float v;
    std::memcpy(&v, &raw, 4);
    return v;
}

std::uint32_t BitStream::ReadCompressedUnsigned(int sizeBits) {
    const int numBytes = sizeBits >> 3;
    std::uint8_t out[4] = {};
    int currentByte = numBytes - 1;
    while (currentByte > 0) {
        if (ReadBool()) {
            out[currentByte] = 0x00;
            --currentByte;
        } else {
            for (int i = 0; i <= currentByte; ++i)
                out[i] = static_cast<std::uint8_t>(ReadBits(8));
            std::uint32_t v = 0;
            for (int i = numBytes - 1; i >= 0; --i) v = (v << 8) | out[i];
            return v;
        }
    }
    if (ReadBool())
        out[0] = static_cast<std::uint8_t>(ReadBits(4));
    else
        out[0] = static_cast<std::uint8_t>(ReadBits(8));
    std::uint32_t v = 0;
    for (int i = numBytes - 1; i >= 0; --i) v = (v << 8) | out[i];
    return v;
}

void BitStream::AlignReadToByte() {
    if (m_readPos % 8 != 0) m_readPos += 8 - (m_readPos % 8);
}

} // namespace vc::net
