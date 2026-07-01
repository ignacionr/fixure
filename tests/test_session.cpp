#include "test_framework.hpp"
#include <fixure/session.hpp>

TEST(SessionTest, LogonAndSequenceFlow) {
    fixure::SessionConfig config{
        .sender_comp_id = "CLIENT",
        .target_comp_id = "SERVER"
    };

    fixure::FixSession session(config);
    ASSERT_EQ(session.next_out_seq_num(), 1);
    ASSERT_EQ(session.next_in_seq_num(), 1);

    // Receive first message (Logon)
    std::string raw_logon = "8=FIX.4.2\x01" "9=42\x01" "35=A\x01" "49=SERVER\x01" "56=CLIENT\x01" "34=1\x01" "98=0\x01" "108=30\x01" "10=188\x01";
    auto msg_logon = fixure::FixMessage::parse(raw_logon);
    ASSERT_TRUE(msg_logon.has_value());

    auto res = session.receive_message(*msg_logon);
    ASSERT_EQ(res.action, fixure::FixSession::SessionAction::SendLogon);
    ASSERT_EQ(session.next_in_seq_num(), 2);

    // Receive message with correct seq num
    std::string raw_hb = "8=FIX.4.2\x01" "9=30\x01" "35=0\x01" "49=SERVER\x01" "56=CLIENT\x01" "34=2\x01" "10=144\x01";
    auto msg_hb = fixure::FixMessage::parse(raw_hb);
    ASSERT_TRUE(msg_hb.has_value());

    res = session.receive_message(*msg_hb);
    ASSERT_EQ(res.action, fixure::FixSession::SessionAction::None);
    ASSERT_EQ(session.next_in_seq_num(), 3);

    // Receive message with gap (expected 3, got 5)
    std::string raw_gap = "8=FIX.4.2\x01" "9=30\x01" "35=0\x01" "49=SERVER\x01" "56=CLIENT\x01" "34=5\x01" "10=147\x01";
    auto msg_gap = fixure::FixMessage::parse(raw_gap);
    ASSERT_TRUE(msg_gap.has_value());

    res = session.receive_message(*msg_gap);
    ASSERT_EQ(res.action, fixure::FixSession::SessionAction::SendResendRequest);
    ASSERT_EQ(res.detail, 3); // request starting from 3
    ASSERT_EQ(session.next_in_seq_num(), 3); // in seq num does not advance
}
