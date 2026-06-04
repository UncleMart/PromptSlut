#include "worker.h"
#include "tool_router.h"
#include "logger.h"
#include <mutex>
#include <chrono>
#include <iostream>
#include <memory>
#include <deque>
#include <thread>
#include <atomic>
#include <regex>
#include <random>

// ---------------------------------------------------------------------------
// Worker helpers
// ---------------------------------------------------------------------------

static std::string generate_uuid() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    const char* hex = "0123456789abcdef";
    std::string uuid = "call_";
    for (int i = 0; i < 16; ++i) {
        uuid += hex[dis(gen)];
    }
    return uuid;
}

static std::string get_tools_cheat_sheet(const ToolRegistry& registry) {
    std::string sheet = "\n\n=== LOCAL TOOL SYSTEM ACCESS (MANDATORY INSTRUCTIONS) ===\n"
                        "You are running in a native C++ harness (\"PromptSlut\") with direct access to local desktop tools.\n"
                        "To perform tasks like searching, reading/writing files, running commands, or accessing the clipboard, you MUST use these tools.\n\n"
                        "AVAILABLE TOOLS:\n";
    
    for (const auto& tool : registry.all()) {
        sheet += "- **" + tool->name() + "**: " + tool->description() + "\n";
        sheet += "  Schema parameters: " + tool->schema().dump() + "\n";
    }

    sheet += "\nCRITICAL CALLING RULES:\n"
             "1. Native Tool Calling: If your API/model runner supports native tool calls, output them via native function-calling.\n"
             "2. Fallback XML Tool Calling: If you want to use a tool, or if native tool-calling is not fully supported or is failing, you can invoke tools by writing explicit XML tags in your main response content. The harness will intercept, execute them, and feed the results back. Use this exact format:\n"
             "   <tool_call name=\"TOOL_NAME\">\n"
             "   {\n"
             "     \"param_name\": \"value\"\n"
             "   }\n"
             "   </tool_call>\n"
             "   For example, to read a file:\n"
             "   <tool_call name=\"file\">\n"
             "   {\n"
             "     \"path\": \"README.md\",\n"
             "     \"op\": \"read\"\n"
             "   }\n"
             "   </tool_call>\n"
             "3. Sequential Calls: You can call multiple tools in a single turn if needed. Always wait for the tool output before proceeding with your final text response.\n"
             "4. Path Resolution: You are running on Windows. You can use relative paths (e.g. \"README.md\" or \"src/main.cpp\"); they are automatically resolved relative to the project workspace root. Always use double backslashes \"\\\\\" or forward slashes \"/\" in paths inside your JSON arguments (e.g., \"C:\\\\PromptSlut\\\\README.md\" or \"C:/PromptSlut/README.md\").\n"
             "5. File Edits: When using the 'edit' tool, the 'old_string' must exist exactly in the file (newlines will be normalized automatically, but indentation and spacing must match). Specify a unique and sufficiently large block to avoid matching multiple locations.\n"
             "=========================================================\n";
    return sheet;
}

// ---------------------------------------------------------------------------
// Worker implementation
// ---------------------------------------------------------------------------

Worker::Worker()
{
    thread_ = std::thread(&Worker::worker_loop, this);
}

Worker::~Worker()
{
    set_stop();
    if (thread_.joinable())
        thread_.join();
}

void Worker::set_tool_registry(const std::vector<nlohmann::json>& tool_defs)
{
    std::lock_guard lock(mutex_);
    tool_defs_ = tool_defs;
}

void Worker::register_tool(std::shared_ptr<Tool> tool)
{
    std::lock_guard lock(mutex_);
    tool_registry_.register_tool(tool);
}

bool Worker::push_request(
    std::vector<nlohmann::json> messages,
    const std::string& host,
    int port,
    const std::string& api_key,
    const std::string& model,
    std::function<void(const std::string& chunk)> on_stream_chunk,
    std::function<void(const std::string& tool_name, const std::string& result)> on_tool_result,
    std::function<void(const std::string&)> on_error,
    std::function<void(const std::string& reasoning)> on_reasoning,
    std::function<void(const std::string& tool_name, const std::string& call_id)> on_tool_started,
    std::function<void(const std::vector<nlohmann::json>&)> on_complete,
    std::function<void(double elapsed, int prompt_t, int completion_t, int total_t)> on_stats,
    bool include_tools)
{
    std::lock_guard lock(mutex_);
    if (stop_)
        return false;

    Request req;
    req.messages = std::move(messages);
    req.host = host;
    req.port = port;
    req.api_key = api_key;
    req.model = model;
    req.on_error = std::move(on_error);
    req.on_stream_chunk = std::move(on_stream_chunk);
    req.on_tool_result = std::move(on_tool_result);
    req.on_reasoning = std::move(on_reasoning);
    req.on_tool_started = std::move(on_tool_started);
    req.on_complete = std::move(on_complete);
    req.on_stats = std::move(on_stats);
    
    if (include_tools) {
        req.tools = tool_defs_;
    } else {
        req.tools.clear();
    }

    stop_current_request_ = false;

    queue_.push_back(std::move(req));
    cond_.notify_one();
    return true;
}

void Worker::set_stop()
{
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    cond_.notify_one();
}

void Worker::worker_loop()
{
    Logger::get().log_message("debug", "Worker loop started");
    while (true)
    {
        Request req;
        {
            std::unique_lock lock(mutex_);
            cond_.wait(lock, [this]() { return !queue_.empty() || stop_; });
            if (stop_ && queue_.empty())
                return;
            req = std::move(queue_.front());
            queue_.pop_front();
        }
        Logger::get().log_message("debug", "Worker processing request...");
        try
        {
            process_request(req);
        }
        catch (const std::exception& e)
        {
            std::string err_msg = std::string("Error: ") + e.what();
            Logger::get().log_message("error", err_msg);
            if (req.on_error)
                req.on_error(err_msg);
        }
        catch (...)
        {
            Logger::get().log_message("error", "Unknown crash caught in worker_loop!");
        }
    }
}

void Worker::process_request(const Request& req)
{
    Logger::get().log_message("debug", "process_request start");
    auto messages = req.messages;

    // Inject dynamic tools cheat sheet if tools are included
    if (!req.tools.empty()) {
        std::string sheet = get_tools_cheat_sheet(tool_registry_);
        if (!messages.empty() && messages[0]["role"] == "system") {
            std::string content = messages[0]["content"].get<std::string>();
            messages[0]["content"] = content + sheet;
        } else {
            nlohmann::json system_msg;
            system_msg["role"] = "system";
            system_msg["content"] = sheet;
            messages.insert(messages.begin(), system_msg);
        }
    }
    std::string final_content;
    int max_tool_calls = 20;
    int iteration = 0;

    while (iteration < max_tool_calls)
    {
        if (stop_current_request_)
        {
            Logger::get().log_message("debug", "Request interrupted");
            if (req.on_error)
                req.on_error("Request interrupted by user.");
            return;
        }

        iteration++;
        Logger::get().log_message("debug", "Iteration " + std::to_string(iteration));

        HttpForwarder http(req.host, req.port, req.api_key);

        nlohmann::json resp;
        auto start_time = std::chrono::high_resolution_clock::now();
        try
        {
            Logger::get().log_message("debug", "calling chat_completion...");
            resp = http.chat_completion(messages, req.tools, req.model);
            Logger::get().log_message("debug", "chat_completion returned");
        }
        catch (const std::exception& e)
        {
            Logger::get().log_message("error", std::string("chat_completion exception: ") + e.what());
            if (req.on_error)
                req.on_error(std::string("Network error: ") + e.what());
            return;
        }
        auto end_time = std::chrono::high_resolution_clock::now();
        double elapsed_seconds = std::chrono::duration<double>(end_time - start_time).count();

        if (resp.contains("usage") && req.on_stats) {
            int prompt_t = 0, completion_t = 0, total_t = 0;
            auto& usage = resp["usage"];
            if (usage.contains("prompt_tokens") && !usage["prompt_tokens"].is_null()) {
                prompt_t = usage["prompt_tokens"].get<int>();
            }
            if (usage.contains("completion_tokens") && !usage["completion_tokens"].is_null()) {
                completion_t = usage["completion_tokens"].get<int>();
            }
            if (usage.contains("total_tokens") && !usage["total_tokens"].is_null()) {
                total_t = usage["total_tokens"].get<int>();
            }
            req.on_stats(elapsed_seconds, prompt_t, completion_t, total_t);
        }

        if (resp.contains("choices") && !resp["choices"].empty())
        {
            auto& msg = resp["choices"][0]["message"];
        if (msg.contains("reasoning") && !msg["reasoning"].is_null())
        {
            std::string reasoning = msg["reasoning"].get<std::string>();
            if (!reasoning.empty() && req.on_reasoning)
                req.on_reasoning(reasoning);
        }
        else if (msg.contains("reasoning_content") && !msg["reasoning_content"].is_null())
        {
            std::string reasoning = msg["reasoning_content"].get<std::string>();
            if (!reasoning.empty() && req.on_reasoning)
                req.on_reasoning(reasoning);
        }

        }

        // Intercept XML/fallback tool calls from text response before extracting tool calls!
        if (resp.contains("choices") && !resp["choices"].empty())
        {
            auto& msg = resp["choices"][0]["message"];
            if (msg.contains("content") && msg["content"].is_string())
            {
                std::string content = msg["content"].get<std::string>();
                if (content.find("<tool_call") != std::string::npos) {
                    std::vector<nlohmann::json> fallback_calls;
                    std::regex tag_regex("<tool_call\\s+name=\"([^\"]+)\"\\s*>([\\s\\S]*?)</tool_call>");
                    auto words_begin = std::sregex_iterator(content.begin(), content.end(), tag_regex);
                    auto words_end = std::sregex_iterator();

                    std::string cleaned_content = content;
                    size_t offset_adjustment = 0;

                    for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
                        std::smatch match = *i;
                        std::string tool_name = match[1].str();
                        std::string args_str = match[2].str();

                        // Trim args_str
                        args_str.erase(0, args_str.find_first_not_of(" \t\r\n"));
                        args_str.erase(args_str.find_last_not_of(" \t\r\n") + 1);

                        nlohmann::json tc;
                        tc["id"] = generate_uuid();
                        tc["type"] = "function";
                        tc["function"]["name"] = tool_name;
                        
                        try {
                            std::string repaired = ToolRouter::repair_json_backslashes(args_str);
                            tc["function"]["arguments"] = nlohmann::json::parse(repaired);
                        } catch (...) {
                            tc["function"]["arguments"] = args_str;
                        }

                        fallback_calls.push_back(tc);

                        size_t start_pos = match.position() - offset_adjustment;
                        size_t len = match.length();
                        cleaned_content.erase(start_pos, len);
                        offset_adjustment += len;
                    }

                    if (!fallback_calls.empty()) {
                        msg["content"] = cleaned_content;
                        if (!msg.contains("tool_calls") || msg["tool_calls"].is_null()) {
                            msg["tool_calls"] = nlohmann::json::array();
                        }
                        for (auto& tc : fallback_calls) {
                            msg["tool_calls"].push_back(tc);
                        }
                    }
                }
            }
        }

        Logger::get().log_message("debug", "extracting tool calls...");
        auto tool_calls = ToolRouter::extract_tool_calls(resp);

        if (!tool_calls.empty() && req.on_tool_started)
        {
            for (auto& tc : tool_calls)
            {
                std::string tool_name = tc["function"]["name"].get<std::string>();
                std::string tool_call_id = tc["id"].get<std::string>();
                req.on_tool_started(tool_name, tool_call_id);
            }
        }

        if (!tool_calls.empty())
        {
            Logger::get().log_message("debug", "dispatching " + std::to_string(tool_calls.size()) + " tools...");
            messages.push_back(resp["choices"][0]["message"]);

            auto outputs = ToolRouter::dispatch(resp, tool_registry_);

            for (auto& output : outputs)
            {
                std::string tool_call_id = output["tool_call_id"].get<std::string>();
                std::string result = output["content"].get<std::string>();

                std::string fname;
                for (auto& tc : tool_calls)
                {
                    if (tc["id"] == tool_call_id)
                    {
                        fname = tc["function"]["name"].get<std::string>();
                        break;
                    }
                }

                if (req.on_tool_result)
                    req.on_tool_result(fname, result);

                nlohmann::json tool_msg;
                tool_msg["role"] = "tool";
                tool_msg["tool_call_id"] = tool_call_id;
                tool_msg["content"] = result;
                messages.push_back(tool_msg);
            }

            continue;
        }

        if (resp.contains("choices") && !resp["choices"].empty())
        {
            auto& msg = resp["choices"][0]["message"];
            if (msg.contains("content"))
            {
                final_content = msg["content"].get<std::string>();
                messages.push_back(msg);
            }
        }
        break;
    }

    if (iteration >= max_tool_calls && req.on_error)
    {
        req.on_error("Max tool calls (" + std::to_string(max_tool_calls) + ") reached — model may be stuck in a loop");
    }

    if (!final_content.empty())
    {
        Logger::get().log_message("assistant", final_content);
        if (req.on_stream_chunk)
            req.on_stream_chunk("Assistant: " + final_content);
    }

    if (req.on_complete)
    {
        req.on_complete(messages);
    }
    Logger::get().log_message("debug", "process_request finished");
}
