// Юнит-тесты сетевого слоя: BitStream, RakNet-фрейминг, offline ping/pong, SA-MP query.
// Без фреймворка — простой assert-подсчёт, ненулевой код возврата при провале.
#include <cmath>
#include <cstdio>

#include <shared/net/bitstream.h>
#include <shared/net/offline.h>
#include <shared/net/peer.h>
#include <shared/net/raknet.h>
#include <shared/math/quaternion.h>
#include <shared/protocol/codec.h>
#include <shared/version.h>

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

void TestReliablePeer() {
    vc::net::ReliablePeer a, b;
    const auto msg1 = a.BuildMessage(std::vector<std::uint8_t>{1, 2, 3}, vc::net::Reliability::ReliableOrdered, 0, 1000);
    CHECK(a.UnackedCount() == 1);
    const auto got1 = b.HandleDatagram(msg1);
    CHECK(got1.payloads.size() == 1 && got1.payloads[0] == std::vector<std::uint8_t>({1, 2, 3}));
    // Дубликат той же датаграммы второй раз не выдаётся
    CHECK(b.HandleDatagram(msg1).payloads.empty());
    // ACK снимает сообщение с переотправки
    auto ack = b.BuildAckDatagram();
    CHECK(ack.has_value());
    a.HandleDatagram(*ack);
    CHECK(a.UnackedCount() == 0);
    CHECK(a.CollectResends(100000).empty());

    // Ordered: пакеты пришли 2,0,1 -> выдача строго 0,1,2
    vc::net::ReliablePeer c, d;
    const auto m0 = c.BuildMessage(std::vector<std::uint8_t>{10}, vc::net::Reliability::ReliableOrdered, 0, 0);
    const auto m1 = c.BuildMessage(std::vector<std::uint8_t>{11}, vc::net::Reliability::ReliableOrdered, 0, 0);
    const auto m2 = c.BuildMessage(std::vector<std::uint8_t>{12}, vc::net::Reliability::ReliableOrdered, 0, 0);
    CHECK(d.HandleDatagram(m2).payloads.empty());
    auto got0 = d.HandleDatagram(m0);
    CHECK(got0.payloads.size() == 1 && got0.payloads[0][0] == 10);
    auto got12 = d.HandleDatagram(m1);
    CHECK(got12.payloads.size() == 2 && got12.payloads[0][0] == 11 && got12.payloads[1][0] == 12);

    // Sequenced: устаревший пакет отбрасывается
    vc::net::ReliablePeer e, f;
    const auto s0 = e.BuildMessage(std::vector<std::uint8_t>{20}, vc::net::Reliability::UnreliableSequenced, 1, 0);
    const auto s1 = e.BuildMessage(std::vector<std::uint8_t>{21}, vc::net::Reliability::UnreliableSequenced, 1, 0);
    CHECK(f.HandleDatagram(s1).payloads.size() == 1);
    CHECK(f.HandleDatagram(s0).payloads.empty());

    // Переотправка по таймауту
    vc::net::ReliablePeer g;
    g.BuildMessage(std::vector<std::uint8_t>{7}, vc::net::Reliability::Reliable, 0, 1000);
    CHECK(g.CollectResends(1200).empty());
    CHECK(g.CollectResends(1600).size() == 1);
}

void TestCodec() {
    const auto req = vc::protocol::BuildConnectionRequest({vc::PROTOCOL_VERSION, "Tommy"});
    const auto parsedReq = vc::protocol::ParseConnectionRequest(req);
    CHECK(parsedReq && parsedReq->protocolVersion == vc::PROTOCOL_VERSION &&
          parsedReq->nick == "Tommy");

    const auto acc = vc::protocol::BuildConnectionAccepted({5, 100});
    const auto parsedAcc = vc::protocol::ParseConnectionAccepted(acc);
    CHECK(parsedAcc && parsedAcc->playerId == 5 && parsedAcc->maxPlayers == 100);

    const auto msg = vc::protocol::BuildServerMessage("Привет, Вайс-Сити!");
    const auto parsedMsg = vc::protocol::ParseServerMessage(msg);
    CHECK(parsedMsg && *parsedMsg == "Привет, Вайс-Сити!");

    const auto sync = vc::protocol::BuildOnFootSync({{-378.5f, -589.1f, 25.32f}, 73, 26});
    const auto parsedSync = vc::protocol::ParseOnFootSync(sync);
    CHECK(parsedSync && parsedSync->position.x == -378.5f &&
          parsedSync->position.y == -589.1f && parsedSync->health == 73 &&
          parsedSync->weapon == 26);

    const auto streamIn =
        vc::protocol::BuildPlayerStreamIn({3, 12, "Ken", {1.0f, 2.0f, 3.0f}});
    const auto parsedIn = vc::protocol::ParsePlayerStreamIn(streamIn);
    CHECK(parsedIn && parsedIn->playerId == 3 && parsedIn->skin == 12 &&
          parsedIn->nick == "Ken" && parsedIn->position.z == 3.0f);

    const auto tp = vc::protocol::BuildTeleport({{-378.5f, -589.1f, 25.6f}, 0.25f});
    const auto parsedTp = vc::protocol::ParseTeleport(tp);
    CHECK(parsedTp && parsedTp->position.y == -589.1f && parsedTp->heading == 0.25f);

    const auto vehIn =
        vc::protocol::BuildVehicleStreamIn({5, 141, {10.0f, -20.0f, 12.5f}, 1.5f, 432});
    const auto parsedVeh = vc::protocol::ParseVehicleStreamIn(vehIn);
    CHECK(parsedVeh && parsedVeh->vehicleId == 5 && parsedVeh->model == 141 &&
          parsedVeh->position.z == 12.5f && parsedVeh->heading == 1.5f &&
          parsedVeh->health == 432);
    const auto parsedVehOut =
        vc::protocol::ParseVehicleStreamOut(vc::protocol::BuildVehicleStreamOut(5));
    CHECK(parsedVehOut && *parsedVehOut == 5);

    const auto posePkt = vc::protocol::BuildInVehicleSync(
        {7, {1.5f, -2.5f, 3.5f}, 0.1f, 0.2f, 0.3f, 0.9f, 777});
    const auto pose = vc::protocol::ParseInVehicleSync(posePkt);
    CHECK(pose && pose->vehicleId == 7 && pose->position.y == -2.5f &&
          pose->quatZ == 0.3f && pose->quatW == 0.9f && pose->health == 777);
    const auto pose2 = vc::protocol::ParseVehicleSyncUpdate(
        vc::protocol::BuildVehicleSyncUpdate({7, {1.5f, -2.5f, 3.5f}, 0.0f, 0.0f, 0.0f, 1.0f}));
    CHECK(pose2 && pose2->vehicleId == 7 && pose2->quatW == 1.0f &&
          pose2->health == 1000); // дефолт агрегата
    const auto enter = vc::protocol::ParseEnterVehicle(
        vc::protocol::BuildEnterVehicle({42, 0}));
    CHECK(enter && enter->vehicleId == 42 && enter->seat == 0);
    const auto occ = vc::protocol::ParseVehicleOccupant(
        vc::protocol::BuildVehicleOccupant({7, 42, 2}));
    CHECK(occ && occ->vehicleId == 7 && occ->playerId == 42 && occ->seat == 2);

    // Кватернион: базис -> quat -> базис (round-trip) для поворота на 30° по Z.
    {
        const float a = 0.5235988f; // 30°
        const float c = std::cos(a), s = std::sin(a);
        const auto q = vc::QuatFromBasis({c, s, 0.0f}, {-s, c, 0.0f}, {0.0f, 0.0f, 1.0f});
        vc::Vector3 r, f, u;
        vc::QuatToBasis(q, r, f, u);
        CHECK(std::fabs(r.x - c) < 1e-5f && std::fabs(r.y - s) < 1e-5f &&
              std::fabs(f.x + s) < 1e-5f && std::fabs(f.y - c) < 1e-5f &&
              std::fabs(u.z - 1.0f) < 1e-5f);
    }

    const auto streamOut = vc::protocol::BuildPlayerStreamOut(3);
    CHECK(vc::protocol::ParsePlayerStreamOut(streamOut) == 3);

    const auto upd = vc::protocol::BuildPlayerSyncUpdate({3, {-10.5f, 20.25f, 7.0f}, 42, 25});
    const auto parsedUpd = vc::protocol::ParsePlayerSyncUpdate(upd);
    CHECK(parsedUpd && parsedUpd->playerId == 3 && parsedUpd->position.x == -10.5f &&
          parsedUpd->health == 42 && parsedUpd->weapon == 25);

    // Оружие и урон (протокол v9).
    const auto gw = vc::protocol::BuildGiveWeapon({26, 500});
    const auto parsedGw = vc::protocol::ParseGiveWeapon(gw);
    CHECK(parsedGw && parsedGw->weapon == 26 && parsedGw->ammo == 500);
    const auto pd = vc::protocol::BuildPlayerDamage({7, 33.5f, 25});
    const auto parsedPd = vc::protocol::ParsePlayerDamage(pd);
    CHECK(parsedPd && parsedPd->victimId == 7 && parsedPd->amount == 33.5f &&
          parsedPd->weapon == 25);
    const auto td = vc::protocol::BuildTakeDamage({3, 12.0f, 17});
    const auto parsedTd = vc::protocol::ParseTakeDamage(td);
    CHECK(parsedTd && parsedTd->attackerId == 3 && parsedTd->amount == 12.0f &&
          parsedTd->weapon == 17);

    // Диалоги SA-MP-стиля (протокол v10).
    const auto sd = vc::protocol::BuildShowDialog(
        {7, static_cast<std::uint8_t>(vc::protocol::DialogStyle::TabList), "Меню",
         "Раз\nДва\tКолонка", "OK", "Отмена"});
    const auto parsedSd = vc::protocol::ParseShowDialog(sd);
    CHECK(parsedSd && parsedSd->dialogId == 7 && parsedSd->style == 4 &&
          parsedSd->caption == "Меню" && parsedSd->info == "Раз\nДва\tКолонка" &&
          parsedSd->button1 == "OK" && parsedSd->button2 == "Отмена");
    const auto dr = vc::protocol::BuildDialogResponse({7, 1, 3, "привет"});
    const auto parsedDr = vc::protocol::ParseDialogResponse(dr);
    CHECK(parsedDr && parsedDr->dialogId == 7 && parsedDr->response == 1 &&
          parsedDr->listItem == 3 && parsedDr->inputText == "привет");
    const auto dr2 = vc::protocol::ParseDialogResponse(
        vc::protocol::BuildDialogResponse({9, 0, -1, ""}));
    CHECK(dr2 && dr2->response == 0 && dr2->listItem == -1 && dr2->inputText.empty());
}

} // namespace

int main() {
    TestBitStream();
    TestRakNetFraming();
    TestOfflineAndQuery();
    TestReliablePeer();
    TestCodec();
    if (g_failures == 0)
        std::printf("OK: все тесты пройдены\n");
    else
        std::printf("FAILED: провалов: %d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
