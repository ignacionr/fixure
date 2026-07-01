#include <fixure/parser.hpp>
#include <fixure/dictionary.hpp>
#include <fixure/scenario.hpp>
#include <fixure/runner.hpp>
#include <fixure/replay.hpp>
#include <fixure/proxy.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string_view>
#include <print>
#include <vector>

void print_usage() {
    std::println(std::cerr, "fixure: FIX Protocol test harness and mock container");
    std::println(std::cerr, "Usage:");
    std::println(std::cerr, "  fixure validate <msg_string_or_file> [--dict <xml_dict_path>] [--separator <char>]");
    std::println(std::cerr, "  fixure run <scenario_file> <host> <port>");
    std::println(std::cerr, "  fixure replay <log_file> <host> <port> [--preserve-timing]");
    std::println(std::cerr, "  fixure proxy <local_port> <target_host> <target_port> [--dict <xml_dict_path>]");
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        print_usage();
        return 1;
    }

    std::string_view command = argv[1];

    if (command == "validate") {
        std::string target = argv[2];
        std::string dict_path = "";
        char separator = fixure::SOH;

        for (int i = 3; i < argc; ++i) {
            if (std::string_view(argv[i]) == "--dict" && i + 1 < argc) {
                dict_path = argv[++i];
            } else if (std::string_view(argv[i]) == "--separator" && i + 1 < argc) {
                std::string_view sep = argv[++i];
                if (sep == "|" || sep == "\\x01") {
                    separator = (sep == "|") ? '|' : '\x01';
                } else if (!sep.empty()) {
                    separator = sep[0];
                }
            }
        }

        // Try reading target as file; if not found, use as raw string
        std::string raw_content = target;
        std::ifstream file(target);
        if (file.is_open()) {
            std::stringstream buffer;
            buffer << file.rdbuf();
            raw_content = buffer.str();
        }

        // Parse message
        auto parse_res = fixure::FixMessage::parse(raw_content, separator);
        if (!parse_res) {
            std::println(std::cerr, "FAIL: Parse failed: {}", to_string(parse_res.error()));
            return 1;
        }

        std::println(std::cout, "PASS: Message parsed successfully. Field count: {}", parse_res->field_count());
        for (const auto& field : parse_res->fields()) {
            std::println(std::cout, "  Tag {:<4} = {}", field.tag, field.value);
        }

        if (!dict_path.empty()) {
            auto dict_res = fixure::Dictionary::load_xml(dict_path);
            if (!dict_res) {
                std::println(std::cerr, "FAIL: Could not load data dictionary: {}", dict_res.error());
                return 1;
            }
            
            auto msg_type_opt = parse_res->get_field(35);
            if (!msg_type_opt) {
                std::println(std::cerr, "FAIL: MsgType (35) tag missing in message");
                return 1;
            }

            std::string err;
            if (!dict_res->validate_message(*msg_type_opt, *parse_res, err)) {
                std::println(std::cerr, "FAIL: Message validation failed: {}", err);
                return 1;
            }
            std::println(std::cout, "PASS: Message is valid according to data dictionary.");
        }
        return 0;
    } 
    else if (command == "run") {
        std::string scenario_file = argv[2];
        if (argc < 5) {
            print_usage();
            return 1;
        }
        std::string host = argv[3];
        int port = std::stoi(argv[4]);

        auto sc_res = fixure::Scenario::load_from_file(scenario_file);
        if (!sc_res) {
            std::println(std::cerr, "FAIL: Could not load scenario: {}", sc_res.error());
            return 1;
        }

        fixure::SessionConfig config{
            .sender_comp_id = "FIXURE_CLIENT",
            .target_comp_id = "SUT"
        };
        fixure::FixSession session(config);
        auto conn = std::make_shared<fixure::TcpConnection>(host, port);
        fixure::ScenarioRunner runner(session, conn);

        std::println(std::cout, "Running scenario '{}' against {}:{}...", scenario_file, host, port);
        auto result = runner.run(*sc_res);

        if (result.success) {
            std::println(std::cout, "PASS: Scenario executed successfully ({} steps).", result.steps_executed);
            return 0;
        } else {
            std::println(std::cerr, "FAIL: Scenario execution failed: {}", result.error_message);
            return 1;
        }
    } 
    else if (command == "replay") {
        std::string log_file = argv[2];
        if (argc < 5) {
            print_usage();
            return 1;
        }
        std::string host = argv[3];
        int port = std::stoi(argv[4]);
        bool preserve_timing = false;

        for (int i = 5; i < argc; ++i) {
            if (std::string_view(argv[i]) == "--preserve-timing") {
                preserve_timing = true;
            }
        }

        auto conn = std::make_shared<fixure::TcpConnection>(host, port);
        if (!conn->connect()) {
            std::println(std::cerr, "FAIL: Could not connect to target {}:{}", host, port);
            return 1;
        }

        fixure::SessionReplayer replayer(conn);
        std::println(std::cout, "Replaying logs from '{}' to {}:{}...", log_file, host, port);
        auto result = replayer.replay_file(log_file, fixure::SOH, preserve_timing);

        if (result.success) {
            std::println(std::cout, "PASS: Replayed {} messages.", result.messages_sent);
            return 0;
        } else {
            std::println(std::cerr, "FAIL: Replay failed: {}", result.error_message);
            return 1;
        }
    }
    else if (command == "proxy") {
        if (argc < 5) {
            print_usage();
            return 1;
        }
        int local_port = std::stoi(argv[2]);
        std::string target_host = argv[3];
        int target_port = std::stoi(argv[4]);
        
        std::optional<fixure::Dictionary> dict;
        for (int i = 5; i < argc; ++i) {
            if (std::string_view(argv[i]) == "--dict" && i + 1 < argc) {
                std::string dict_path = argv[++i];
                auto dict_res = fixure::Dictionary::load_xml(dict_path);
                if (!dict_res) {
                    std::println(std::cerr, "FAIL: Could not load data dictionary: {}", dict_res.error());
                    return 1;
                }
                dict = *dict_res;
            }
        }

        fixure::Proxy proxy(local_port, target_host, target_port, dict);
        if (!proxy.start()) {
            return 1;
        }
        return 0;
    }

    print_usage();
    return 1;
}
