#pragma once

#include <string_view>
#include <array>
#include <optional>
#include <span>
#include <expected>
#include <system_error>
#include <ostream>

namespace fixure {

enum class ParseError {
    Ok = 0,
    EmptyBuffer,
    InvalidBeginString,    // Tag 8 must be first
    MissingBodyLength,     // Tag 9 must be second
    InvalidBodyLength,
    BodyLengthMismatch,
    MissingChecksum,       // Tag 10 must be last
    InvalidChecksum,
    ChecksumMismatch,
    InvalidFormat,         // general parsing issues (e.g., missing '=' or SOH)
    TooManyFields,
    TagNotFound
};

std::string_view to_string(ParseError err);

inline std::ostream& operator<<(std::ostream& os, ParseError err) {
    os << to_string(err);
    return os;
}

struct Field {
    int tag = 0;
    std::string_view value;
};

constexpr size_t MAX_FIELDS = 256;
constexpr char SOH = '\x01';

class FixMessage {
public:
    FixMessage() = default;
    
    // Parse raw FIX bytes. Zero-copy, zero-allocation.
    // The message is expected to use SOH (\x01) as field separator.
    // If a different delimiter (like '|') is used for debugging/tests,
    // it can be specified as the second parameter.
    static std::expected<FixMessage, ParseError> parse(std::string_view raw, char separator = SOH);

    std::string_view raw() const { return m_raw; }
    
    std::span<const Field> fields() const { return std::span<const Field>(m_fields.data(), m_field_count); }
    size_t field_count() const { return m_field_count; }

    std::optional<std::string_view> get_field(int tag) const;
    std::optional<int> get_int(int tag) const;
    std::optional<double> get_double(int tag) const;
    
    // Find repeating group instances.
    // group_tag: the tag that specifies the number of instances (e.g. 78)
    // delim_tag: the first tag in the repeating group, indicating a new instance (e.g. 79)
    struct GroupInstance {
        std::span<const Field> fields;
        std::optional<std::string_view> get_field(int tag) const;
    };

    // Returns a list of group instances. Uses static capacity to avoid allocations.
    // For simplicity, returns a std::array or a custom small_vector-like structure on stack.
    struct GroupView {
        std::array<GroupInstance, 64> instances;
        size_t count = 0;
    };

    GroupView get_group(int group_tag, int delim_tag) const;

private:
    std::string_view m_raw;
    std::array<Field, MAX_FIELDS> m_fields;
    size_t m_field_count = 0;
};

// Formatting utility: constructs a raw FIX message from list of fields (used for mocks/sends).
// To keep it zero-allocation, it writes into a caller-supplied buffer or std::span.
std::expected<std::string_view, std::error_code> serialize_message(
    std::span<const Field> fields,
    std::span<char> buffer,
    char separator = SOH
);

} // namespace fixure
