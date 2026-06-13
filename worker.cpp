#include "worker.h"
#include "tool_router.h"
#include "logger.h"
#include "tools.h"
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
    std::string active_ws = get_workspace_directory().string();
    std::string sheet = "\n\n=== LOCAL TOOL SYSTEM ACCESS (MANDATORY INSTRUCTIONS) ===\n"
                        "You are running in a native C++ harness (\"PromptSlut\") with direct access to local desktop tools.\n"
                        "CURRENT ACTIVE WORKSPACE DIRECTORY: \"" + active_ws + "\"\n"
                        "To change this workspace directory, you can call the 'file' tool with op='set_workspace' (or set_cwd) and specify the new directory path in 'path'.\n"
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

Worker::Worker(QObject* parent)
    : QObject(parent)
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
    bool include_tools,
    int context_limit,
    const std::string& session_id,
    int max_tool_calls)
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
    req.context_limit = context_limit;
    req.session_id = session_id;
    req.max_tool_calls = max_tool_calls;
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

static std::vector<nlohmann::json> perform_emergency_trim(const std::vector<nlohmann::json>& messages, bool is_parse_error = false) {
    if (messages.empty()) {
        return messages;
    }

    std::vector<nlohmann::json> trimmed;

    // 1. Copy and optionally discard messages
    if (messages.size() <= 4) {
        trimmed = messages;
    } else {
        // Keep index 0 (the System prompt / tools schema)
        trimmed.push_back(messages[0]);

        size_t keep_from_end = 3; // Keep the last 3 messages as-is
        size_t end_start_index = messages.size() - keep_from_end;

        // Discard 50% of the messages between index 1 and end_start_index
        size_t middle_count = end_start_index - 1;
        size_t to_discard_count = middle_count / 2;
        if (to_discard_count == 0 && middle_count > 0) {
            to_discard_count = 1; // Force discard at least 1 if middle is non-empty!
        }
        size_t start_keeping_again = 1 + to_discard_count;

        // Add remaining older messages
        for (size_t i = start_keeping_again; i < end_start_index; ++i) {
            trimmed.push_back(messages[i]);
        }

        // Add inline warning message
        nlohmann::json warning_msg;
        warning_msg["role"] = "system";
        if (is_parse_error) {
            warning_msg["content"] = "[SYSTEM ALERT: Your last tool call was cut off and failed to parse because it exceeded generation length limits. If you were writing/editing a file, please do so in smaller chunks using op='append', or shorten your output.]";
        } else {
            warning_msg["content"] = "[SYSTEM NOTE: Older messages in this session have been automatically trimmed from active context to fit within the server's context limit. Older messages are still archived on disk.]";
        }
        trimmed.push_back(warning_msg);

        // Add the active Hot Zone messages
        for (size_t i = end_start_index; i < messages.size(); ++i) {
            trimmed.push_back(messages[i]);
        }
    }

    // 2. Scan and truncate ANY message content that exceeds 8,000 characters!
    // This is the ultimate shield against giant tool outputs (like file listing or massive file reads)
    for (auto& msg : trimmed) {
        if (msg.contains("content")) {
            if (msg["content"].is_string()) {
                std::string content = msg["content"].get<std::string>();
                if (content.length() > 8000) {
                    std::string truncated = content.substr(0, 8000) + "\n\n[... CONTENT TRUNCATED FOR CONTEXT LIMITS ...]";
                    msg["content"] = truncated;
                    Logger::get().log_message("warning", "Truncated a message from " + std::to_string(content.length()) + " to 8000 characters.");
                }
            } else if (msg["content"].is_array()) {
                // If it's a multimodal array (text/image/audio), check text blocks
                for (auto& item : msg["content"]) {
                    if (item.contains("type") && item["type"] == "text" && item.contains("text")) {
                        std::string txt = item["text"].get<std::string>();
                        if (txt.length() > 8000) {
                            item["text"] = txt.substr(0, 8000) + "\n\n[... CONTENT TRUNCATED FOR CONTEXT LIMITS ...]";
                            Logger::get().log_message("warning", "Truncated a multimodal text block from " + std::to_string(txt.length()) + " to 8000 characters.");
                        }
                    }
                }
            }
        }
    }

    return trimmed;
}

#include <regex>

static std::string convert_xml_parameters_to_json(const std::string& xml_str) {
    std::string trimmed = xml_str;
    trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));

    // Strip markdown code block ticks if present (e.g. ```json ... ``` or ``` ...)
    if (trimmed.rfind("```", 0) == 0) {
        size_t first_newline = trimmed.find('\n');
        if (first_newline != std::string::npos) {
            trimmed = trimmed.substr(first_newline + 1);
        } else {
            trimmed.erase(0, 3);
        }
        trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
        // Remove trailing backticks
        size_t last_ticks = trimmed.rfind("```");
        if (last_ticks != std::string::npos) {
            trimmed = trimmed.substr(0, last_ticks);
        }
        trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);
    }

    if (!trimmed.empty() && trimmed.front() == '{') {
        return trimmed;
    }

    std::regex param_regex("<parameter=([^>]+)>([\\s\\S]*?)</parameter>");
    auto words_begin = std::sregex_iterator(xml_str.begin(), xml_str.end(), param_regex);
    auto words_end = std::sregex_iterator();

    nlohmann::json obj;
    bool found_any = false;

    for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
        std::smatch match = *i;
        std::string name = match[1].str();
        std::string value = match[2].str();

        name.erase(0, name.find_first_not_of(" \t\r\n"));
        name.erase(name.find_last_not_of(" \t\r\n") + 1);
        value.erase(0, value.find_first_not_of(" \t\r\n"));
        value.erase(value.find_last_not_of(" \t\r\n") + 1);

        obj[name] = value;
        found_any = true;
    }

    if (found_any) {
        return obj.dump();
    }

    return "{}";
}

void Worker::emit_task_update(const std::string& action, const std::string& data) {
    emit request_task_update(
        QString::fromStdString(current_session_id_),
        QString::fromStdString(action),
        QString::fromStdString(data)
    );
}

void Worker::process_request(const Request& req)
{
    Logger::get().log_message("debug", "process_request start");
    current_session_id_ = req.session_id;
    auto messages = req.messages;

    bool is_coder_mode = false;
    if (!messages.empty() && messages[0]["role"] == "system") {
        std::string content = messages[0]["content"].get<std::string>();
        if (content.find("Coder Mode") != std::string::npos) {
            is_coder_mode = true;
            Logger::get().log_message("debug", "Coder Mode detected: Disabling native tool schemas to prevent llama-server auto-parser crash.");
        }
    }

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
    int max_tool_calls = req.max_tool_calls;
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

        // Proactive context limit estimation and pre-emptive trim
        size_t total_chars = 0;
        for (const auto& msg : messages) {
            if (msg.contains("content")) {
                if (msg["content"].is_string()) {
                    total_chars += msg["content"].get<std::string>().length();
                } else if (msg["content"].is_array()) {
                    total_chars += msg["content"].dump().length();
                }
            }
        }
        // Conservative token approximation (1 token = 3.5 characters)
        double estimated_tokens = static_cast<double>(total_chars) / 3.5;
        if (estimated_tokens > req.context_limit * 0.8) {
            Logger::get().log_message("warning", "Estimated tokens (" + std::to_string(static_cast<int>(estimated_tokens)) + 
                ") exceed 80% of limit (" + std::to_string(req.context_limit) + "). Performing pre-emptive trim...");
            messages = perform_emergency_trim(messages);
        }

        HttpForwarder http(req.host, req.port, req.api_key);

        nlohmann::json resp;
        auto start_time = std::chrono::high_resolution_clock::now();
        bool call_succeeded = false;
        int max_retries = 3;
        int retry_count = 0;

        while (retry_count < max_retries && !call_succeeded) {
            try
            {
                Logger::get().log_message("debug", "calling chat_completion_stream...");
                resp = http.chat_completion_stream(messages, is_coder_mode ? std::vector<nlohmann::json>() : req.tools, req.model, req.on_stream_chunk, req.on_reasoning);
                Logger::get().log_message("debug", "chat_completion_stream returned");
                call_succeeded = true;
            }
            catch (const std::exception& e)
            {
                std::string err_str = e.what();
                Logger::get().log_message("error", std::string("chat_completion exception: ") + err_str);
                
                bool is_parse_error = (err_str.find("Failed to parse tool call arguments") != std::string::npos ||
                                       err_str.find("parse error") != std::string::npos ||
                                       err_str.find("HTTP 500") != std::string::npos);

                if (is_parse_error ||
                    err_str.find("exceeds the available context size") != std::string::npos || 
                    err_str.find("exceed_context_size_error") != std::string::npos ||
                    err_str.find("context_length_exceeded") != std::string::npos ||
                    err_str.find("exceeds context") != std::string::npos ||
                    err_str.find("exceeds maximum") != std::string::npos ||
                    err_str.find("too large") != std::string::npos) 
                {
                    Logger::get().log_message("warning", "Context size or parse error detected. Performing emergency trim and retrying...");
                    if (messages.size() > 4) {
                        messages = perform_emergency_trim(messages, is_parse_error);
                        retry_count++;
                        continue; // Loop back and retry!
                    }
                }
                
                if (req.on_error)
                    req.on_error(std::string("Network error: ") + e.what());
                return;
            }
        }

        if (!call_succeeded) {
            if (req.on_error)
                req.on_error("Error: Failed to perform chat completion after emergency trim retries.");
            return;
        }
        auto end_time = std::chrono::high_resolution_clock::now();
        double elapsed_seconds = std::chrono::duration<double>(end_time - start_time).count();

        try {
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

                double gen_time = elapsed_seconds;
                if (resp.contains("timings") && !resp["timings"].is_null()) {
                    auto& timings = resp["timings"];
                    if (timings.contains("predicted_ms") && !timings["predicted_ms"].is_null()) {
                        auto& p_ms = timings["predicted_ms"];
                        double ms_val = 0.0;
                        if (p_ms.is_number_integer()) {
                            ms_val = static_cast<double>(p_ms.get<int64_t>());
                        } else if (p_ms.is_number_float()) {
                            ms_val = p_ms.get<double>();
                        }
                        gen_time = ms_val / 1000.0;
                    }
                }
                req.on_stats(gen_time, prompt_t, completion_t, total_t);
            }
        } catch (const std::exception& e) {
            Logger::get().log_message("error", std::string("Error processing stats: ") + e.what());
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
                    // Supports both formats:
                    // 1. <tool_call name="file">args</tool_call>
                    // 2. <tool_call><function=file>args</function></tool_call>
                    std::regex tag_regex("<tool_call(?:\\s+name=[\\\"']([^\\\"']+)[\\\"']\\s*)?>([\\s\\S]*?)</tool_call>");
                    auto words_begin = std::sregex_iterator(content.begin(), content.end(), tag_regex);
                    auto words_end = std::sregex_iterator();

                    std::string cleaned_content = content;
                    size_t offset_adjustment = 0;

                    for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
                        std::smatch match = *i;
                        std::string tool_name;
                        std::string args_str;

                        if (match[1].matched && !match[1].str().empty()) {
                            tool_name = match[1].str();
                            args_str = match[2].str();
                        } else {
                            std::string inner = match[2].str();
                            std::regex func_regex("<function=([^>]+)>([\\s\\S]*?)</function>");
                            std::smatch func_match;
                            if (std::regex_search(inner, func_match, func_regex)) {
                                tool_name = func_match[1].str();
                                args_str = func_match[2].str();
                            } else {
                                continue;
                            }
                        }

                        // Trim tool_name
                        tool_name.erase(0, tool_name.find_first_not_of(" \t\r\n"));
                        tool_name.erase(tool_name.find_last_not_of(" \t\r\n") + 1);

                        // Trim args_str
                        args_str.erase(0, args_str.find_first_not_of(" \t\r\n"));
                        args_str.erase(args_str.find_last_not_of(" \t\r\n") + 1);

                        nlohmann::json tc;
                        tc["id"] = generate_uuid();
                        tc["type"] = "function";
                        tc["function"]["name"] = tool_name;
                        
                        try {
                            std::string converted = convert_xml_parameters_to_json(args_str);
                            std::string repaired = ToolRouter::repair_json_backslashes(converted);
                            auto parsed = nlohmann::json::parse(repaired); // verify it is valid JSON
                            tc["function"]["arguments"] = repaired; // Store as string!
                        } catch (...) {
                            nlohmann::json fallback_obj;
                            fallback_obj["raw_input"] = args_str;
                            tc["function"]["arguments"] = fallback_obj.dump();
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
            if (msg.contains("content") && !msg["content"].is_null())
            {
                final_content = msg["content"].get<std::string>();
                messages.push_back(msg);
            }
            else
            {
                messages.push_back(msg);
            }

            // Fallback: If final_content is empty, check reasoning fields so the user sees the output!
            if (final_content.empty())
            {
                if (msg.contains("reasoning_content") && !msg["reasoning_content"].is_null())
                {
                    final_content = msg["reasoning_content"].get<std::string>();
                }
                else if (msg.contains("reasoning") && !msg["reasoning"].is_null())
                {
                    final_content = msg["reasoning"].get<std::string>();
                }
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
