#ifndef TOOL_H
#define TOOL_H

#include <string>
#include <functional>
#include <vector>
#include <nlohmann/json.hpp>

// Forward declare the JSON type used throughout the codebase.
using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Tool interface
//
// Every tool is a callable that receives the current conversation state
// and returns a result string.  Tool registration happens at boot time
// via a central ToolRegistry (see tool_router.h/cpp for the router that
// dispatches llama-server tool_calls into these handlers).
// ---------------------------------------------------------------------------

class Tool
{
public:
    virtual ~Tool() = default;

    // --- Metadata ----------------------------------------------------------------
    // Return the name of this tool as llama-server expects it in the schema.
    virtual std::string name() const = 0;

    // Return a short description that appears in the tool schema.
    virtual std::string description() const = 0;

    // --- Execution ---------------------------------------------------------------
    // Execute the tool with the arguments JSON that came from llama-server.
    // The return value is the content that gets appended back to the chat.
    virtual std::string execute(const json& arguments) = 0;

    // --- Schema generation -------------------------------------------------------
    // Return the nlohmann::json schema object that goes into the tools[] list
    // when constructing the Chat API request.
    virtual json schema() = 0;
};

// ---------------------------------------------------------------------------
// ToolRegistry
//
// Simple linear container – we keep it minimal.  No mutex here; the registry
// is accessed only from the background worker thread after construction.
// ---------------------------------------------------------------------------

class ToolRegistry
{
public:
    void register_tool(std::shared_ptr<Tool> tool);
    std::vector<std::shared_ptr<Tool>> all() const;
    Tool* by_name(const std::string& name) const;

    // Convert registered tools to the JSON format expected by llama-server.
    nlohmann::json to_json() const;

    ToolRegistry() = default;
    ToolRegistry(const ToolRegistry&) = delete;
    ToolRegistry& operator=(const ToolRegistry&) = delete;

private:
    std::vector<std::shared_ptr<Tool>> tools_;
};

#endif // TOOL_H
