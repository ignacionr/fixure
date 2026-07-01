#pragma once

#include <fixure/parser.hpp>
#include <string>
#include <vector>
#include <expected>
#include <ostream>

namespace fixure {

enum class ActionType {
    Connect,
    Send,
    Expect,
    Mock, // Automatically respond if a message matches a pattern
    Disconnect
};

std::string_view to_string(ActionType type);

inline std::ostream& operator<<(std::ostream& os, ActionType type) {
    os << to_string(type);
    return os;
}

struct ScenarioStep {
    ActionType type;
    std::vector<Field> fields; // Holds fields to send, or fields to validate/match
    
    // To support lifetime of string_views, we can store the underlying string values in a separate vector of std::string
    // so that the string_views inside fields point to valid memory.
    std::vector<std::string> string_pool;
};

class Scenario {
public:
    explicit Scenario(std::string name);

    const std::string& name() const { return m_name; }
    const std::vector<ScenarioStep>& steps() const { return m_steps; }

    // Fluent API Builder
    Scenario& connect();
    Scenario& send(std::span<const Field> fields);
    Scenario& expect(std::span<const Field> fields);
    Scenario& mock(std::span<const Field> fields);
    Scenario& disconnect();

    // DSL file parser
    static std::expected<Scenario, std::string> load_from_file(std::string_view file_path);
    static std::expected<Scenario, std::string> parse_dsl(std::string_view dsl_content);

private:
    std::string m_name;
    std::vector<ScenarioStep> m_steps;

    void add_step(ActionType type, std::span<const Field> fields);
};

} // namespace fixure
