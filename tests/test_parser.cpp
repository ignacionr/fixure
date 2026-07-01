#include "test_framework.hpp"
#include <fixure/parser.hpp>

TEST(ParserTest, ParseValidMessage) {
    // Valid logon message (checksum = 110, length = 67)
    std::string raw = "8=FIX.4.2\x01" "9=67\x01" "35=A\x01" "49=SENDER\x01" "56=TARGET\x01" "34=1\x01" "52=20260701-12:00:00.000\x01" "98=0\x01" "108=30\x01" "10=110\x01";
    
    auto res = fixure::FixMessage::parse(raw);
    if (!res.has_value()) {
        std::cout << "DEBUG: Parser error code is: " << static_cast<int>(res.error()) << " (" << to_string(res.error()) << ")" << std::endl;
    }
    ASSERT_TRUE(res.has_value());
    
    ASSERT_EQ(res->field_count(), 10);
    ASSERT_EQ(*res->get_field(8), "FIX.4.2");
    ASSERT_EQ(*res->get_int(9), 67);
    ASSERT_EQ(*res->get_field(35), "A");
    ASSERT_EQ(*res->get_field(49), "SENDER");
    ASSERT_EQ(*res->get_field(56), "TARGET");
    ASSERT_EQ(*res->get_int(34), 1);
    ASSERT_EQ(*res->get_int(98), 0);
    ASSERT_EQ(*res->get_int(108), 30);
    ASSERT_EQ(*res->get_field(10), "110");
}

TEST(ParserTest, InvalidChecksum) {
    // Checksum tag says 109, but correct checksum is 110
    std::string raw = "8=FIX.4.2\x01" "9=67\x01" "35=A\x01" "49=SENDER\x01" "56=TARGET\x01" "34=1\x01" "52=20260701-12:00:00.000\x01" "98=0\x01" "108=30\x01" "10=109\x01";
    auto res = fixure::FixMessage::parse(raw);
    ASSERT_FALSE(res.has_value());
    ASSERT_EQ(res.error(), fixure::ParseError::ChecksumMismatch);
}

TEST(ParserTest, InvalidBodyLength) {
    // Body length says 66, but correct length is 67
    std::string raw = "8=FIX.4.2\x01" "9=66\x01" "35=A\x01" "49=SENDER\x01" "56=TARGET\x01" "34=1\x01" "52=20260701-12:00:00.000\x01" "98=0\x01" "108=30\x01" "10=109\x01";
    auto res = fixure::FixMessage::parse(raw);
    ASSERT_FALSE(res.has_value());
    ASSERT_EQ(res.error(), fixure::ParseError::BodyLengthMismatch);
}

TEST(ParserTest, ParseRepeatingGroups) {
    // Logon with custom repeating group for fields:
    // NoAllocs (tag 78) = 2, alloc account (79), alloc shares (80)
    std::string raw = "8=FIX.4.2\x01" "9=59\x01" "35=D\x01" "49=SND\x01" "56=TRG\x01" "34=2\x01" "78=2\x01" "79=ACC1\x01" "80=100\x01" "79=ACC2\x01" "80=200\x01" "10=134\x01";
    
    auto res = fixure::FixMessage::parse(raw);
    ASSERT_TRUE(res.has_value());
    
    auto group = res->get_group(78, 79);
    ASSERT_EQ(group.count, 2);
    
    ASSERT_EQ(*group.instances[0].get_field(79), "ACC1");
    ASSERT_EQ(*group.instances[0].get_field(80), "100");
    ASSERT_EQ(*group.instances[1].get_field(79), "ACC2");
    ASSERT_EQ(*group.instances[1].get_field(80), "200");
}

TEST(ParserTest, Serialization) {
    std::vector<fixure::Field> fields = {
        {8, "FIX.4.2"},
        {35, "0"},
        {49, "SENDER"},
        {56, "TARGET"},
        {34, "2"}
    };
    
    char buf[512];
    auto res = fixure::serialize_message(fields, buf, '|');
    ASSERT_TRUE(res.has_value());
    
    std::string_view expected = "8=FIX.4.2|9=30|35=0|49=SENDER|56=TARGET|34=2|10=100|";
    ASSERT_EQ(*res, expected);
}
