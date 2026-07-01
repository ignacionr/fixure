#include "test_framework.hpp"
#include <fixure/dictionary.hpp>
#include <fixure/parser.hpp>

TEST(DictionaryTest, ParseXmlAndValidate) {
    std::string xml = 
        "<fix major=\"4\" minor=\"2\">"
        "  <fields>"
        "    <field number=\"1\" name=\"Account\" type=\"STRING\"/>"
        "    <field number=\"8\" name=\"BeginString\" type=\"STRING\"/>"
        "    <field number=\"9\" name=\"BodyLength\" type=\"INT\"/>"
        "    <field number=\"10\" name=\"Checksum\" type=\"STRING\"/>"
        "    <field number=\"35\" name=\"MsgType\" type=\"STRING\"/>"
        "    <field number=\"108\" name=\"HeartBtInt\" type=\"INT\"/>"
        "  </fields>"
        "  <messages>"
        "    <message name=\"Logon\" msgtype=\"A\" msgcat=\"admin\">"
        "      <field name=\"HeartBtInt\" required=\"Y\"/>"
        "      <field name=\"Account\" required=\"N\"/>"
        "    </message>"
        "  </messages>"
        "</fix>";

    auto dict_res = fixure::Dictionary::parse_xml(xml);
    ASSERT_TRUE(dict_res.has_value());

    const auto& dict = *dict_res;
    ASSERT_EQ(*dict.get_tag("Account"), 1);
    ASSERT_EQ(*dict.get_name(108), "HeartBtInt");
    ASSERT_EQ(*dict.get_msg_type("Logon"), "A");
    ASSERT_EQ(*dict.get_msg_name("A"), "Logon");

    ASSERT_TRUE(dict.is_required("A", 108));
    ASSERT_FALSE(dict.is_required("A", 1)); // Account is optional

    // Validate a valid logon message (HeartBtInt is present)
    std::string raw_valid = "8=FIX.4.2\x01" "9=12\x01" "35=A\x01" "108=30\x01" "10=026\x01";
    auto msg_valid = fixure::FixMessage::parse(raw_valid);
    ASSERT_TRUE(msg_valid.has_value());

    std::string err;
    ASSERT_TRUE(dict.validate_message("A", *msg_valid, err));

    // Validate an invalid logon message (HeartBtInt tag 108 is missing)
    std::string raw_invalid = "8=FIX.4.2\x01" "9=5\x01" "35=A\x01" "10=178\x01";
    auto msg_invalid = fixure::FixMessage::parse(raw_invalid);
    ASSERT_TRUE(msg_invalid.has_value());

    ASSERT_FALSE(dict.validate_message("A", *msg_invalid, err));
    ASSERT_FALSE(err.empty());
}
