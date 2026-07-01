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
#else
#include <winsock2.h>
#endif

TEST(ProxyTest, EndToEndProxyForwardingAndTiming) {
    int port_sut = 9001;
    int port_proxy = 9002;

    // 1. Start SUT Mock Server in background thread
    std::thread sut_thread([port_sut]() {
        int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) return;
        
        int opt = 1;
        ::setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        struct sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port_sut);
        
        if (::bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
            ::close(server_fd);
            return;
        }
        if (::listen(server_fd, 1) < 0) {
            ::close(server_fd);
            return;
        }
        
        struct sockaddr_in client_addr{};
        socklen_t addrlen = sizeof(client_addr);
        int client_fd = ::accept(server_fd, (struct sockaddr*)&client_addr, &addrlen);
        if (client_fd >= 0) {
            char buf[512];
            auto bytes = ::recv(client_fd, buf, sizeof(buf), 0);
            if (bytes > 0) {
                // Send logon response back
                std::string logon_resp = "8=FIX.4.2\x01" "9=42\x01" "35=A\x01" "49=SERVER\x01" "56=CLIENT\x01" "34=1\x01" "98=0\x01" "108=30\x01" "10=188\x01";
                ::send(client_fd, logon_resp.data(), logon_resp.size(), 0);
            }
            ::close(client_fd);
        }
        ::close(server_fd);
    });

    // 2. Start Proxy in background thread
    fixure::Proxy proxy(port_proxy, "127.0.0.1", port_sut);
    std::thread proxy_thread([&proxy]() {
        proxy.start();
    });

    // Wait a brief moment for servers to start
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // 3. Client connects to Proxy
    int client_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port_proxy);
    ::inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

    int conn_res = ::connect(client_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
    ASSERT_EQ(conn_res, 0);

    // Send Logon
    std::string logon = "8=FIX.4.2\x01" "9=42\x01" "35=A\x01" "49=CLIENT\x01" "56=SERVER\x01" "34=1\x01" "98=0\x01" "108=30\x01" "10=188\x01";
    auto sent = ::send(client_fd, logon.data(), logon.size(), 0);
    ASSERT_EQ(sent, static_cast<ssize_t>(logon.size()));

    // Read Response
    char buf[512];
    auto received = ::recv(client_fd, buf, sizeof(buf), 0);
    ASSERT_TRUE(received > 0);

    std::string_view response(buf, received);
    ASSERT_TRUE(response.starts_with("8=FIX.4.2"));
    ASSERT_TRUE(response.contains("35=A")); // Logon response

    // Clean up
    ::close(client_fd);
    proxy.stop();

    if (sut_thread.joinable()) sut_thread.join();
    if (proxy_thread.joinable()) proxy_thread.join();
}
