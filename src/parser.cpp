#include <fixure/parser.hpp>
#include <charconv>
#include <format>
#include <numeric>

namespace fixure {

std::string_view to_string(ParseError err) {
    static constexpr std::array<std::string_view, 12> names{
        "Ok",
        "EmptyBuffer",
        "InvalidBeginString",
        "MissingBodyLength",
        "InvalidBodyLength",
        "BodyLengthMismatch",
        "MissingChecksum",
        "InvalidChecksum",
        "ChecksumMismatch",
        "InvalidFormat",
        "TooManyFields",
        "TagNotFound"
    };

    const auto idx = static_cast<size_t>(err);
    if (idx < names.size()) {
        return names[idx];
    }
    return "UnknownError";
}

std::expected<FixMessage, ParseError> FixMessage::parse(std::string_view raw, char separator) {
    if (raw.empty()) {
        return std::unexpected(ParseError::EmptyBuffer);
    }

    FixMessage msg;
    msg.m_raw = raw;

    size_t pos = 0;
    size_t field_idx = 0;
    
    size_t body_start_pos = std::string_view::npos;
    size_t body_end_pos = std::string_view::npos;
    size_t checksum_start_pos = std::string_view::npos;

    while (pos < raw.length()) {
        if (field_idx >= MAX_FIELDS) {
            return std::unexpected(ParseError::TooManyFields);
        }

        size_t eq = raw.find('=', pos);
        if (eq == std::string_view::npos) {
            // Check if there are only trailing whitespaces/SOHs
            bool only_whitespace = true;
            for (size_t i = pos; i < raw.length(); ++i) {
                if (raw[i] != separator && raw[i] != '\r' && raw[i] != '\n' && raw[i] != ' ') {
                    only_whitespace = false;
                    break;
                }
            }
            if (only_whitespace) {
                break;
            }
            return std::unexpected(ParseError::InvalidFormat);
        }

        size_t soh = raw.find(separator, eq);
        if (soh == std::string_view::npos) {
            return std::unexpected(ParseError::InvalidFormat);
        }

        std::string_view tag_str = raw.substr(pos, eq - pos);
        std::string_view val_str = raw.substr(eq + 1, soh - (eq + 1));

        int tag = 0;
        auto [ptr, ec] = std::from_chars(tag_str.data(), tag_str.data() + tag_str.size(), tag);
        if (ec != std::errc{} || ptr != tag_str.data() + tag_str.size()) {
            return std::unexpected(ParseError::InvalidFormat);
        }

        msg.m_fields[field_idx] = Field{tag, val_str};
        
        // Basic position tracking for validation
        if (field_idx == 0) {
            if (tag != 8) {
                return std::unexpected(ParseError::InvalidBeginString);
            }
        } else if (field_idx == 1) {
            if (tag != 9) {
                return std::unexpected(ParseError::MissingBodyLength);
            }
            // Body starts immediately after the SOH of tag 9
            body_start_pos = soh + 1;
        }

        if (tag == 10) {
            checksum_start_pos = pos;
            body_end_pos = pos; // Body ends right before tag 10 starts
        }

        field_idx++;
        pos = soh + 1;
    }

    msg.m_field_count = field_idx;

    if (msg.m_field_count < 3) {
        return std::unexpected(ParseError::InvalidFormat);
    }

    // Verify tag 10 is indeed the last tag (ignoring trailing whitespace)
    if (msg.m_fields[msg.m_field_count - 1].tag != 10) {
        return std::unexpected(ParseError::MissingChecksum);
    }

    // Validate Body Length (tag 9)
    auto body_len_opt = msg.get_int(9);
    if (!body_len_opt) {
        return std::unexpected(ParseError::InvalidBodyLength);
    }
    int expected_body_len = *body_len_opt;

    if (body_start_pos == std::string_view::npos || body_end_pos == std::string_view::npos) {
        return std::unexpected(ParseError::InvalidFormat);
    }

    int actual_body_len = static_cast<int>(body_end_pos - body_start_pos);
    if (actual_body_len != expected_body_len) {
        return std::unexpected(ParseError::BodyLengthMismatch);
    }

    // Validate Checksum (tag 10)
    if (checksum_start_pos == std::string_view::npos) {
        return std::unexpected(ParseError::MissingChecksum);
    }

    // Calculate actual checksum of characters from 0 up to checksum_start_pos
    unsigned int calculated_checksum = 0;
    for (size_t i = 0; i < checksum_start_pos; ++i) {
        calculated_checksum += static_cast<unsigned char>(raw[i]);
    }
    calculated_checksum %= 256;

    std::string_view checksum_str = msg.m_fields[msg.m_field_count - 1].value;
    int expected_checksum = 0;
    auto [ptr_cs, ec_cs] = std::from_chars(checksum_str.data(), checksum_str.data() + checksum_str.size(), expected_checksum);

    if (ec_cs != std::errc{} || expected_checksum < 0 || expected_checksum > 255) {
        return std::unexpected(ParseError::InvalidChecksum);
    }

    if (calculated_checksum != static_cast<unsigned int>(expected_checksum)) {
        return std::unexpected(ParseError::ChecksumMismatch);
    }

    return msg;
}

std::optional<std::string_view> FixMessage::get_field(int tag) const {
    for (size_t i = 0; i < m_field_count; ++i) {
        if (m_fields[i].tag == tag) {
            return m_fields[i].value;
        }
    }
    return std::nullopt;
}

std::optional<int> FixMessage::get_int(int tag) const {
    auto val = get_field(tag);
    if (!val) return std::nullopt;
    int res = 0;
    auto [ptr, ec] = std::from_chars(val->data(), val->data() + val->size(), res);
    if (ec == std::errc{}) {
        return res;
    }
    return std::nullopt;
}

std::optional<double> FixMessage::get_double(int tag) const {
    auto val = get_field(tag);
    if (!val) return std::nullopt;
    
    if (val->size() >= 64) return std::nullopt;
    
    char buf[64];
    std::copy(val->begin(), val->end(), buf);
    buf[val->size()] = '\0';
    
    char* endptr = nullptr;
    double res = std::strtod(buf, &endptr);
    if (endptr == buf + val->size()) {
        return res;
    }
    return std::nullopt;
}

std::optional<std::string_view> FixMessage::GroupInstance::get_field(int tag) const {
    for (const auto& f : fields) {
        if (f.tag == tag) {
            return f.value;
        }
    }
    return std::nullopt;
}

FixMessage::GroupView FixMessage::get_group(int group_tag, int delim_tag) const {
    GroupView view;
    
    // Find group_tag field index
    size_t group_field_idx = std::string_view::npos;
    for (size_t i = 0; i < m_field_count; ++i) {
        if (m_fields[i].tag == group_tag) {
            group_field_idx = i;
            break;
        }
    }

    if (group_field_idx == std::string_view::npos) {
        return view;
    }

    // Number of instances expected
    auto num_instances_opt = get_int(group_tag);
    if (!num_instances_opt || *num_instances_opt <= 0) {
        return view;
    }
    size_t num_instances = static_cast<size_t>(*num_instances_opt);

    // Scan following fields
    size_t current_idx = group_field_idx + 1;
    size_t instance_start_idx = std::string_view::npos;

    while (current_idx < m_field_count && view.count < num_instances && view.count < 64) {
        int tag = m_fields[current_idx].tag;

        if (tag == delim_tag) {
            // If we already have a started instance, close it
            if (instance_start_idx != std::string_view::npos) {
                view.instances[view.count++] = GroupInstance{
                    .fields = std::span<const Field>(&m_fields[instance_start_idx], current_idx - instance_start_idx)
                };
            }
            instance_start_idx = current_idx;
        } else if (tag == 10 || tag == 8 || tag == 9) {
            // Reached end of message or another header tag; stop group parsing
            break;
        }

        current_idx++;
    }

    // Close the final instance
    if (instance_start_idx != std::string_view::npos && view.count < num_instances && view.count < 64) {
        view.instances[view.count++] = GroupInstance{
            .fields = std::span<const Field>(&m_fields[instance_start_idx], current_idx - instance_start_idx)
        };
    }

    return view;
}

std::expected<std::string_view, std::error_code> serialize_message(
    std::span<const Field> fields,
    std::span<char> buffer,
    char separator
) {
    // We need to calculate body length and checksum.
    // Layout:
    // 8=BeginString<SOH>9=BodyLength<SOH>...other fields...10=Checksum<SOH>
    
    // Find begin string and checksum, or error out if not present or in wrong order.
    if (fields.empty() || fields[0].tag != 8) {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }

    // Format all body fields (index 1 to end, but excluding checksum if already in the input fields)
    // Wait, it is better to format:
    // 1. Tag 8
    // 2. We'll placeholders for Tag 9 and Tag 10
    
    size_t offset = 0;
    
    auto write_str = [&](std::string_view s) -> bool {
        if (offset + s.size() > buffer.size()) return false;
        std::copy(s.begin(), s.end(), buffer.begin() + static_cast<ptrdiff_t>(offset));
        offset += s.size();
        return true;
    };

    auto write_tag_val = [&](int tag, std::string_view val) -> bool {
        char tag_buf[16];
        auto [ptr, ec] = std::to_chars(tag_buf, tag_buf + sizeof(tag_buf), tag);
        if (ec != std::errc{}) return false;
        
        std::string_view tag_str(tag_buf, static_cast<size_t>(ptr - tag_buf));
        if (!write_str(tag_str)) return false;
        if (offset >= buffer.size()) return false;
        buffer[offset++] = '=';
        if (!write_str(val)) return false;
        if (offset >= buffer.size()) return false;
        buffer[offset++] = separator;
        return true;
    };

    // 1. Write tag 8
    if (!write_tag_val(8, fields[0].value)) {
        return std::unexpected(std::make_error_code(std::errc::no_buffer_space));
    }

    // 2. Write body length tag placeholder
    // We'll write "9=00000" first, then patch it.
    size_t body_length_tag_pos = offset;
    if (!write_str("9=00000")) return std::unexpected(std::make_error_code(std::errc::no_buffer_space));
    if (offset >= buffer.size()) return std::unexpected(std::make_error_code(std::errc::no_buffer_space));
    buffer[offset++] = separator;

    size_t body_start_pos = offset;

    // 3. Write other fields (excluding 8, 9, 10 if present, or just formatting them)
    for (size_t i = 1; i < fields.size(); ++i) {
        const auto& f = fields[i];
        if (f.tag == 8 || f.tag == 9 || f.tag == 10) {
            continue; // Skip standard headers/trailers, we generate them
        }
        if (!write_tag_val(f.tag, f.value)) {
            return std::unexpected(std::make_error_code(std::errc::no_buffer_space));
        }
    }

    size_t body_end_pos = offset;
    size_t body_len = body_end_pos - body_start_pos;

    // Format body length into the placeholder
    char body_len_buf[16];
    auto [ptr_bl, ec_bl] = std::to_chars(body_len_buf, body_len_buf + sizeof(body_len_buf), body_len);
    if (ec_bl != std::errc{}) return std::unexpected(std::make_error_code(std::errc::value_too_large));
    std::string_view body_len_str(body_len_buf, static_cast<size_t>(ptr_bl - body_len_buf));

    // Patch the body length field: "9=" is at body_length_tag_pos.
    // The placeholder was "9=00000". Let's pad body_len_str to 5 chars or reconstruct/shift.
    // Shifting is easiest. Let's shift the rest of the buffer to fit body_len_str exactly.
    size_t placeholder_val_len = 5; // "00000"
    int diff = static_cast<int>(body_len_str.size()) - static_cast<int>(placeholder_val_len);
    
    if (diff != 0) {
        if (static_cast<int>(offset) + diff > static_cast<int>(buffer.size())) {
            return std::unexpected(std::make_error_code(std::errc::no_buffer_space));
        }

        size_t shift_start = body_length_tag_pos + 2 + placeholder_val_len;
        if (diff > 0) {
            std::move_backward(
                buffer.begin() + static_cast<ptrdiff_t>(shift_start),
                buffer.begin() + static_cast<ptrdiff_t>(offset),
                buffer.begin() + static_cast<ptrdiff_t>(offset) + diff
            );
        } else {
            std::move(
                buffer.begin() + static_cast<ptrdiff_t>(shift_start),
                buffer.begin() + static_cast<ptrdiff_t>(offset),
                buffer.begin() + static_cast<ptrdiff_t>(shift_start) + diff
            );
        }
        offset = static_cast<size_t>(static_cast<int>(offset) + diff);
    }

    // Write actual body length into the "9=xxxxx" slot
    std::copy(
        body_len_str.begin(),
        body_len_str.end(),
        buffer.begin() + static_cast<ptrdiff_t>(body_length_tag_pos + 2)
    );

    // 4. Calculate checksum of all bytes before tag 10
    unsigned int checksum = 0;
    for (size_t i = 0; i < offset; ++i) {
        checksum += static_cast<unsigned char>(buffer[i]);
    }
    checksum %= 256;

    // Write checksum tag "10="
    char cs_str[3];
    cs_str[0] = static_cast<char>('0' + (checksum / 100));
    cs_str[1] = static_cast<char>('0' + ((checksum / 10) % 10));
    cs_str[2] = static_cast<char>('0' + (checksum % 10));
    
    if (!write_tag_val(10, std::string_view(cs_str, 3))) {
        return std::unexpected(std::make_error_code(std::errc::no_buffer_space));
    }

    return std::string_view(buffer.data(), offset);
}

} // namespace fixure
