#include <fixure/session.hpp>
#include <chrono>
#include <cstdio>
#include <ctime>

namespace fixure {

std::string get_sending_time() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ) % 1000;
    
    std::tm tm_val{};
#if defined(_MSC_VER)
    gmtime_s(&tm_val, &time);
#else
    gmtime_r(&time, &tm_val);
#endif

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d%02d%02d-%02d:%02d:%02d.%03d",
        tm_val.tm_year + 1900, tm_val.tm_mon + 1, tm_val.tm_mday,
        tm_val.tm_hour, tm_val.tm_min, tm_val.tm_sec,
        static_cast<int>(ms.count()));
    return std::string(buf);
}

FixSession::FixSession(SessionConfig config)
    : m_config(std::move(config)) {}

size_t FixSession::prepare_header(std::string_view msg_type, std::span<Field> header_fields_buffer, std::string& time_str_out) {
    // A standard FIX header must contain:
    // 8=BeginString
    // 9=BodyLength (handled by serializer)
    // 35=MsgType
    // 49=SenderCompID
    // 56=TargetCompID
    // 34=MsgSeqNum
    // 52=SendingTime
    
    if (header_fields_buffer.size() < 6) {
        return 0; // buffer too small
    }

    time_str_out = get_sending_time();

    header_fields_buffer[0] = Field{8, m_config.begin_string};
    header_fields_buffer[1] = Field{35, msg_type};
    header_fields_buffer[2] = Field{49, m_config.sender_comp_id};
    header_fields_buffer[3] = Field{56, m_config.target_comp_id};
    
    // Convert out sequence number to string.
    // We store the string representation somewhere or we just allocate it for headers.
    // Since this is session management (which is a higher layer), a small string allocation or inline conversion is fine.
    // Let's use a static/thread-local or stack buffer for the sequence number.
    // But since the header buffer takes std::string_view, we need a stable string storage.
    // We can use a thread-local or session-owned buffer.
    // Let's store sequence string as a thread-local to maintain zero-allocation for the session.
    static thread_local char seq_buf[16];
    auto [ptr_seq, ec_seq] = std::to_chars(seq_buf, seq_buf + sizeof(seq_buf), m_next_out_seq);
    std::string_view seq_str(seq_buf, ptr_seq - seq_buf);

    header_fields_buffer[4] = Field{34, seq_str};
    header_fields_buffer[5] = Field{52, time_str_out};

    return 6;
}

FixSession::ProcessResult FixSession::receive_message(const FixMessage& msg) {
    auto msg_type_opt = msg.get_field(35);
    auto seq_num_opt = msg.get_int(34);

    if (!msg_type_opt) {
        return ProcessResult{SessionAction::SendReject, 0, "Missing MsgType (35)"};
    }
    if (!seq_num_opt) {
        return ProcessResult{SessionAction::SendReject, 0, "Missing MsgSeqNum (34)"};
    }

    std::string_view msg_type = *msg_type_opt;
    int seq_num = *seq_num_opt;

    if (!m_logged_on) {
        if (msg_type != "A") {
            return ProcessResult{SessionAction::Disconnect, 0, "First message must be Logon (35=A)"};
        }
        
        m_logged_on = true;
        m_next_in_seq = seq_num + 1;
        return ProcessResult{SessionAction::SendLogon, seq_num, "Logon processed"};
    }

    // Sequence number checks
    if (seq_num == m_next_in_seq) {
        m_next_in_seq++;
        
        if (msg_type == "0") {
            // Heartbeat
            return ProcessResult{SessionAction::None, 0, "Heartbeat received"};
        } else if (msg_type == "1") {
            // Test Request
            return ProcessResult{SessionAction::SendHeartbeat, seq_num, "Test Request received"};
        } else if (msg_type == "2") {
            // Resend Request
            return ProcessResult{SessionAction::ResetSequence, seq_num, "Resend Request received"};
        } else if (msg_type == "4") {
            // Sequence Reset (Reset or Gap Fill)
            auto new_seq_opt = msg.get_int(36);
            if (new_seq_opt) {
                m_next_in_seq = *new_seq_opt;
                return ProcessResult{SessionAction::None, *new_seq_opt, "Sequence reset"};
            }
            return ProcessResult{SessionAction::SendReject, 0, "SequenceReset missing NewSeqNo (36)"};
        } else if (msg_type == "5") {
            // Logout
            m_logged_on = false;
            return ProcessResult{SessionAction::Disconnect, 0, "Logout received"};
        }
        
        return ProcessResult{SessionAction::ProcessApplicationMessage, 0, "Application message processed"};
    } else if (seq_num > m_next_in_seq) {
        // Gap detected!
        // We do not increment m_next_in_seq, we ask for a Resend Request.
        int expected = m_next_in_seq;
        return ProcessResult{SessionAction::SendResendRequest, expected, "Gap detected"};
    } else {
        // seq_num < m_next_in_seq
        // Check for PossDupFlag (43)
        auto poss_dup_opt = msg.get_field(43);
        if (poss_dup_opt && *poss_dup_opt == "Y") {
            return ProcessResult{SessionAction::None, 0, "Duplicate message ignored"};
        }
        return ProcessResult{SessionAction::Disconnect, 0, "Sequence number too low"};
    }
}

void FixSession::on_message_sent() {
    m_next_out_seq++;
}

} // namespace fixure
