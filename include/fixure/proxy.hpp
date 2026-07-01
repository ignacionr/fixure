#pragma once

#include <fixure/parser.hpp>
#include <fixure/dictionary.hpp>
#include <string>
#include <string_view>
#include <memory>
#include <unordered_map>
#include <chrono>
#include <atomic>

namespace fixure {

class Proxy {
public:
    struct LatencyTracker {
        std::chrono::steady_clock::time_point start_time;
        std::string msg_type;
    };

    Proxy(int local_port, std::string target_host, int target_port, std::optional<Dictionary> dict = std::nullopt);
    ~Proxy();

    // Starts the proxy. Blocks until stop() is called.
    bool start();
    void stop();
    int get_local_port() const { return m_local_port; }

private:
    int m_local_port;
    std::string m_target_host;
    int m_target_port;
    std::optional<Dictionary> m_dict;
    std::atomic<bool> m_running{false};
    int m_server_fd = -1;

    // Track latency of requests by tag (e.g. ClOrdID -> ExecutionReport)
    // Map of ClOrdID -> LatencyTracker
    std::unordered_map<std::string, LatencyTracker> m_order_latency;
    
    // Last sent client message seq num or type to associate responses generally
    std::chrono::steady_clock::time_point m_last_client_msg_time;
    std::string m_last_client_msg_type;

    void handle_connection(int client_fd);
    void forward_traffic(int src_fd, int dst_fd, bool is_client_to_server, std::atomic<bool>& connection_active);
    void process_bytes(std::string_view bytes, bool is_client_to_server);
    void analyze_message(const FixMessage& msg, bool is_client_to_server);
};

} // namespace fixure
