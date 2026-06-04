#include "tool_router.h"
#include <stdexcept>

// ---------------------------------------------------------------------------
// ToolRouter implementation
// ---------------------------------------------------------------------------

std::vector<nlohmann::json> ToolRouter::dispatch(
    const nlohmann::json& response,
    const ToolRegistry& registry)
{
    std::vector<nlohmann::json> outputs;

    auto tool_calls = extract_tool_calls(response);

    for (auto& tc : tool_calls)
    {
        std::string tool_name = tc["function"]["name"].get<std::string>();
        auto args_json = tc["function"]["arguments"];

        // Parse arguments — handles both string and object (the llama quirk).
        nlohmann::json parsed = parse_arguments(args_json);

        // Find the matching tool.
        Tool* tool = find_tool(tool_name, registry);
        if (!tool)
        {
            Logger::get().log_tool_call(tool_name, "not_found");
            outputs.push_back({
                {"role", "tool"},
                {"tool_call_id", tc["id"]},
                {"content", "Error: tool not found"}
            });
            continue;
        }

        // Execute the tool.
        try
        {
            std::string result = tool->execute(parsed);
            outputs.push_back({
                {"role", "tool"},
                {"tool_call_id", tc["id"]},
                {"content", result}
            });
            Logger::get().log_tool_call(tool_name, result);
        }
        catch (const std::exception& e)
        {
            outputs.push_back({
                {"role", "tool"},
                {"tool_call_id", tc["id"]},
                {"content", std::string("Error: ") + e.what()}
            });
            Logger::get().log_tool_call(tool_name, std::string("error: ") + e.what());
        }
    }

    return outputs;
}

std::vector<nlohmann::json> ToolRouter::extract_tool_calls(const nlohmann::json& response)
{
    std::vector<nlohmann::json> tool_calls;

    if (response.contains("choices"))
    {
        for (auto& choice : response["choices"])
        {
            if (choice.contains("message") && choice["message"].contains("tool_calls"))
            {
                for (auto& tc : choice["message"]["tool_calls"])
                {
                    tool_calls.push_back(tc);
                }
            }
        }
    }
    // DEBUG: fprintf(stdout, "Found %zu tool calls\n", tool_calls.size());

    return tool_calls;
}

std::string ToolRouter::repair_json_backslashes(const std::string& input)
{
    std::string result;
    result.reserve(input.size() * 11 / 10);
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '\\') {
            // Check if it's a valid JSON escape sequence
            if (i + 1 < input.size()) {
                char next = input[i + 1];
                if (next == 'n' || next == 'r' || next == 't' || next == 'b' || next == 'f' || next == '"' || next == '\\' || next == '/' || next == 'u') {
                    result += '\\';
                    result += next;
                    i++;
                } else {
                    // Invalid escape sequence (likely a raw path backslash), escape it properly
                    result += "\\\\";
                }
            } else {
                result += "\\\\";
            }
        } else {
            result += input[i];
        }
    }
    return result;
}

nlohmann::json ToolRouter::parse_arguments(const nlohmann::json& args_json)
{
    // The llama-server quirk: arguments may be a string OR an object.
    if (args_json.is_string())
    {
        // Standard serialized JSON string.
        try {
            std::string str = args_json.get<std::string>();
            std::string repaired = repair_json_backslashes(str);
            nlohmann::json parsed = nlohmann::json::parse(repaired);
            if (parsed.is_object()) return parsed;
        } catch (...) {}
        return nlohmann::json::object();
    }
    else if (args_json.is_object())
    {
        return args_json;
    }
    else
    {
        return nlohmann::json::object();
    }
}

Tool* ToolRouter::find_tool(const std::string& name, const ToolRegistry& registry)
{
    return registry.by_name(name);
}
