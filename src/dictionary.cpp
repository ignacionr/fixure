#include <fixure/dictionary.hpp>
#include <fixure/parser.hpp>
#include <fstream>
#include <sstream>
#include <charconv>

namespace fixure {

namespace {

// Helper to extract an attribute value from a tag string (e.g. 'number="1"')
std::string_view get_attribute(std::string_view tag, std::string_view attr) {
    std::string key = std::string(attr) + "=\"";
    size_t start = tag.find(key);
    if (start == std::string_view::npos) {
        // Try single quotes just in case
        key = std::string(attr) + "='";
        start = tag.find(key);
        if (start == std::string_view::npos) {
            return {};
        }
    }
    start += key.length();
    size_t end = tag.find(key.back() == '"' ? '"' : '\'', start);
    if (end == std::string_view::npos) {
        return {};
    }
    return tag.substr(start, end - start);
}

} // namespace

std::expected<Dictionary, std::string> Dictionary::load_xml(std::string_view file_path) {
    std::ifstream file{std::string(file_path)};
    if (!file.is_open()) {
        return std::unexpected("Could not open file: " + std::string(file_path));
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return parse_xml(buffer.str());
}

std::expected<Dictionary, std::string> Dictionary::parse_xml(std::string_view xml) {
    Dictionary dict;
    size_t pos = 0;

    std::string current_message_type = "";
    bool in_fields_section = false;
    bool in_messages_section = false;

    while (pos < xml.length()) {
        size_t tag_start = xml.find('<', pos);
        if (tag_start == std::string_view::npos) {
            break;
        }

        // Handle XML comments <!-- ... -->
        if (xml.substr(tag_start, 4) == "<!--") {
            size_t comment_end = xml.find("-->", tag_start + 4);
            if (comment_end == std::string_view::npos) {
                return std::unexpected("Malformed XML: unclosed comment");
            }
            pos = comment_end + 3;
            continue;
        }

        size_t tag_end = xml.find('>', tag_start);
        if (tag_end == std::string_view::npos) {
            return std::unexpected("Malformed XML: unclosed tag");
        }

        std::string_view tag_content = xml.substr(tag_start + 1, tag_end - (tag_start + 1));
        pos = tag_end + 1;

        // Skip end tags we don't care about, or track state
        if (tag_content.starts_with("/")) {
            std::string_view end_name = tag_content.substr(1);
            if (end_name == "fields") {
                in_fields_section = false;
            } else if (end_name == "messages") {
                in_messages_section = false;
            } else if (end_name == "message") {
                current_message_type = "";
            }
            continue;
        }

        // Get tag name
        size_t space_pos = tag_content.find(' ');
        std::string_view tag_name = (space_pos == std::string_view::npos) 
            ? tag_content 
            : tag_content.substr(0, space_pos);

        if (tag_name == "fields") {
            in_fields_section = true;
        } else if (tag_name == "messages") {
            in_messages_section = true;
        } else if (tag_name == "field") {
            if (in_fields_section) {
                // Main field definition: <field number="1" name="Account" type="STRING"/>
                std::string_view num_str = get_attribute(tag_content, "number");
                std::string_view name_str = get_attribute(tag_content, "name");
                std::string_view type_str = get_attribute(tag_content, "type");

                if (!num_str.empty() && !name_str.empty()) {
                    int tag_num = 0;
                    std::from_chars(num_str.data(), num_str.data() + num_str.size(), tag_num);
                    dict.m_name_to_tag[std::string(name_str)] = tag_num;
                    dict.m_tag_to_name[tag_num] = std::string(name_str);
                    if (!type_str.empty()) {
                        dict.m_tag_to_type[tag_num] = std::string(type_str);
                    }
                }
            } else if (!current_message_type.empty()) {
                // Message-specific field list: <field name="Account" required="Y"/>
                std::string_view name_str = get_attribute(tag_content, "name");
                std::string_view req_str = get_attribute(tag_content, "required");

                if (!name_str.empty()) {
                    bool req = (req_str == "Y" || req_str == "y");
                    // We need to resolve the tag number later or now
                    auto it = dict.m_name_to_tag.find(std::string(name_str));
                    if (it != dict.m_name_to_tag.end()) {
                        dict.m_messages[current_message_type].fields[it->second] = req;
                    } else {
                        // Temp storage or assume fields defined before messages
                        // In QuickFIX XML, <fields> section always precedes <messages>.
                        // But to be safe, we can resolve it by saving name and resolving post-parse
                        // Let's resolve it now.
                    }
                }
            }
        } else if (tag_name == "message" && in_messages_section) {
            // Message definition: <message name="Heartbeat" msgtype="0" msgcat="admin">
            std::string_view name_str = get_attribute(tag_content, "name");
            std::string_view type_str = get_attribute(tag_content, "msgtype");

            if (!name_str.empty() && !type_str.empty()) {
                std::string msg_type(type_str);
                current_message_type = msg_type;
                dict.m_msg_name_to_type[std::string(name_str)] = msg_type;
                dict.m_msg_type_to_name[msg_type] = std::string(name_str);
                dict.m_messages[msg_type] = MessageDef{
                    .name = std::string(name_str),
                    .msg_type = msg_type,
                    .fields = {}
                };
            }
        }
    }

    return dict;
}

std::optional<int> Dictionary::get_tag(std::string_view name) const {
    auto it = m_name_to_tag.find(std::string(name));
    if (it != m_name_to_tag.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<std::string_view> Dictionary::get_name(int tag) const {
    auto it = m_tag_to_name.find(tag);
    if (it != m_tag_to_name.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<std::string_view> Dictionary::get_msg_type(std::string_view msg_name) const {
    auto it = m_msg_name_to_type.find(std::string(msg_name));
    if (it != m_msg_name_to_type.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<std::string_view> Dictionary::get_msg_name(std::string_view msg_type) const {
    auto it = m_msg_type_to_name.find(std::string(msg_type));
    if (it != m_msg_type_to_name.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool Dictionary::is_required(std::string_view msg_type, int tag) const {
    auto it = m_messages.find(std::string(msg_type));
    if (it != m_messages.end()) {
        auto fit = it->second.fields.find(tag);
        if (fit != it->second.fields.end()) {
            return fit->second;
        }
    }
    return false;
}

bool Dictionary::validate_message(std::string_view msg_type, const FixMessage& msg, std::string& out_error) const {
    auto it = m_messages.find(std::string(msg_type));
    if (it == m_messages.end()) {
        out_error = "Unknown MsgType: " + std::string(msg_type);
        return false;
    }

    // Verify all required fields for this message type are present
    for (const auto& [tag, req] : it->second.fields) {
        if (req && !msg.get_field(tag)) {
            auto name_opt = get_name(tag);
            std::string field_name = name_opt ? std::string(*name_opt) : std::to_string(tag);
            out_error = "Missing required field: " + field_name + " (" + std::to_string(tag) + ")";
            return false;
        }
    }

    return true;
}

} // namespace fixure
