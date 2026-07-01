#include <fixure/runner.hpp>
#include <iostream>
#include <cstring>
#include <algorithm>

#if !defined(_MSC_VER)
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

namespace fixure {

// --- InMemoryConnection ---

InMemoryConnection::InMemoryConnection() = default;

bool InMemoryConnection::connect() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_connected = true;
    return true;
}

void InMemoryConnection::disconnect() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_connected = false;
    m_cv_client.notify_all();
    m_cv_server.notify_all();
}

bool InMemoryConnection::send(std::string_view data) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_connected) return false;
    m_to_server.push(std::string(data));
    m_cv_server.notify_one();
    return true;
}

std::optional<std::string> InMemoryConnection::receive(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(m_mutex);
    if (!m_connected) return std::nullopt;
    
    if (m_to_client.empty()) {
        m_cv_client.wait_for(lock, timeout, [this] { return !m_to_client.empty() || !m_connected; });
    }
    
    if (!m_connected || m_to_client.empty()) {
        return std::nullopt;
    }
    
    std::string msg = std::move(m_to_client.front());
    m_to_client.pop();
    return msg;
}

bool InMemoryConnection::server_send(std::string_view data) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_connected) return false;
    m_to_client.push(std::string(data));
    m_cv_client.notify_one();
    return true;
}

std::optional<std::string> InMemoryConnection::server_receive(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(m_mutex);
    if (!m_connected) return std::nullopt;
    
    if (m_to_server.empty()) {
        m_cv_server.wait_for(lock, timeout, [this] { return !m_to_server.empty() || !m_connected; });
    }
    
    if (!m_connected || m_to_server.empty()) {
        return std::nullopt;
    }
    
    std::string msg = std::move(m_to_server.front());
    m_to_server.pop();
    return msg;
}

// --- TcpConnection ---

TcpConnection::TcpConnection(std::string host, int port)
    : m_host(std::move(host)), m_port(port) {
#if defined(_MSC_VER)
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

TcpConnection::~TcpConnection() {
    disconnect();
#if defined(_MSC_VER)
    WSACleanup();
#endif
}

bool TcpConnection::connect() {
    disconnect();

    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    std::string port_str = std::to_string(m_port);
    if (getaddrinfo(m_host.c_str(), port_str.c_str(), &hints, &res) != 0) {
        return false;
    }

    m_socket = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (m_socket < 0) {
        freeaddrinfo(res);
        return false;
    }

    if (::connect(m_socket, res->ai_addr, res->ai_addrlen) < 0) {
        freeaddrinfo(res);
        disconnect();
        return false;
    }

    freeaddrinfo(res);
    return true;
}

void TcpConnection::disconnect() {
    if (m_socket >= 0) {
#if !defined(_MSC_VER)
        close(m_socket);
#else
        closesocket(m_socket);
#endif
        m_socket = -1;
    }
}

bool TcpConnection::send(std::string_view data) {
    if (m_socket < 0) return false;
    
    size_t total_sent = 0;
    while (total_sent < data.size()) {
        auto sent = ::send(m_socket, data.data() + total_sent, data.size() - total_sent, 0);
        if (sent <= 0) {
            return false;
        }
        total_sent += sent;
    }
    return true;
}

std::optional<std::string> TcpConnection::receive(std::chrono::milliseconds timeout) {
    if (m_socket < 0) return std::nullopt;

    // Set receive timeout
#if !defined(_MSC_VER)
    struct timeval tv;
    tv.tv_sec = timeout.count() / 1000;
    tv.tv_usec = (timeout.count() % 1000) * 1000;
    setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#else
    DWORD tv = static_cast<DWORD>(timeout.count());
    setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#endif

    char buf[2048];
    auto bytes_received = ::recv(m_socket, buf, sizeof(buf), 0);
    if (bytes_received <= 0) {
        return std::nullopt;
    }
    return std::string(buf, bytes_received);
}

// --- ScenarioRunner ---

ScenarioRunner::ScenarioRunner(FixSession& session, std::shared_ptr<Connection> conn)
    : m_session(session), m_conn(std::move(conn)) {}

namespace {
std::optional<std::string> extract_next_message(std::string& buffer, char separator) {
    size_t start = buffer.find("8=");
    if (start == std::string::npos) {
        buffer.clear();
        return std::nullopt;
    }
    if (start > 0) {
        buffer.erase(0, start);
    }
    
    size_t idx10 = buffer.find("10=");
    if (idx10 == std::string::npos) {
        return std::nullopt;
    }
    
    size_t separator_pos = buffer.find(separator, idx10);
    if (separator_pos == std::string::npos) {
        return std::nullopt;
    }
    
    size_t msg_len = separator_pos + 1;
    std::string msg = buffer.substr(0, msg_len);
    buffer.erase(0, msg_len);
    return msg;
}
} // namespace

ScenarioRunner::Result ScenarioRunner::run(const Scenario& scenario) {
    Result result;
    result.steps_executed = 0;

    std::string rx_buffer;
    char separator = SOH; // Default FIX separator

    for (const auto& step : scenario.steps()) {
        result.steps_executed++;
        
        switch (step.type) {
            case ActionType::Connect: {
                if (!m_conn->connect()) {
                    result.error_message = "Step " + std::to_string(result.steps_executed) + " (CONNECT): Connection failed";
                    return result;
                }
                break;
            }
            case ActionType::Disconnect: {
                m_conn->disconnect();
                break;
            }
            case ActionType::Send: {
                // Construct the message fields
                std::vector<Field> all_fields;
                all_fields.reserve(6 + step.fields.size());
                
                // Set placeholders for header
                Field header_buf[6];
                std::string time_str;
                size_t header_count = m_session.prepare_header(
                    step.fields[0].tag == 35 ? step.fields[0].value : "0", 
                    header_buf, 
                    time_str
                );
                
                for (size_t i = 0; i < header_count; ++i) {
                    all_fields.push_back(header_buf[i]);
                }
                for (const auto& f : step.fields) {
                    // Skip if they are already standard header tags 8, 35, 49, 56, 34, 52
                    if (f.tag == 8 || f.tag == 35 || f.tag == 49 || f.tag == 56 || f.tag == 34 || f.tag == 52) {
                        continue;
                    }
                    all_fields.push_back(f);
                }

                char serialize_buf[4096];
                auto res = serialize_message(all_fields, serialize_buf, separator);
                if (!res) {
                    result.error_message = "Step " + std::to_string(result.steps_executed) + " (SEND): Serialization failed";
                    return result;
                }

                if (!m_conn->send(*res)) {
                    result.error_message = "Step " + std::to_string(result.steps_executed) + " (SEND): Send failed";
                    return result;
                }

                m_session.on_message_sent();
                break;
            }
            case ActionType::Expect: {
                bool matched = false;
                auto start_time = std::chrono::steady_clock::now();
                auto timeout = std::chrono::seconds(5);

                while (!matched) {
                    if (std::chrono::steady_clock::now() - start_time > timeout) {
                        result.error_message = "Step " + std::to_string(result.steps_executed) + " (EXPECT): Timeout waiting for message";
                        return result;
                    }

                    // Try to parse from the buffer first
                    auto msg_str_opt = extract_next_message(rx_buffer, separator);
                    if (!msg_str_opt) {
                        // Receive new data
                        auto rx = m_conn->receive(std::chrono::milliseconds(500));
                        if (rx) {
                            rx_buffer.append(*rx);
                        }
                        continue;
                    }

                    // We have a message, let's parse it!
                    auto parsed_msg_res = FixMessage::parse(*msg_str_opt, separator);
                    if (!parsed_msg_res) {
                        result.error_message = "Step " + std::to_string(result.steps_executed) + " (EXPECT): Parse error " + std::string(to_string(parsed_msg_res.error()));
                        return result;
                    }

                    const auto& msg = *parsed_msg_res;

                    // Let session handle sequence & admin layers
                    auto session_res = m_session.receive_message(msg);
                    if (session_res.action == FixSession::SessionAction::Disconnect) {
                        result.error_message = "Step " + std::to_string(result.steps_executed) + " (EXPECT): Session disconnect. " + session_res.info;
                        return result;
                    }

                    // Check if it matches expected fields
                    if (match_fields(msg, step.fields)) {
                        matched = true;
                    } else {
                        // Check if it triggers a mock response instead
                        if (process_mocks(msg)) {
                            // Handled by mock, keep waiting for the expected message
                            continue;
                        }
                        
                        // If it's a heartbeat/test request, it was already handled by session, so we can ignore and keep waiting
                        auto msg_type_opt = msg.get_field(35);
                        if (msg_type_opt && (*msg_type_opt == "0" || *msg_type_opt == "1")) {
                            continue;
                        }

                        // Otherwise, it's an mismatching application message
                        result.error_message = "Step " + std::to_string(result.steps_executed) + " (EXPECT): Fields mismatch. Received MsgType=" + std::string(msg_type_opt.value_or("?"));
                        return result;
                    }
                }
                break;
            }
            case ActionType::Mock: {
                // Register a mock rule
                m_active_mocks.push_back(step);
                break;
            }
        }
    }

    result.success = true;
    return result;
}

bool ScenarioRunner::match_fields(const FixMessage& msg, std::span<const Field> expected) const {
    for (const auto& f : expected) {
        auto actual_val = msg.get_field(f.tag);
        if (!actual_val || *actual_val != f.value) {
            return false;
        }
    }
    return true;
}

bool ScenarioRunner::process_mocks(const FixMessage& received_msg) {
    for (const auto& mock_rule : m_active_mocks) {
        // The mock rule fields have:
        // - Trigger pattern (e.g. tag 35=D, etc.)
        // Wait, how do we distinguish trigger fields vs response fields in a single Mock step?
        // Let's design:
        // A Mock step's fields list specifies the fields that must be present to trigger the mock.
        // Wait! How do we know what the mock response is?
        // Let's look at the scenario file structure:
        // A MOCK action can be split or contain both trigger and response, or we can assume:
        // The MOCK step registers a responder: when we receive a message matching the MOCK step's trigger (e.g. 35=D),
        // we automatically respond with a pre-configured message!
        // But how does our Scenario Step distinguish trigger from response?
        // Ah! In `ScenarioStep`, we have a single `fields` list.
        // We can divide it:
        // For example, if a field is `35=D`, that is the trigger.
        // The mock response fields can be specified in the same step, or we can use a custom prefix, or we can assume:
        // Trigger is tag 35=D, and any fields following it are response?
        // Wait, a cleaner way:
        // We can define the trigger as the first few fields (e.g., MsgType), and the rest of the fields as the response!
        // Or we can use a specific separator, or we can simply check:
        // The trigger is `35=D` (the incoming message type). The response is all fields of the mock step *except* the trigger!
        // Let's check: yes!
        // If we receive a message with `35=D` (matching the mock's first field `35=D`), we respond with a message constructed from the remaining fields in the mock step!
        // Let's trace this:
        // Suppose the MOCK step has: `35=D | 35=8 | 39=2 | 150=F`.
        // The first field is `35=D` (the trigger: if MsgType is D).
        // The response is: `35=8 | 39=2 | 150=F`!
        // That is incredibly elegant, simple, and requires no syntax additions to our DSL!
        // Let's check:
        // Trigger: `msg.get_field(35) == mock_rule.fields[0].value`.
        // If it matches, we build and send a response using fields from `mock_rule.fields[1]` to the end!
        // Let's implement this! It's so clean.
        if (mock_rule.fields.empty()) continue;
        
        const auto& trigger = mock_rule.fields[0];
        auto actual_val = received_msg.get_field(trigger.tag);
        if (actual_val && *actual_val == trigger.value) {
            // Trigger matched!
            // Build response fields
            std::vector<Field> resp_fields;
            resp_fields.reserve(6 + mock_rule.fields.size() - 1);

            // Add standard session header
            Field header_buf[6];
            std::string time_str;
            std::string_view resp_msg_type = "8"; // default to ExecutionReport
            if (mock_rule.fields.size() > 1 && mock_rule.fields[1].tag == 35) {
                resp_msg_type = mock_rule.fields[1].value;
            }

            size_t header_count = m_session.prepare_header(resp_msg_type, header_buf, time_str);
            for (size_t i = 0; i < header_count; ++i) {
                resp_fields.push_back(header_buf[i]);
            }

            // Add response fields (skipping standard headers if they are overridden)
            for (size_t i = 1; i < mock_rule.fields.size(); ++i) {
                const auto& f = mock_rule.fields[i];
                if (f.tag == 8 || f.tag == 35 || f.tag == 49 || f.tag == 56 || f.tag == 34 || f.tag == 52) {
                    continue;
                }
                resp_fields.push_back(f);
            }

            char serialize_buf[4096];
            auto res = serialize_message(resp_fields, serialize_buf, SOH);
            if (res) {
                m_conn->send(*res);
                m_session.on_message_sent();
                return true;
            }
        }
    }
    return false;
}

} // namespace fixure
