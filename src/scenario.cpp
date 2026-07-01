#include <fixure/scenario.hpp>
#include <fstream>
#include <sstream>
#include <charconv>
#include <algorithm>

namespace fixure {

std::string_view to_string(ActionType type) {
    switch (type) {
        case ActionType::Connect: return "CONNECT";
        case ActionType::Send: return "SEND";
        case ActionType::Expect: return "EXPECT";
        case ActionType::Mock: return "MOCK";
        case ActionType::Disconnect: return "DISCONNECT";
    }
    return "UNKNOWN";
}

namespace {
std::string_view trim(std::string_view str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}
} // namespace

Scenario::Scenario(std::string name) : m_name(std::move(name)) {}

void Scenario::add_step(ActionType type, std::span<const Field> fields) {
    ScenarioStep step;
    step.type = type;
    step.string_pool.reserve(fields.size());
    step.fields.reserve(fields.size());
    
    for (const auto& f : fields) {
        step.string_pool.push_back(std::string(f.value));
    }
    for (size_t i = 0; i < fields.size(); ++i) {
        step.fields.push_back(Field{
            .tag = fields[i].tag,
            .value = step.string_pool[i]
        });
    }
    m_steps.push_back(std::move(step));
}

Scenario& Scenario::connect() {
    add_step(ActionType::Connect, {});
    return *this;
}

Scenario& Scenario::send(std::span<const Field> fields) {
    add_step(ActionType::Send, fields);
    return *this;
}

Scenario& Scenario::expect(std::span<const Field> fields) {
    add_step(ActionType::Expect, fields);
    return *this;
}

Scenario& Scenario::mock(std::span<const Field> fields) {
    add_step(ActionType::Mock, fields);
    return *this;
}

Scenario& Scenario::disconnect() {
    add_step(ActionType::Disconnect, {});
    return *this;
}

std::expected<Scenario, std::string> Scenario::load_from_file(std::string_view file_path) {
    std::ifstream file{std::string(file_path)};
    if (!file.is_open()) {
        return std::unexpected("Could not open scenario file: " + std::string(file_path));
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return parse_dsl(buffer.str());
}

std::expected<Scenario, std::string> Scenario::parse_dsl(std::string_view dsl) {
    Scenario sc("ParsedScenario");
    size_t line_num = 0;
    size_t pos = 0;

    while (pos < dsl.length()) {
        size_t next_line = dsl.find('\n', pos);
        std::string_view line_raw = (next_line == std::string_view::npos) 
            ? dsl.substr(pos) 
            : dsl.substr(pos, next_line - pos);
        
        pos = (next_line == std::string_view::npos) ? dsl.length() : next_line + 1;
        line_num++;

        std::string_view line = trim(line_raw);
        if (line.empty() || line.starts_with("#")) {
            continue;
        }

        // Get command (first word)
        size_t space_pos = line.find_first_of(" \t");
        std::string_view cmd = (space_pos == std::string_view::npos) ? line : line.substr(0, space_pos);
        std::string_view args = (space_pos == std::string_view::npos) ? "" : line.substr(space_pos);
        args = trim(args);

        ActionType type;
        if (cmd == "CONNECT") type = ActionType::Connect;
        else if (cmd == "SEND") type = ActionType::Send;
        else if (cmd == "EXPECT") type = ActionType::Expect;
        else if (cmd == "MOCK") type = ActionType::Mock;
        else if (cmd == "DISCONNECT") type = ActionType::Disconnect;
        else {
            return std::unexpected("Line " + std::to_string(line_num) + ": Unknown command " + std::string(cmd));
        }

        std::vector<Field> fields;
        std::vector<std::string> val_pool;

        // Parse fields
        if (!args.empty()) {
            size_t field_pos = 0;
            while (field_pos < args.length()) {
                size_t next_pipe = args.find('|', field_pos);
                // Also support SOH as delimiter in file
                if (next_pipe == std::string_view::npos) {
                    next_pipe = args.find('\x01', field_pos);
                }

                std::string_view token = (next_pipe == std::string_view::npos)
                    ? args.substr(field_pos)
                    : args.substr(field_pos, next_pipe - field_pos);
                
                field_pos = (next_pipe == std::string_view::npos) ? args.length() : next_pipe + 1;
                
                token = trim(token);
                if (token.empty()) continue;

                size_t eq_pos = token.find('=');
                if (eq_pos == std::string_view::npos) {
                    return std::unexpected("Line " + std::to_string(line_num) + ": Invalid field format '" + std::string(token) + "'");
                }

                std::string_view tag_str = trim(token.substr(0, eq_pos));
                std::string_view val_str = trim(token.substr(eq_pos + 1));

                int tag = 0;
                auto [ptr, ec] = std::from_chars(tag_str.data(), tag_str.data() + tag_str.size(), tag);
                if (ec != std::errc{}) {
                    return std::unexpected("Line " + std::to_string(line_num) + ": Invalid tag number '" + std::string(tag_str) + "'");
                }

                val_pool.push_back(std::string(val_str));
                fields.push_back(Field{tag, val_str});
            }
        }

        // Add the step to the scenario
        // Since we need stable views, we recreate field views using the new pool inside add_step
        sc.add_step(type, fields);
    }

    return sc;
}

} // namespace fixure
