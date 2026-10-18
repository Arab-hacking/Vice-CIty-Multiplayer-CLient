#include <shared/net/peer.h>

namespace vc::net {

std::vector<std::uint8_t> ReliablePeer::BuildMessage(std::span<const std::uint8_t> payload,
                                                     Reliability reliability,
                                                     std::uint8_t orderingChannel,
                                                     std::uint64_t nowMs) {
    InternalPacket packet;
    packet.messageNumber = m_nextMessageNumber++;
    packet.reliability = reliability;
    packet.payload.assign(payload.begin(), payload.end());
    if (HasOrderingMetadata(reliability)) {
        const std::uint8_t ch = orderingChannel & 0x0F;
        packet.orderingChannel = ch;
        packet.orderingIndex = m_sendOrdering[ch]++;
    }
    Datagram datagram;
    datagram.packets.push_back(packet);
    std::vector<std::uint8_t> bytes = EncodeDatagram(datagram);
    if (IsReliable(reliability))
        m_unacked[packet.messageNumber] = Pending{bytes, nowMs, 0};
    return bytes;
}

ReliablePeer::Incoming ReliablePeer::HandleDatagram(std::span<const std::uint8_t> bytes) {
    Incoming result;
    const auto datagram = DecodeDatagram(bytes);
    if (!datagram) return result;

    for (const auto& [lo, hi] : datagram->acks)
        for (std::uint32_t n = lo; n <= hi; ++n)
            m_unacked.erase(static_cast<std::uint16_t>(n));

    for (const InternalPacket& packet : datagram->packets) {
        if (IsReliable(packet.reliability)) {
            m_pendingAcks.insert(packet.messageNumber);
            if (m_seenReliable.contains(packet.messageNumber)) continue; // дубликат
            m_seenReliable.insert(packet.messageNumber);
            m_seenOrder.push_back(packet.messageNumber);
            if (m_seenOrder.size() > 4096) {
                m_seenReliable.erase(m_seenOrder.front());
                m_seenOrder.pop_front();
            }
        }

        const std::uint8_t ch = packet.orderingChannel.value_or(0) & 0x0F;
        switch (packet.reliability) {
        case Reliability::Unreliable:
        case Reliability::Reliable:
            result.payloads.push_back(packet.payload);
            break;
        case Reliability::UnreliableSequenced:
        case Reliability::ReliableSequenced: {
            const std::uint16_t idx = packet.orderingIndex.value_or(0);
            if (!m_hasSequencedHighest[ch] || SerialGreater(idx, m_sequencedHighest[ch])) {
                m_hasSequencedHighest[ch] = true;
                m_sequencedHighest[ch] = idx;
                result.payloads.push_back(packet.payload);
            }
            break;
        }
        case Reliability::ReliableOrdered: {
            const std::uint16_t idx = packet.orderingIndex.value_or(0);
            if (idx == m_orderedNext[ch]) {
                result.payloads.push_back(packet.payload);
                ++m_orderedNext[ch];
                while (true) {
                    auto it = m_orderedBuffer[ch].find(m_orderedNext[ch]);
                    if (it == m_orderedBuffer[ch].end()) break;
                    result.payloads.push_back(std::move(it->second));
                    m_orderedBuffer[ch].erase(it);
                    ++m_orderedNext[ch];
                }
            } else if (SerialGreater(idx, m_orderedNext[ch])) {
                if (m_orderedBuffer[ch].size() < 512)
                    m_orderedBuffer[ch][idx] = packet.payload;
            }
            break;
        }
        }
    }
    return result;
}

std::optional<std::vector<std::uint8_t>> ReliablePeer::BuildAckDatagram() {
    if (m_pendingAcks.empty()) return std::nullopt;
    Datagram datagram;
    datagram.acks = BuildAckRanges({m_pendingAcks.begin(), m_pendingAcks.end()});
    m_pendingAcks.clear();
    return EncodeDatagram(datagram);
}

std::vector<std::vector<std::uint8_t>> ReliablePeer::CollectResends(std::uint64_t nowMs,
                                                                    std::uint64_t timeoutMs,
                                                                    int maxResends) {
    std::vector<std::vector<std::uint8_t>> out;
    for (auto it = m_unacked.begin(); it != m_unacked.end();) {
        Pending& pending = it->second;
        if (nowMs - pending.lastSentMs >= timeoutMs) {
            if (pending.resends >= maxResends) {
                it = m_unacked.erase(it);
                continue;
            }
            ++pending.resends;
            pending.lastSentMs = nowMs;
            out.push_back(pending.datagram);
        }
        ++it;
    }
    return out;
}

} // namespace vc::net
