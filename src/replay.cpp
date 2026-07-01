#include <fixure/replay.hpp>
#include <fixure/parser.hpp>
#include <fstream>
#include <sstream>
#include <charconv>
#include <thread>
#include <chrono>

#if defined(_MSC_VER)
#define timegm _mkgmtime
#endif

namespace fixure {

SessionReplayer::SessionReplayer(std::shared_ptr<Connection> conn)
    : m_conn(std::move(conn)) {}

namespace {

std::chrono::system_clock::time_point parse_sending_time(std::string_view time_str) {
    if (time_str.size() < 21) return {};

    int year = 0, month = 0, day = 0, hour = 0, min = 0, sec = 0, ms = 0;
    std::from_chars(time_str.data(), time_str.data() + 4, year);
    std::from_chars(time_str.data() + 4, time_str.data() + 6, month);
    std::from_chars(time_str.data() + 6, time_str.data() + 8, day);
    // index 8 is '-'
    std::from_chars(time_str.data() + 9, time_str.data() + 11, hour);
    // index 11 is ':'
    std::from_chars(time_str.data() + 12, time_str.data() + 14, min);
    // index 14 is ':'
    std::from_chars(time_str.data() + 15, time_str.data() + 17, sec);
    // index 17 is '.'
    std::from_chars(time_str.data() + 18, time_str.data() + 21, ms);

    std::tm tm_val{};
    tm_val.tm_year = year - 1900;
    tm_val.tm_mon = month - 1;
    tm_val.tm_mday = day;
    tm_val.tm_hour = hour;
    tm_val.tm_min = min;
    tm_val.tm_sec = sec;

    auto time_t_val = timegm(&tm_val);
    return std::chrono::system_clock::from_time_t(time_t_val) + std::chrono::milliseconds(ms);
}

} // namespace

SessionReplayer::ReplayResult SessionReplayer::replay_file(std::string_view file_path, char separator, bool preserve_timing) {
    ReplayResult result;
    result.messages_sent = 0;

    std::ifstream file{std::string(file_path)};
    if (!file.is_open()) {
        result.error_message = "Could not open log file: " + std::string(file_path);
        return result;
    }

    std::string line;
    std::chrono::system_clock::time_point last_msg_time;
    bool first_msg = true;

    while (std::getline(file, line)) {
        // Strip trailing/leading spaces/newlines
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }

        // Parse message to get sending time if we need to preserve timing
        std::chrono::system_clock::time_point msg_time;
        if (preserve_timing) {
            auto parsed = FixMessage::parse(line, separator);
            if (parsed) {
                auto time_opt = parsed->get_field(52); // SendingTime
                if (time_opt) {
                    msg_time = parse_sending_time(*time_opt);
                }
            }
        }

        if (preserve_timing && !first_msg && msg_time != std::chrono::system_clock::time_point{}) {
            if (msg_time > last_msg_time) {
                auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(msg_time - last_msg_time);
                std::this_thread::sleep_for(delay);
            }
        }

        // Send the raw message
        if (!m_conn->send(line)) {
            result.error_message = "Send failed at message index " + std::to_string(result.messages_sent);
            return result;
        }

        result.messages_sent++;
        if (preserve_timing && msg_time != std::chrono::system_clock::time_point{}) {
            last_msg_time = msg_time;
            first_msg = false;
        }
    }

    result.success = true;
    return result;
}

} // namespace fixure
