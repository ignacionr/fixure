#pragma once

#include <fixure/parser.hpp>
#include <string>
#include <string_view>
#include <vector>
#include <expected>
#include <chrono>
#include <ostream>

namespace fixure {

struct SessionConfig {
    std::string begin_string = "FIX.4.2";
    std::string sender_comp_id;
    std::string target_comp_id;
    int heartbeat_interval = 30; // seconds
};

class FixSession {
public:
    explicit FixSession(SessionConfig config);

    int next_out_seq_num() const { return m_next_out_seq; }
    int next_in_seq_num() const { return m_next_in_seq; }
    
    void set_next_out_seq_num(int seq) { m_next_out_seq = seq; }
    void set_next_in_seq_num(int seq) { m_next_in_seq = seq; }

    const SessionConfig& config() const { return m_config; }

    // Prepare a standard message header (BeginString, BodyLength, MsgType, SenderCompID, TargetCompID, MsgSeqNum, SendingTime)
    // Returns the number of header fields populated.
    // buffer must have space for the fields.
    size_t prepare_header(std::string_view msg_type, std::span<Field> header_fields_buffer, std::string& time_str_out);

    // Process an incoming parsed message. Returns a vector of action commands to perform
    // (e.g. SEND_HEARTBEAT, SEND_LOGON, SEND_RESEND_REQUEST, INCOMING_APPLICATION_MSG).
    enum class SessionAction {
        None,
        SendLogon,
        SendHeartbeat,
        SendReject,
        SendResendRequest,
        ResetSequence,
        ProcessApplicationMessage,
        Disconnect
    };

    struct ProcessResult {
        SessionAction action = SessionAction::None;
        int detail = 0; // e.g. target sequence number for resend request, or reject reason
        std::string info;
    };

    ProcessResult receive_message(const FixMessage& msg);

    // Call this when sending an outgoing message (increments out sequence number)
    void on_message_sent();

private:
    SessionConfig m_config;
    int m_next_out_seq = 1;
    int m_next_in_seq = 1;
    bool m_logged_on = false;
};

// Generates FIX formatted sending time (YYYYMMDD-HH:MM:SS.mmm)
std::string get_sending_time();

inline std::ostream& operator<<(std::ostream& os, FixSession::SessionAction action) {
    switch (action) {
        case FixSession::SessionAction::None: os << "None"; break;
        case FixSession::SessionAction::SendLogon: os << "SendLogon"; break;
        case FixSession::SessionAction::SendHeartbeat: os << "SendHeartbeat"; break;
        case FixSession::SessionAction::SendReject: os << "SendReject"; break;
        case FixSession::SessionAction::SendResendRequest: os << "SendResendRequest"; break;
        case FixSession::SessionAction::ResetSequence: os << "ResetSequence"; break;
        case FixSession::SessionAction::ProcessApplicationMessage: os << "ProcessApplicationMessage"; break;
        case FixSession::SessionAction::Disconnect: os << "Disconnect"; break;
    }
    return os;
}

} // namespace fixure
