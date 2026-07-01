#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <optional>
#include <expected>

namespace fixure {

struct FieldDef {
    int tag = 0;
    std::string name;
    std::string type;
};

struct MessageDef {
    std::string name;
    std::string msg_type;
    std::unordered_map<int, bool> fields; // tag -> required
};

class Dictionary {
public:
    Dictionary() = default;

    // Load from a QuickFIX XML dictionary file
    static std::expected<Dictionary, std::string> load_xml(std::string_view file_path);
    // Load from a direct XML string
    static std::expected<Dictionary, std::string> parse_xml(std::string_view xml_content);

    std::optional<int> get_tag(std::string_view name) const;
    std::optional<std::string_view> get_name(int tag) const;
    std::optional<std::string_view> get_msg_type(std::string_view msg_name) const;
    std::optional<std::string_view> get_msg_name(std::string_view msg_type) const;

    bool is_required(std::string_view msg_type, int tag) const;
    bool validate_message(std::string_view msg_type, const class FixMessage& msg, std::string& out_error) const;

private:
    std::unordered_map<std::string, int> m_name_to_tag;
    std::unordered_map<int, std::string> m_tag_to_name;
    std::unordered_map<std::string, std::string> m_msg_name_to_type;
    std::unordered_map<std::string, std::string> m_msg_type_to_name;
    std::unordered_map<std::string, MessageDef> m_messages; // msg_type -> MessageDef
    std::unordered_map<int, std::string> m_tag_to_type;
};

} // namespace fixure
