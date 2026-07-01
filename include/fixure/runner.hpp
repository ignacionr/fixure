#pragma once

#include <fixure/scenario.hpp>
#include <fixure/session.hpp>
#include <chrono>
#include <memory>
#include <string>
#include <array>
#include <queue>
#include <mutex>
#include <condition_variable>

namespace fixure {

class Connection {
public:
    virtual ~Connection() = default;
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool send(std::string_view data) = 0;
    virtual std::optional<std::string> receive(std::chrono::milliseconds timeout) = 0;
};

// In-Memory Bidirectional Connection for unit tests and local mock-ups
class InMemoryConnection : public Connection {
public:
    InMemoryConnection();

    bool connect() override;
    void disconnect() override;
    bool send(std::string_view data) override;
    std::optional<std::string> receive(std::chrono::milliseconds timeout) override;

    // Server-side interface (for SUT mockup)
    bool server_send(std::string_view data);
    std::optional<std::string> server_receive(std::chrono::milliseconds timeout);

private:
    std::queue<std::string> m_to_server;
    std::queue<std::string> m_to_client;
    std::mutex m_mutex;
    std::condition_variable m_cv_client;
    std::condition_variable m_cv_server;
    bool m_connected = false;
};

// TCP Socket Connection (standard for FIX integration testing)
class TcpConnection : public Connection {
public:
    TcpConnection(std::string host, int port);
    ~TcpConnection() override;

    bool connect() override;
    void disconnect() override;
    bool send(std::string_view data) override;
    std::optional<std::string> receive(std::chrono::milliseconds timeout) override;

private:
    std::string m_host;
    int m_port;
    int m_socket = -1;
};

class ScenarioRunner {
public:
    struct Result {
        bool success = false;
        std::string error_message;
        size_t steps_executed = 0;
    };

    ScenarioRunner(FixSession& session, std::shared_ptr<Connection> conn);

    Result run(const Scenario& scenario);

private:
    using StepHandler = bool (ScenarioRunner::*)(const ScenarioStep&, Result&, std::string&, char);

    FixSession& m_session;
    std::shared_ptr<Connection> m_conn;
    std::vector<ScenarioStep> m_active_mocks;

    bool handle_connect_step(const ScenarioStep& step, Result& result, std::string& rx_buffer, char separator);
    bool handle_disconnect_step(const ScenarioStep& step, Result& result, std::string& rx_buffer, char separator);
    bool handle_send_step(const ScenarioStep& step, Result& result, std::string& rx_buffer, char separator);
    bool handle_expect_step(const ScenarioStep& step, Result& result, std::string& rx_buffer, char separator);
    bool handle_mock_step(const ScenarioStep& step, Result& result, std::string& rx_buffer, char separator);

    bool match_fields(const FixMessage& msg, std::span<const Field> expected) const;
    bool process_mocks(const FixMessage& received_msg);
};

} // namespace fixure
