#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdlib>

#include <shared/protocol/codec.h>
#include <shared/version.h>

#include "net_client.h"

namespace vc::network {

NetClient& NetClient::Instance() {
    static NetClient instance;
    return instance;
}

void NetClient::Log(const std::string& text) {
    OutputDebugStringA(text.c_str());
    if (m_log) {
        std::fputs(text.c_str(), m_log);
        std::fflush(m_log);
    }
}

void NetClient::Init(FILE* log, const std::string& iniPath) {
    m_log = log;
    char ip[64] = "127.0.0.1";
    char nick[32] = "Player";
    const int port = static_cast<int>(
        GetPrivateProfileIntA("Server", "Port", 7777, iniPath.c_str()));
    GetPrivateProfileStringA("Server", "IP", ip, ip, sizeof(ip), iniPath.c_str());
    GetPrivateProfileStringA("Server", "Nick", nick, nick, sizeof(nick), iniPath.c_str());
    m_server = {ip, static_cast<std::uint16_t>(port)};
    m_nick = nick;

    if (!m_socket.IsValid() || !m_socket.Bind(0)) {
        m_state = State::Failed;
        m_status = "не удалось создать UDP-сокет";
        Log("net: " + m_status + "\n");
        return;
    }
    m_state = State::Connecting;
    m_status = "подключение к " + m_server.ip + ":" + std::to_string(m_server.port) + "...";
    Log("net: " + m_status + " ник=" + m_nick + "\n");
}

void NetClient::SendPayload(const std::vector<std::uint8_t>& payload,
                            vc::net::Reliability reliability, std::uint8_t channel,
                            std::uint64_t nowMs) {
    m_socket.SendTo(m_server, m_peer.BuildMessage(payload, reliability, channel, nowMs));
}

void NetClient::SendChat(const std::string& text) {
    if (m_state != State::Connected) return;
    SendPayload(vc::protocol::BuildChatMessage(text), vc::net::Reliability::ReliableOrdered, 0,
                GetTickCount64());
}

void NetClient::HandlePayload(const std::vector<std::uint8_t>& payload) {
    if (auto accepted = vc::protocol::ParseConnectionAccepted(payload)) {
        m_state = State::Connected;
        m_playerId = accepted->playerId;
        m_status = "подключено к " + m_server.ip + ":" + std::to_string(m_server.port) +
                   " (id " + std::to_string(m_playerId) + ")";
        Log("net: " + m_status + "\n");
    } else if (auto rejected = vc::protocol::ParseConnectionRejected(payload)) {
        m_state = State::Failed;
        m_status = "сервер отказал: " + *rejected;
        Log("net: " + m_status + "\n");
    } else if (auto message = vc::protocol::ParseServerMessage(payload)) {
        m_chatLines.push_back(*message);
        if (m_chatLines.size() > 8) m_chatLines.erase(m_chatLines.begin());
        Log("net[chat]: " + *message + "\n");
    }
}

void NetClient::Tick(std::uint64_t nowMs, const std::optional<vc::Vector3>& playerPosition) {
    if (m_state == State::Disabled || m_state == State::Failed) return;

    if (m_state == State::Connecting && !m_requestSent) {
        m_requestSent = true;
        m_connectStartedMs = nowMs;
        SendPayload(vc::protocol::BuildConnectionRequest({vc::PROTOCOL_VERSION, m_nick}),
                    vc::net::Reliability::ReliableOrdered, 0, nowMs);
    }
    if (m_state == State::Connecting && nowMs - m_connectStartedMs > 10000) {
        m_state = State::Failed;
        m_status = "сервер " + m_server.ip + ":" + std::to_string(m_server.port) +
                   " не отвечает (таймаут)";
        Log("net: " + m_status + "\n");
        return;
    }

    for (int i = 0; i < 32; ++i) {
        auto received = m_socket.Receive(0);
        if (!received) break;
        const auto incoming = m_peer.HandleDatagram(received->data);
        for (const auto& payload : incoming.payloads) HandlePayload(payload);
    }
    if (auto ack = m_peer.BuildAckDatagram()) m_socket.SendTo(m_server, *ack);
    for (const auto& resend : m_peer.CollectResends(nowMs)) m_socket.SendTo(m_server, resend);

    if (m_state == State::Connected && playerPosition && nowMs - m_lastSyncMs >= 100) {
        m_lastSyncMs = nowMs;
        SendPayload(vc::protocol::BuildOnFootSync(*playerPosition),
                    vc::net::Reliability::UnreliableSequenced, 1, nowMs);
    }
}

} // namespace vc::network
