#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <shared/net/bitstream.h>

// Проволочный формат RakNet (версия SA-MP/PyRakBR): датаграммы, заголовки
// внутренних пакетов, ACK-диапазоны. См. docs/NETWORK-PROTOCOL.md.
//
// Осознанное отличие от PyRakBR/SA-MP: тела пакетов НЕ обфусцируются их
// "security"-слоем (checksum_byte в заголовке отсутствует) — этот стек
// предназначен для связи НАШ клиент <-> НАШ сервер, а не для чужих серверов SA-MP/BR.
namespace vc::net {

// Значения совпадают с PacketReliability в PyRakBR / RakNet.
enum class Reliability : std::uint8_t {
    Unreliable = 0,
    UnreliableSequenced = 1,
    Reliable = 2,
    ReliableOrdered = 3,
    ReliableSequenced = 4,
};

[[nodiscard]] constexpr bool IsReliable(Reliability r) {
    return r == Reliability::Reliable || r == Reliability::ReliableOrdered ||
           r == Reliability::ReliableSequenced;
}

[[nodiscard]] constexpr bool HasOrderingMetadata(Reliability r) {
    return r == Reliability::UnreliableSequenced ||
           r == Reliability::ReliableOrdered ||
           r == Reliability::ReliableSequenced;
}

// Один внутренний пакет RakNet внутри датаграммы.
struct InternalPacket {
    std::uint16_t messageNumber = 0;
    Reliability reliability = Reliability::Unreliable;
    std::optional<std::uint16_t> orderingIndex;
    std::optional<std::uint8_t> orderingChannel;
    // Фрагментация (split) — пакет, не влезающий в датаграмму.
    std::optional<std::uint16_t> splitId;
    std::uint32_t splitIndex = 0;
    std::uint32_t splitCount = 0;
    std::vector<std::uint8_t> payload; // тело: уже наши игровые пакеты (shared/protocol)

    [[nodiscard]] bool HasSplit() const { return splitId.has_value(); }
};

using AckRange = std::pair<std::uint16_t, std::uint16_t>; // [min, max]

struct Datagram {
    std::vector<AckRange> acks;             // подтверждения чужих надёжных пакетов
    std::vector<InternalPacket> packets;    // может быть пустым => чистая ACK-датаграмма
};

// Сериализация в точности по core/protocol.py PyRakBR (_build_header/_parse_one_packet):
//  датаграмма: [has_acks:1][ACK-диапазоны?][пакет...]
//  пакет:      [message_number:16 LE][reliability:3]
//              [ordering_index:16 LE][~ordering_channel:4]   (если есть ordering)
//              [has_split:1][split_id:16 LE][split_index:comp32][split_count:comp32]?
//              [bitlen:comp16][align][тело]
[[nodiscard]] std::vector<std::uint8_t> EncodeDatagram(const Datagram& datagram);
[[nodiscard]] std::optional<Datagram> DecodeDatagram(std::span<const std::uint8_t> bytes);

// Сжать список номеров пакетов в ACK-диапазоны ([10,11,12,20] -> [(10,12),(20,20)]).
[[nodiscard]] std::vector<AckRange> BuildAckRanges(std::vector<std::uint16_t> messageNumbers);

} // namespace vc::net
