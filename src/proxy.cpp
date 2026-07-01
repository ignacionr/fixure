#include <fixure/proxy.hpp>
#include <iostream>
#include <thread>
#include <print>
#include <cstring>

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
    if (separator_pos == std::string_view::npos) {
        return std::nullopt;
    }
    
    size_t msg_len = separator_pos + 1;
    std::string msg = buffer.substr(0, msg_len);
    buffer.erase(0, msg_len);
    return msg;
}
} // namespace

Proxy::Proxy(int local_port, std::string target_host, int target_port, std::optional<Dictionary> dict)
    : m_local_port(local_port), m_target_host(std::move(target_host)), m_target_port(target_port), m_dict(std::move(dict)) {
#if defined(_MSC_VER)
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

Proxy::~Proxy() {
    stop();
#if defined(_MSC_VER)
    WSACleanup();
#endif
}

bool Proxy::start() {
    m_running = true;

    m_server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_server_fd < 0) {
        std::println(std::cerr, "[PROXY] [ERROR] Socket creation failed");
        return false;
    }

    int opt = 1;
    ::setsockopt(m_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(m_local_port);

    if (::bind(m_server_fd, reinterpret_cast<struct sockaddr*>(&address), sizeof(address)) < 0) {
        std::println(std::cerr, "[PROXY] [ERROR] Bind failed on port {}", m_local_port);
        return false;
    }

    if (::listen(m_server_fd, 3) < 0) {
        std::println(std::cerr, "[PROXY] [ERROR] Listen failed");
        return false;
    }

    std::println(std::cout, "[PROXY] Listening on port {} and forwarding to {}:{}...", m_local_port, m_target_host, m_target_port);

    while (m_running) {
        struct sockaddr_in client_addr{};
        socklen_t addrlen = sizeof(client_addr);
        
        int client_fd = ::accept(m_server_fd, reinterpret_cast<struct sockaddr*>(&client_addr), &addrlen);
        if (client_fd < 0) {
            if (!m_running) break;
            continue;
        }

        std::println(std::cout, "[PROXY] New SUT / client connection accepted");
        
        // Handle each client connection in a separate thread so the proxy can accept subsequent connections
        std::thread([this, client_fd]() {
            this->handle_connection(client_fd);
        }).detach();
    }

    return true;
}

void Proxy::stop() {
    m_running = false;
    if (m_server_fd >= 0) {
#if !defined(_MSC_VER)
        ::close(m_server_fd);
#else
        ::closesocket(m_server_fd);
#endif
        m_server_fd = -1;
    }
}

void Proxy::handle_connection(int client_fd) {
    // Connect to the target server
    int target_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (target_fd < 0) {
        std::println(std::cerr, "[PROXY] [ERROR] Target socket creation failed");
        ::close(client_fd);
        return;
    }

    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    std::string port_str = std::to_string(m_target_port);
    
    if (::getaddrinfo(m_target_host.c_str(), port_str.c_str(), &hints, &res) != 0) {
        std::println(std::cerr, "[PROXY] [ERROR] Resolving target host {} failed", m_target_host);
        ::close(client_fd);
        ::close(target_fd);
        return;
    }

    if (::connect(target_fd, res->ai_addr, res->ai_addrlen) < 0) {
        std::println(std::cerr, "[PROXY] [ERROR] Connection to target {}:{} failed", m_target_host, m_target_port);
        ::freeaddrinfo(res);
        ::close(client_fd);
        ::close(target_fd);
        return;
    }

    ::freeaddrinfo(res);
    std::println(std::cout, "[PROXY] Successfully connected to target server {}:{}", m_target_host, m_target_port);

    std::atomic<bool> connection_active{true};

    // Spawn bidirectional traffic forwarders
    std::thread t1([this, client_fd, target_fd, &connection_active]() {
        this->forward_traffic(client_fd, target_fd, true, connection_active);
    });

    std::thread t2([this, target_fd, client_fd, &connection_active]() {
        this->forward_traffic(target_fd, client_fd, false, connection_active);
    });

    t1.join();
    t2.join();
    
    std::println(std::cout, "[PROXY] Client session closed");
}

void Proxy::forward_traffic(int src_fd, int dst_fd, bool is_client_to_server, std::atomic<bool>& connection_active) {
    char buf[4096];
    std::string rx_buffer;

    while (m_running && connection_active) {
        auto bytes_read = ::recv(src_fd, buf, sizeof(buf), 0);
        if (bytes_read <= 0) {
            connection_active = false;
            break;
        }

        rx_buffer.append(buf, static_cast<size_t>(bytes_read));

        // Parse and analyze messages from buffer
        while (true) {
            auto msg_opt = extract_next_message(rx_buffer, SOH);
            if (!msg_opt) {
                msg_opt = extract_next_message(rx_buffer, '|');
                if (!msg_opt) break;
            }

            auto parse_res = FixMessage::parse(*msg_opt, msg_opt->find('|') != std::string::npos ? '|' : SOH);
            if (parse_res) {
                analyze_message(*parse_res, is_client_to_server);
            } else {
                std::println(std::cerr, "[PROXY] [ERROR] Failed to parse message: {}", to_string(parse_res.error()));
            }
        }

        // Forward raw bytes to destination socket
        size_t total_sent = 0;
        size_t len = static_cast<size_t>(bytes_read);
        while (total_sent < len) {
            auto sent = ::send(dst_fd, buf + total_sent, len - total_sent, 0);
            if (sent <= 0) {
                connection_active = false;
                break;
            }
            total_sent += static_cast<size_t>(sent);
        }
    }

#if !defined(_MSC_VER)
    ::close(src_fd);
    ::close(dst_fd);
#else
    ::closesocket(src_fd);
    ::closesocket(dst_fd);
#endif
}

void Proxy::analyze_message(const FixMessage& msg, bool is_client_to_server) {
    auto msg_type_opt = msg.get_field(35);
    if (!msg_type_opt) return;

    std::string msg_type(msg_type_opt.value());

    if (is_client_to_server) {
        // Track Order request
        if (msg_type == "D") {
            auto cl_ord_id = msg.get_field(11);
            if (cl_ord_id) {
                m_order_latency[std::string(*cl_ord_id)] = LatencyTracker{
                    .start_time = std::chrono::steady_clock::now(),
                    .msg_type = "D"
                };
            }
        }
        
        m_last_client_msg_time = std::chrono::steady_clock::now();
        m_last_client_msg_type = msg_type;

        // Validation
        if (m_dict) {
            std::string err;
            if (!m_dict->validate_message(msg_type, msg, err)) {
                std::println(std::cerr, "\033[1;31m[PROXY] [INVALID] Client -> Server MsgType={} | Error: {}\033[0m", msg_type, err);
            } else {
                std::println(std::cout, "\033[1;32m[PROXY] [VALID] Client -> Server MsgType={}\033[0m", msg_type);
            }
        } else {
            std::println(std::cout, "[PROXY] Client -> Server MsgType={}", msg_type);
        }
    } 
    else {
        // Handle Server Response
        bool measured_order = false;
        
        if (msg_type == "8") {
            // Execution Report
            auto cl_ord_id = msg.get_field(11);
            if (cl_ord_id) {
                auto it = m_order_latency.find(std::string(*cl_ord_id));
                if (it != m_order_latency.end()) {
                    auto duration = std::chrono::steady_clock::now() - it->second.start_time;
                    auto micros = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
                    std::println(std::cout, "\033[1;36m[PROXY] [TIMING] ClOrdID {} Order Latency: {} µs\033[0m", *cl_ord_id, micros);
                    m_order_latency.erase(it);
                    measured_order = true;
                }
            }
        }

        if (!measured_order && !m_last_client_msg_type.empty()) {
            auto duration = std::chrono::steady_clock::now() - m_last_client_msg_time;
            auto micros = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
            std::println(std::cout, "[PROXY] [TIMING] Roundtrip Client {} -> Server {} Latency: {} µs", m_last_client_msg_type, msg_type, micros);
            m_last_client_msg_type = "";
        }

        // Validation
        if (m_dict) {
            std::string err;
            if (!m_dict->validate_message(msg_type, msg, err)) {
                std::println(std::cerr, "\033[1;31m[PROXY] [INVALID] Server -> Client MsgType={} | Error: {}\033[0m", msg_type, err);
            } else {
                std::println(std::cout, "\033[1;32m[PROXY] [VALID] Server -> Client MsgType={}\033[0m", msg_type);
            }
        } else {
            std::println(std::cout, "[PROXY] Server -> Client MsgType={}", msg_type);
        }
    }
}

} // namespace fixure
