#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <unordered_map>
#include <vector>

#include <shared/net/raknet.h>

// Надёжная доставка поверх RakNet-фрейминга для ОДНОГО удалённого собеседника:
//  - нумерация пакетов, ACK и переотправка неподтверждённых;
//  - дедупликация надёжных пакетов;
//  - ordered-доставка по каналам, отбрасывание устаревших sequenced.
// Транспорт (сокет) сюда не зашит: методы возвращают готовые датаграммы,
// отправку делает вызывающий код — так слой легко тестировать без сети.
namespace vc::net {

class ReliablePeer {
public:
    // Собрать датаграмму с одним сообщением. Надёжные запоминаются для переотправки.
    [[nodiscard]] std::vector<std::uint8_t> BuildMessage(std::span<const std::uint8_t> payload,
                                                         Reliability reliability,
                                                         std::uint8_t orderingChannel,
                                                         std::uint64_t nowMs);

    struct Incoming {
        // Тела сообщений в порядке доставки (для ordered — строго по ordering_index).
        std::vector<std::vector<std::uint8_t>> payloads;
    };

    // Разобрать входящую датаграмму: обработать ACK, отклонить дубли/устаревшие,
    // вернуть готовые к выдаче сообщения.
    [[nodiscard]] Incoming HandleDatagram(std::span<const std::uint8_t> datagram);

    // Датаграмма только с ACK на полученные надёжные пакеты (если есть что подтверждать).
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> BuildAckDatagram();

    // Датаграммы надёжных сообщений, у которых истёк таймаут подтверждения.
    [[nodiscard]] std::vector<std::vector<std::uint8_t>> CollectResends(std::uint64_t nowMs,
                                                                         std::uint64_t timeoutMs = 500,
                                                                         int maxResends = 12);

    [[nodiscard]] std::size_t UnackedCount() const { return m_unacked.size(); }

private:
    struct Pending {
        std::vector<std::uint8_t> datagram;
        std::uint64_t lastSentMs = 0;
        int resends = 0;
    };

    static bool SerialGreater(std::uint16_t a, std::uint16_t b) {
        return static_cast<std::int16_t>(a - b) > 0;
    }

    std::uint16_t m_nextMessageNumber = 0;
    std::uint16_t m_sendOrdering[16] = {};
    std::unordered_map<std::uint16_t, Pending> m_unacked;

    std::set<std::uint16_t> m_pendingAcks;
    std::set<std::uint16_t> m_seenReliable;
    std::deque<std::uint16_t> m_seenOrder;

    bool m_hasSequencedHighest[16] = {};
    std::uint16_t m_sequencedHighest[16] = {};
    std::uint16_t m_orderedNext[16] = {};
    std::map<std::uint16_t, std::vector<std::uint8_t>> m_orderedBuffer[16];
};

} // namespace vc::net
