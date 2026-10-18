// Юнит-тесты сетевого слоя: BitStream, RakNet-фрейминг, offline ping/pong, SA-MP query.
// Без фреймворка — простой assert-подсчёт, ненулевой код возврата при провале.
#include <cmath>
#include <cstdio>

#include <shared/net/bitstream.h>
#include <shared/net/offline.h>
#include <shared/net/raknet.h>

namespace {
int g_failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
            ++g_failures;                                                      \
        }                                                                      \
    } while (false)

void TestBitStream() {
    vc::net::BitStream bs;
    bs.WriteBool(true);
    bs.WriteBits(0b101, 3);
    bs.WriteUint16(0xBEEF);
    bs.WriteUint32(0xDEADBEEF);
    bs.WriteFloat(3.25f);
    bs.WriteCompressedUnsigned(0, 16);
    bs.WriteCompressedUnsigned(5, 16);
    bs.WriteCompressedUnsigned(300, 16);
    bs.WriteCompressedUnsigned(70000 > 0xFFFF ? 0x1'2345 : 0, 32);
    bs.WriteCompressedUnsigned(0xFFFFFFF0u, 32);
    bs.AlignWriteToByte();

    vc::net::BitStream in(bs.Data());
    CHECK(in.ReadBool() == true);
    CHECK(in.ReadBits(3) == 0b101);
    CHECK(in.ReadUint16() == 0xBEEF);
    CHECK(in.ReadUint32() == 0xDEADBEEF);
    CHECK(std::fabs(in.ReadFloat() - 3.25f) < 1e-6f);
    CHECK(in.ReadCompressedUnsigned(16) == 0);
    CHECK(in.ReadCompressedUnsigned(16) == 5);
    CHECK(in.ReadCompressedUnsigned(16) == 300);
    CHECK(in.ReadCompressedUnsigned(32) == 0x12345);
    CHECK(in.ReadCompressedUnsigned(32) == 0xFFFFFFF0u);
    CHECK(!in.Overflowed());
}

void TestRakNetFraming() {
    vc::net::Datagram dg;
    dg.acks = vc::net::BuildAckRanges({10, 11, 12, 20, 25, 26});
    CHECK(dg.acks.size() == 3);
    CHECK(dg.acks[0] == vc::net::AckRange(10, 12));
    CHECK(dg.acks[1] == vc::net::AckRange(20, 20));

    vc::net::InternalPacket p1;
    p1.messageNumber = 42;
    p1.reliability = vc::net::Reliability::ReliableOrdered;
    p1.orderingIndex = 7;
    p1.orderingChannel = 3;
    p1.payload = {0x0B, 0x19, 'h', 'i'};
    dg.packets.push_back(p1);

    vc::net::InternalPacket p2;
    p2.messageNumber = 43;
    p2.reliability = vc::net::Reliability::UnreliableSequenced;
    p2.orderingIndex = 9;
    p2.orderingChannel = 15;
    p2.payload = {1, 2, 3};
    dg.packets.push_back(p2);

    vc::net::InternalPacket p3;
    p3.messageNumber = 44;
    p3.reliability = vc::net::Reliability::Reliable;
    p3.splitId = 5;
    p3.splitIndex = 1;
    p3.splitCount = 4;
    p3.payload = {9, 9};
    dg.packets.push_back(p3);

    const auto bytes = vc::net::EncodeDatagram(dg);
    auto decoded = vc::net::DecodeDatagram(bytes);
    CHECK(decoded.has_value());
    if (!decoded) return;
    CHECK(decoded->acks == dg.acks);
    CHECK(decoded->packets.size() == 3);
    if (decoded->packets.size() != 3) return;
    const auto& q1 = decoded->packets[0];
    CHECK(q1.messageNumber == 42);
    CHECK(q1.reliability == vc::net::Reliability::ReliableOrdered);
    CHECK(q1.orderingIndex == 7);
    CHECK(q1.orderingChannel == 3);
    CHECK(q1.payload == p1.payload);
    const auto& q2 = decoded->packets[1];
    CHECK(q2.orderingChannel == 15);
    CHECK(q2.payload == p2.payload);
    const auto& q3 = decoded->packets[2];
    CHECK(q3.HasSplit() && *q3.splitId == 5 && q3.splitIndex == 1 && q3.splitCount == 4);
    CHECK(q3.payload == p3.payload);

    // Чистая ACK-датаграмма
    vc::net::Datagram ackOnly;
    ackOnly.acks = {{1, 1}, {5, 9}};
    auto ackDecoded = vc::net::DecodeDatagram(vc::net::EncodeDatagram(ackOnly));
    CHECK(ackDecoded.has_value() && ackDecoded->packets.empty() &&
          ackDecoded->acks == ackOnly.acks);

    // Мусор не должен декодироваться в пакеты из ничего
    CHECK(!vc::net::DecodeDatagram({}).has_value());
}

void TestOfflineAndQuery() {
    const auto ping = vc::net::BuildUnconnectedPing(12345678, 0xABCDEF);
    CHECK(vc::net::IsUnconnectedPing(ping));
    CHECK(vc::net::ParsePingTime(ping) == 12345678);

    const auto pong = vc::net::BuildUnconnectedPong(12345678, 777, "Test Server;1;100");
    auto parsed = vc::net::ParseUnconnectedPong(pong);
    CHECK(parsed.has_value());
    if (parsed) {
        CHECK(parsed->timeMs == 12345678);
        CHECK(parsed->serverGuid == 777);
        CHECK(parsed->motd == "Test Server;1;100");
    }

    vc::net::SampServerInfo info;
    info.players = 13;
    info.maxPlayers = 100;
    info.hostname = "VC Server";
    info.gamemode = "freeroam";
    info.language = "Russian";
    const vc::net::Endpoint ep{"127.0.0.1", 7777};
    const auto reply = vc::net::BuildSampInfoReply(ep, info);
    auto parsedInfo = vc::net::ParseSampInfoReply(reply);
    CHECK(parsedInfo.has_value());
    if (parsedInfo) {
        CHECK(parsedInfo->players == 13);
        CHECK(parsedInfo->hostname == "VC Server");
        CHECK(parsedInfo->gamemode == "freeroam");
    }
    const auto query = vc::net::BuildSampQuery(ep, vc::net::kSampQueryInfo);
    CHECK(query.size() == 11);
    CHECK(vc::net::IsSampQuery(query, vc::net::kSampQueryInfo));
}

} // namespace

int main() {
    TestBitStream();
    TestRakNetFraming();
    TestOfflineAndQuery();
    if (g_failures == 0)
        std::printf("OK: все тесты пройдены\n");
    else
        std::printf("FAILED: провалов: %d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
