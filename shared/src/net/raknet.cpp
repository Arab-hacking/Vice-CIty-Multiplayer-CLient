#include <shared/net/raknet.h>

#include <algorithm>

namespace vc::net {
namespace {

void WriteRangeList(BitStream& bs, const std::vector<AckRange>& ranges) {
    bs.WriteCompressedUnsigned(static_cast<std::uint32_t>(ranges.size()), 16);
    for (const auto& [lo, hi] : ranges) {
        bs.WriteBool(lo == hi);
        bs.WriteUint16(lo);
        if (lo != hi) bs.WriteUint16(hi);
    }
}

std::vector<AckRange> ReadRangeList(BitStream& bs) {
    std::vector<AckRange> ranges;
    const std::uint32_t count = bs.ReadCompressedUnsigned(16);
    ranges.reserve(count);
    for (std::uint32_t i = 0; i < count && !bs.Overflowed(); ++i) {
        const bool single = bs.ReadBool();
        const std::uint16_t lo = bs.ReadUint16();
        const std::uint16_t hi = single ? lo : bs.ReadUint16();
        ranges.emplace_back(lo, hi);
    }
    return ranges;
}

void WritePacket(BitStream& bs, const InternalPacket& p) {
    bs.WriteUint16(p.messageNumber);
    bs.WriteBits(static_cast<std::uint64_t>(p.reliability) & 0x7u, 3);
    if (HasOrderingMetadata(p.reliability)) {
        bs.WriteUint16(p.orderingIndex.value_or(0));
        // Канал пишется инвертированным (~channel & 0xF) — каприз RakNet, повторяем 1-в-1.
        bs.WriteBits(static_cast<std::uint64_t>(~p.orderingChannel.value_or(0)) & 0xFu, 4);
    }
    bs.WriteBool(p.HasSplit());
    if (p.HasSplit()) {
        bs.WriteUint16(*p.splitId);
        bs.WriteCompressedUnsigned(p.splitIndex, 32);
        bs.WriteCompressedUnsigned(p.splitCount, 32);
    }
    bs.WriteCompressedUnsigned(static_cast<std::uint32_t>(p.payload.size() * 8), 16);
    bs.AlignWriteToByte();
    bs.WriteBytes(p.payload);
}

std::optional<InternalPacket> ReadPacket(BitStream& bs) {
    InternalPacket p;
    p.messageNumber = bs.ReadUint16();
    p.reliability = static_cast<Reliability>(bs.ReadBits(3) & 0x7u);
    if (HasOrderingMetadata(p.reliability)) {
        p.orderingIndex = bs.ReadUint16();
        p.orderingChannel = static_cast<std::uint8_t>(~bs.ReadBits(4)) & 0xFu;
    }
    if (bs.ReadBool()) {
        p.splitId = bs.ReadUint16();
        p.splitIndex = bs.ReadCompressedUnsigned(32);
        p.splitCount = bs.ReadCompressedUnsigned(32);
    }
    const std::uint32_t bitlen = bs.ReadCompressedUnsigned(16);
    const std::size_t bodyBytes = (bitlen + 7) / 8;
    bs.AlignReadToByte();
    if (bs.Overflowed() || bodyBytes * 8 > bs.RemainingBits()) return std::nullopt;
    p.payload.resize(bodyBytes);
    bs.ReadBytes(p.payload);
    if (bs.Overflowed()) return std::nullopt;
    return p;
}

} // namespace

std::vector<std::uint8_t> EncodeDatagram(const Datagram& datagram) {
    BitStream bs;
    bs.WriteBool(!datagram.acks.empty());
    if (!datagram.acks.empty()) WriteRangeList(bs, datagram.acks);
    for (const InternalPacket& p : datagram.packets) WritePacket(bs, p);
    bs.AlignWriteToByte();
    std::vector<std::uint8_t> out = bs.Data();
    out.resize((bs.BitLength() + 7) / 8);
    return out;
}

std::optional<Datagram> DecodeDatagram(std::span<const std::uint8_t> bytes) {
    if (bytes.empty()) return std::nullopt;
    BitStream bs(bytes);
    Datagram result;
    if (bs.ReadBool()) result.acks = ReadRangeList(bs);
    if (bs.Overflowed()) return std::nullopt;
    // Пакеты идут друг за другом, пока не кончатся байты. Хвостовой padding < 1 байта
    // после AlignWriteToByte не даёт остатка >= минимального заголовка, поэтому
    // требуем хотя бы 20 бит (16 номер + 3 reliability + 1 split) на очередной пакет.
    while (bs.RemainingBits() >= 20) {
        std::optional<InternalPacket> p = ReadPacket(bs);
        if (!p) return std::nullopt;
        result.packets.push_back(std::move(*p));
    }
    return result;
}

std::vector<AckRange> BuildAckRanges(std::vector<std::uint16_t> messageNumbers) {
    std::vector<AckRange> ranges;
    if (messageNumbers.empty()) return ranges;
    std::sort(messageNumbers.begin(), messageNumbers.end());
    messageNumbers.erase(std::unique(messageNumbers.begin(), messageNumbers.end()),
                          messageNumbers.end());
    std::uint16_t start = messageNumbers.front(), end = start;
    for (std::size_t i = 1; i < messageNumbers.size(); ++i) {
        if (static_cast<std::uint16_t>(messageNumbers[i]) ==
            static_cast<std::uint16_t>(end + 1)) {
            end = messageNumbers[i];
        } else {
            ranges.emplace_back(start, end);
            start = end = messageNumbers[i];
        }
    }
    ranges.emplace_back(start, end);
    return ranges;
}

} // namespace vc::net
