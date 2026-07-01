#pragma once

#include <fixure/runner.hpp>
#include <string>
#include <string_view>
#include <expected>

namespace fixure {

class SessionReplayer {
public:
    explicit SessionReplayer(std::shared_ptr<Connection> conn);

    struct ReplayResult {
        size_t messages_sent = 0;
        bool success = false;
        std::string error_message;
    };

    // Replays a file containing one FIX message per line.
    // separator is the FIX tag separator (defaults to SOH or '|').
    ReplayResult replay_file(std::string_view file_path, char separator = SOH, bool preserve_timing = false);

private:
    std::shared_ptr<Connection> m_conn;
};

} // namespace fixure
