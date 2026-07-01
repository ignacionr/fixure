#include "test_framework.hpp"
#include <fixure/proxy.hpp>
#include <fixure/runner.hpp>
#include <thread>
#include <chrono>

#if !defined(_MSC_VER)
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>
#else
#include <winsock2.h>
#endif

TEST(ProxyTest, EndToEndProxyForwardingAndTiming) {
    // We will bind to port 0 to let the OS assign an ephemeral port dynamically.
    // This avoids port conflicts in CI parallel runners.
    int assigned_sut_port = 0;
    std::atomic<bool> sut_started{false};

    // 1. Start SUT Mock Server in background thread
    std::thread sut_thread([&assigned_sut_port, &sut_started]() {
        int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) return;
        
        int opt = 1;
        ::setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        struct sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = ::inet_addr("127.0.0.1");
        address.sin_port = htons(static_cast<uint16_t>(0)); // Dynamic ephemeral port allocation
        
        if (::bind(server_fd, reinterpret_cast<struct sockaddr*>(&address), sizeof(address)) < 0) {
            ::close(server_fd);
            return;
        }
        
        // Resolve the assigned port
        socklen_t addr_len = sizeof(address);
        if (::getsockname(server_fd, reinterpret_cast<struct sockaddr*>(&address), &addr_len) == 0) {
            assigned_sut_port = ntohs(address.sin_port);
        }
        
        if (::listen(server_fd, 1) < 0) {
            ::close(server_fd);
            return;
        }
        
        sut_started = true;

        // Use select with a 3-second timeout to avoid blocking indefinitely
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(server_fd, &rfds);
        struct timeval tv{};
        tv.tv_sec = 3;
        
        int retval = ::select(server_fd + 1, &rfds, nullptr, nullptr, &tv);
        if (retval > 0) {
            struct sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            int client_fd = ::accept(server_fd, reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);
            if (client_fd >= 0) {
                // Select on client_fd for read
                FD_ZERO(&rfds);
                FD_SET(client_fd, &rfds);
                struct timeval client_tv{};
                client_tv.tv_sec = 3;
                
                int client_val = ::select(client_fd + 1, &rfds, nullptr, nullptr, &client_tv);
                if (client_val > 0) {
                    char buf[512];
                    auto bytes = ::recv(client_fd, buf, sizeof(buf), 0);
                    if (bytes > 0) {
                        // Send logon response back
                        std::string logon_resp = "8=FIX.4.2\x01" "9=42\x01" "35=A\x01" "49=SERVER\x01" "56=CLIENT\x01" "34=1\x01" "98=0\x01" "108=30\x01" "10=188\x01";
                        ::send(client_fd, logon_resp.data(), logon_resp.size(), 0);
                    }
                }
                ::close(client_fd);
            }
        }
        ::close(server_fd);
    });

    // Wait for SUT to bind and start listening
    for (int i = 0; i < 50; ++i) {
        if (sut_started && assigned_sut_port > 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    ASSERT_TRUE(sut_started);
    ASSERT_TRUE(assigned_sut_port > 0);

    // 2. Start Proxy in background thread binding to local port 0 (dynamic)
    fixure::Proxy proxy(0, "127.0.0.1", assigned_sut_port);
    std::thread proxy_thread([&proxy]() {
        proxy.start();
    });

    // Wait for proxy to resolve its local port
    int assigned_proxy_port = 0;
    for (int i = 0; i < 50; ++i) {
        assigned_proxy_port = proxy.get_local_port();
        if (assigned_proxy_port > 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    ASSERT_TRUE(assigned_proxy_port > 0);

    // 3. Client connects to Proxy
    int client_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(static_cast<uint16_t>(assigned_proxy_port));
    ::inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

    int conn_res = ::connect(client_fd, reinterpret_cast<struct sockaddr*>(&serv_addr), sizeof(serv_addr));
    ASSERT_EQ(conn_res, 0);

    // Send Logon
    std::string logon = "8=FIX.4.2\x01" "9=42\x01" "35=A\x01" "49=CLIENT\x01" "56=SERVER\x01" "34=1\x01" "98=0\x01" "108=30\x01" "10=188\x01";
    auto sent = ::send(client_fd, logon.data(), logon.size(), 0);
    ASSERT_EQ(sent, static_cast<ssize_t>(logon.size()));

    // Read Response with select timeout (3 seconds)
    fd_set client_fds;
    FD_ZERO(&client_fds);
    FD_SET(client_fd, &client_fds);
    struct timeval client_tv{};
    client_tv.tv_sec = 3;

    int client_select = ::select(client_fd + 1, &client_fds, nullptr, nullptr, &client_tv);
    ASSERT_TRUE(client_select > 0);

    char buf[512];
    auto received = ::recv(client_fd, buf, sizeof(buf), 0);
    ASSERT_TRUE(received > 0);

    std::string_view response(buf, static_cast<size_t>(received));
    ASSERT_TRUE(response.starts_with("8=FIX.4.2"));
    ASSERT_TRUE(response.contains("35=A")); // Logon response

    // Clean up
    ::close(client_fd);
    proxy.stop();

    if (sut_thread.joinable()) sut_thread.join();
    if (proxy_thread.joinable()) proxy_thread.join();
}
