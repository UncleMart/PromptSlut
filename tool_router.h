#ifndef TOOL_ROUTER_H
#define TOOL_ROUTER_H

#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "Tool.h"
#include "logger.h"

// ---------------------------------------------------------------------------
// ToolRouter
//
// Dispatches tool_calls from the LLM response to registered tools.
//
// The Llama-Server Tool Quirk (Constraint #4):
//   Recent llama-server builds sometimes return tool_calls[].function.arguments
//   as a parsed JSON object instead of a serialized JSON string.
//   This function checks both is_string() and is_object() and handles
//   gracefully to prevent a parser crash.
//
// Thread safety:
//   - Called only from the worker thread.
//   - Logger is thread-safe (guarded internally).
// ---------------------------------------------------------------------------

class ToolRouter
{
public:
    // Parse a llama-server response and dispatch tool_calls to registered tools.
    // Returns the tool output messages to append to the conversation.
    static std::vector<nlohmann::json> dispatch(
        const nlohmann::json& response,
        const ToolRegistry& registry);

    // Extract tool calls from a response.
    static std::vector<nlohmann::json> extract_tool_calls(const nlohmann::json& response);

    // Repair unescaped backslashes in JSON strings (such as Windows paths)
    static std::string repair_json_backslashes(const std::string& input);

    // Parse arguments — handles the string vs object quirk.
    static nlohmann::json parse_arguments(const nlohmann::json& args_json);

private:
    static Tool* find_tool(const std::string& name, const ToolRegistry& registry);
};

#endif // TOOL_ROUTER_H
