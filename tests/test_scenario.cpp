#include "test_framework.hpp"
#include <fixure/scenario.hpp>
#include <fixure/runner.hpp>
#include <fixure/session.hpp>
#include <thread>

TEST(ScenarioTest, ParseDsl) {
    std::string dsl = 
        "# Connect to SUT\n"
        "CONNECT\n"
        "EXPECT 35=A | 98=0\n"
        "SEND   35=A | 49=SUT | 56=MOCK\n"
        "MOCK   35=D | 35=8 | 39=2\n"
        "DISCONNECT\n";

    auto sc_res = fixure::Scenario::parse_dsl(dsl);
    ASSERT_TRUE(sc_res.has_value());

    const auto& sc = *sc_res;
    ASSERT_EQ(sc.steps().size(), 5);
    ASSERT_EQ(sc.steps()[0].type, fixure::ActionType::Connect);
    ASSERT_EQ(sc.steps()[1].type, fixure::ActionType::Expect);
    ASSERT_EQ(sc.steps()[1].fields.size(), 2);
    ASSERT_EQ(sc.steps()[1].fields[0].tag, 35);
    ASSERT_EQ(sc.steps()[1].fields[0].value, "A");
}

TEST(ScenarioTest, RunScenarioInMemory) {
    // Define scenario
    std::string dsl = 
        "CONNECT\n"
        "SEND   35=A | 98=0 | 108=30\n" // Client sends logon
        "EXPECT 35=A\n"                  // Client expects logon back
        "SEND   35=D | 55=AAPL | 54=1\n" // Client sends order
        "EXPECT 35=8 | 39=2\n"           // Client expects execution report (filled)
        "DISCONNECT\n";

    auto sc_res = fixure::Scenario::parse_dsl(dsl);
    ASSERT_TRUE(sc_res.has_value());

    auto conn = std::make_shared<fixure::InMemoryConnection>();
    fixure::SessionConfig config{
        .sender_comp_id = "CLIENT",
        .target_comp_id = "SERVER"
    };
    fixure::FixSession session(config);
    fixure::ScenarioRunner runner(session, conn);

    // Run SUT in a background thread
    std::thread sut_thread([conn]() {
        // Wait for connect
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        // 1. Receive Logon
        auto logon_raw = conn->server_receive(std::chrono::seconds(1));
        if (!logon_raw) return;

        // Send Logon back
        std::string logon_resp = "8=FIX.4.2\x01" "9=42\x01" "35=A\x01" "49=SERVER\x01" "56=CLIENT\x01" "34=1\x01" "98=0\x01" "108=30\x01" "10=188\x01";
        conn->server_send(logon_resp);

        // 2. Receive Order
        auto order_raw = conn->server_receive(std::chrono::seconds(1));
        if (!order_raw) return;

        // Send Execution Report (filled) back
        std::string exec_report = "8=FIX.4.2\x01" "9=41\x01" "35=8\x01" "49=SERVER\x01" "56=CLIENT\x01" "34=2\x01" "39=2\x01" "150=2\x01" "10=124\x01";
        conn->server_send(exec_report);
    });

    auto res = runner.run(*sc_res);
    
    if (sut_thread.joinable()) {
        sut_thread.join();
    }

    ASSERT_TRUE(res.success);
    ASSERT_EQ(res.steps_executed, 6);
}
