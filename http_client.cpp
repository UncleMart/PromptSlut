#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
#warning "Warning: CPPHTTPLIB_OPENSSL_SUPPORT macro is missing! HTTPS connections will fail."
#endif

#include "http_client.h"
#include "logger.h"
#include "tool_router.h"
#include <stdexcept>
#include <sstream>
#include <string>
#include <functional>
#include <iostream>
#include <memory>

static int estimate_tokens(const std::string& text) {
    int tokens = 0;
    size_t i = 0;
    while (i < text.length()) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 128) {
            size_t start = i;
            while (i < text.length() && static_cast<unsigned char>(text[i]) < 128) {
                i++;
            }
            std::string ascii_sub = text.substr(start, i - start);
            tokens += static_cast<int>(ascii_sub.length() / 3.8);
        } else {
            tokens++;
            if ((c & 0xE0) == 0xC0) i += 2;
            else if ((c & 0xF0) == 0xE0) i += 3;
            else if ((c & 0xF8) == 0xF0) i += 4;
            else i++;
        }
    }
    return tokens > 0 ? tokens : 1;
}

#ifdef DEBUG_OUTPUT
#define DBG(msg) std::cout << msg << std::endl
#else
#define DBG(msg) do {} while(0)
#endif

HttpForwarder::HttpForwarder(std::string_view host, int port, std::string_view api_key)
    : host_(host), port_(port), api_key_(api_key)
{
}

nlohmann::json HttpForwarder::make_request(
    const std::vector<nlohmann::json>& messages,
    const std::vector<nlohmann::json>& tools,
    const std::string& model,
    const nlohmann::json& response_format)
{
    std::cout << "[DEBUG CLIENT] inside make_request" << std::endl;
    nlohmann::json req;
    req["model"] = model;
    req["messages"] = messages;
    req["temperature"] = 0.7;
    req["max_tokens"] = 8192;

    if (!tools.empty())
        req["tools"] = tools;

    if (!response_format.empty())
        req["response_format"] = response_format;

    std::cout << "[DEBUG CLIENT] make_request finished" << std::endl;
    return req;
}

std::string HttpForwarder::post(const std::string& url, const std::string& body)
{
    Logger::get().log_message("debug", "HttpForwarder::post start, url: " + url);
    
    std::string scheme = "http";
    std::string clean_host = host_;
    int target_port = port_;

    if (host_.rfind("https://", 0) == 0) {
        scheme = "https";
        clean_host = host_.substr(8);
    } else if (host_.rfind("http://", 0) == 0) {
        scheme = "http";
        clean_host = host_.substr(7);
    }

    if (!clean_host.empty() && clean_host.back() == '/') {
        clean_host.pop_back();
    }

    size_t colon_pos = clean_host.find(':');
    if (colon_pos != std::string::npos) {
        try {
            target_port = std::stoi(clean_host.substr(colon_pos + 1));
        } catch (...) {}
        clean_host = clean_host.substr(0, colon_pos);
    }

    // Correct Base Class: Using httplib::ClientImpl as the polymorphic interface pointer
    std::unique_ptr<httplib::ClientImpl> client;
    if (scheme == "https") {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
        if (target_port > 0) {
            client = std::make_unique<httplib::SSLClient>(clean_host, target_port);
        } else {
            client = std::make_unique<httplib::SSLClient>(clean_host, 443);
        }
#else
        throw std::runtime_error("HTTPS connections are not supported because CPPHTTPLIB_OPENSSL_SUPPORT is missing!");
#endif
    } else {
        if (target_port > 0) {
            client = std::make_unique<httplib::ClientImpl>(clean_host, target_port);
        } else {
            client = std::make_unique<httplib::ClientImpl>(clean_host, 80);
        }
    }

    client->enable_server_certificate_verification(false);
    client->set_keep_alive(false);
    
    if (!client->is_valid())
        throw std::runtime_error("HttpForwarder: invalid client for " + host_);
    
    client->set_connection_timeout(5000);
    client->set_read_timeout(60000);
    
    httplib::Headers headers = {
        {"Content-Type", "application/json"},
        {"ngrok-skip-browser-warning", "69420"}
    };
    if (!api_key_.empty() && api_key_ != "key" && api_key_ != "default") {
        headers.emplace("Authorization", "Bearer " + api_key_);
    }
    
    auto res = client->Post(url.c_str(), headers, body, "application/json");
    
    if (!res)
        throw std::runtime_error("HttpForwarder: network error on POST " + url);
    
    if (res->status < 200 || res->status >= 300)
    {
        std::ostringstream oss;
        oss << "HttpForwarder: HTTP " << res->status << " on POST " + url;
        if (!res->body.empty())
            oss << "\nBody: " << res->body;
        throw std::runtime_error(oss.str());
    }
    
    return res->body;
}

nlohmann::json HttpForwarder::chat_completion(
    const std::vector<nlohmann::json>& messages,
    const std::vector<nlohmann::json>& tools,
    const std::string& model,
    const nlohmann::json& response_format)
{
    std::cout << "[DEBUG CLIENT] inside chat_completion" << std::endl;
    nlohmann::json payload = make_request(messages, tools, model, response_format);

    std::cout << "[DEBUG CLIENT] dumping payload..." << std::endl;
    std::string body = payload.dump();
    std::cout << "[DEBUG CLIENT] calling post()..." << std::endl;
    std::string response = post("/v1/chat/completions", body);

    std::cout << "[DEBUG CLIENT] parsing response..." << std::endl;

    bool is_json = false;
    size_t first_char = response.find_first_not_of(" \t\r\n");
    if (first_char != std::string::npos && response[first_char] == '{') {
        is_json = true;
    }

    if (!is_json && response.find("data:") != std::string::npos) {
        std::cout << "[DEBUG CLIENT] detected text/event-stream response, reconstructing JSON..." << std::endl;
        std::string accumulated_content;
        std::string reasoning_content;
        std::istringstream stream(response);
        std::string line;
        while (std::getline(stream, line)) {
            auto last = line.find_last_not_of(" \r\n");
            if (last != std::string::npos) {
                line.erase(last + 1);
            }
            if (line == "data: [DONE]" || line.find("data:") != 0) {
                continue;
            }

            std::string data_part = line.substr(5);
            auto first = data_part.find_first_not_of(' ');
            if (first != std::string::npos) {
                data_part.erase(0, first);
            }

            try {
                nlohmann::json delta = nlohmann::json::parse(data_part);
                if (delta.contains("choices") && !delta["choices"].is_null() && !delta["choices"].empty()) {
                    auto& choices = delta["choices"];
                    if (choices[0].contains("delta") && !choices[0]["delta"].is_null()) {
                        auto& delta_obj = choices[0]["delta"];
                        if (delta_obj.contains("content") && !delta_obj["content"].is_null()) {
                            accumulated_content += delta_obj["content"].get<std::string>();
                        }
                        if (delta_obj.contains("reasoning_content") && !delta_obj["reasoning_content"].is_null()) {
                            reasoning_content += delta_obj["reasoning_content"].get<std::string>();
                        } else if (delta_obj.contains("reasoning") && !delta_obj["reasoning"].is_null()) {
                            reasoning_content += delta_obj["reasoning"].get<std::string>();
                        }
                    }
                }
            } catch (...) {}
        }

        nlohmann::json reconstructed;
        nlohmann::json message_obj;
        message_obj["role"] = "assistant";
        message_obj["content"] = accumulated_content;
        if (!reasoning_content.empty()) {
            message_obj["reasoning_content"] = reasoning_content;
        }

        nlohmann::json choice_obj;
        choice_obj["index"] = 0;
        choice_obj["message"] = message_obj;
        choice_obj["finish_reason"] = "stop";

        reconstructed["choices"] = nlohmann::json::array({choice_obj});
        reconstructed["object"] = "chat.completion";
        
        std::cout << "[DEBUG CLIENT] reconstructed json successfully" << std::endl;
        return reconstructed;
    }

    nlohmann::json resp = nlohmann::json::parse(response.begin(), response.end());

    std::cout << "[DEBUG CLIENT] checking choices..." << std::endl;
    if (!resp.contains("choices") || !resp["choices"].is_array())
        throw std::runtime_error("HttpForwarder: malformed response — no choices array");

    std::cout << "[DEBUG CLIENT] chat_completion finished successfully" << std::endl;
    return resp;
}

nlohmann::json HttpForwarder::chat_completion_stream(
    const std::vector<nlohmann::json>& messages,
    const std::vector<nlohmann::json>& tools,
    const std::string& model,
    std::function<void(const std::string& chunk)> on_chunk,
    std::function<void(const std::string& reasoning_chunk)> on_reasoning,
    const nlohmann::json& response_format)
{
    nlohmann::json payload = make_request(messages, tools, model, response_format);
    payload["stream"] = true;
    
    std::string body = payload.dump();
    
    std::string scheme = "http";
    std::string clean_host = host_;
    int target_port = port_;

    if (host_.rfind("https://", 0) == 0) {
        scheme = "https";
        clean_host = host_.substr(8);
    } else if (host_.rfind("http://", 0) == 0) {
        scheme = "http";
        clean_host = host_.substr(7);
    }

    if (!clean_host.empty() && clean_host.back() == '/') {
        clean_host.pop_back();
    }

    size_t colon_pos = clean_host.find(':');
    if (colon_pos != std::string::npos) {
        try {
            target_port = std::stoi(clean_host.substr(colon_pos + 1));
        } catch (...) {}
        clean_host = clean_host.substr(0, colon_pos);
    }

    // Correct Base Class: Using httplib::ClientImpl as the polymorphic interface pointer
    std::unique_ptr<httplib::ClientImpl> client;
    if (scheme == "https") {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
        if (target_port > 0) {
            client = std::make_unique<httplib::SSLClient>(clean_host, target_port);
        } else {
            client = std::make_unique<httplib::SSLClient>(clean_host, 443);
        }
#else
        throw std::runtime_error("HTTPS connections are not supported because CPPHTTPLIB_OPENSSL_SUPPORT is missing!");
#endif
    } else {
        if (target_port > 0) {
            client = std::make_unique<httplib::ClientImpl>(clean_host, target_port);
        } else {
            client = std::make_unique<httplib::ClientImpl>(clean_host, 80);
        }
    }

    client->enable_server_certificate_verification(false);
    client->set_keep_alive(false);
    
    if (!client->is_valid())
        throw std::runtime_error("HttpForwarder: invalid client for " + host_);
    
    client->set_connection_timeout(5000);
    client->set_read_timeout(60000);
    
    httplib::Headers headers = {
        {"Content-Type", "application/json"},
        {"Accept", "text/event-stream"},
        {"ngrok-skip-browser-warning", "69420"}
    };
    if (!api_key_.empty() && api_key_ != "key" && api_key_ != "default") {
        headers.emplace("Authorization", "Bearer " + api_key_);
    }
    
    std::string buffer;
    std::string accumulated_content;
    std::string accumulated_reasoning;
    nlohmann::json accumulated_tool_calls = nlohmann::json::array();
    nlohmann::json accumulated_usage = nlohmann::json::object();
    nlohmann::json accumulated_timings = nlohmann::json::object();
    int accumulated_content_chunks = 0;
    
    auto process_line = [&](const std::string& line) {
        if (line == "data: [DONE]" || line.find("data:") != 0) {
            return;
        }
        std::string data_part = line.substr(5);
        auto first = data_part.find_first_not_of(' ');
        if (first != std::string::npos) {
            data_part.erase(0, first);
        }
        try {
            nlohmann::json delta = nlohmann::json::parse(data_part);
            if (delta.contains("usage") && !delta["usage"].is_null()) {
                accumulated_usage = delta["usage"];
            }
            if (delta.contains("timings") && !delta["timings"].is_null()) {
                accumulated_timings = delta["timings"];
            }
            if (delta.contains("choices") && !delta["choices"].is_null() && !delta["choices"].empty()) {
                auto& choices = delta["choices"];
                if (choices[0].contains("delta") && !choices[0]["delta"].is_null()) {
                    auto& delta_obj = choices[0]["delta"];
                    if (delta_obj.contains("content") && !delta_obj["content"].is_null()) {
                        std::string content = delta_obj["content"].get<std::string>();
                        accumulated_content += content;
                        accumulated_content_chunks++;
                        if (on_chunk) on_chunk(content);
                    }
                    if (delta_obj.contains("reasoning_content") && !delta_obj["reasoning_content"].is_null()) {
                        std::string reasoning = delta_obj["reasoning_content"].get<std::string>();
                        accumulated_reasoning += reasoning;
                        accumulated_content_chunks++;
                        if (on_reasoning) on_reasoning(reasoning);
                    } else if (delta_obj.contains("reasoning") && !delta_obj["reasoning"].is_null()) {
                        std::string reasoning = delta_obj["reasoning"].get<std::string>();
                        accumulated_reasoning += reasoning;
                        accumulated_content_chunks++;
                        if (on_reasoning) on_reasoning(reasoning);
                    }
                    if (delta_obj.contains("tool_calls") && !delta_obj["tool_calls"].is_null()) {
                        for (auto& tc : delta_obj["tool_calls"]) {
                            int idx = tc.value("index", 0);
                            while (accumulated_tool_calls.size() <= static_cast<size_t>(idx)) {
                                nlohmann::json empty_tc;
                                empty_tc["id"] = "";
                                empty_tc["type"] = "function";
                                empty_tc["function"]["name"] = "";
                                empty_tc["function"]["arguments"] = "";
                                accumulated_tool_calls.push_back(empty_tc);
                            }
                            auto& target_tc = accumulated_tool_calls[idx];
                            if (tc.contains("id") && !tc["id"].is_null()) {
                                target_tc["id"] = target_tc["id"].get<std::string>() + tc["id"].get<std::string>();
                            }
                            if (tc.contains("function") && !tc["function"].is_null()) {
                                auto& func = tc["function"];
                                if (func.contains("name") && !func["name"].is_null()) {
                                    target_tc["function"]["name"] = target_tc["function"]["name"].get<std::string>() + func["name"].get<std::string>();
                                }
                                if (func.contains("arguments") && !func["arguments"].is_null()) {
                                    target_tc["function"]["arguments"] = target_tc["function"]["arguments"].get<std::string>() + func["arguments"].get<std::string>();
                                }
                            }
                        }
                    }
                }
            }
        } catch (...) {}
    };

    auto res = client->Post("/v1/chat/completions", headers, body, "application/json",
        [&](const char* data, size_t len) -> bool {
            buffer.append(data, len);
            size_t pos;
            while ((pos = buffer.find('\n')) != std::string::npos) {
                std::string line = buffer.substr(0, pos);
                buffer.erase(0, pos + 1);
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                process_line(line);
            }
            return true;
        });
    
    if (!res)
        throw std::runtime_error("HttpForwarder: network error on POST /v1/chat/completions");
    
    if (res->status < 200 || res->status >= 300)
    {
        std::ostringstream oss;
        oss << "HttpForwarder: HTTP " << res->status << " on POST /v1/chat/completions";
        if (!res->body.empty())
            oss << "\nBody: " << res->body;
        throw std::runtime_error(oss.str());
    }

    if (!buffer.empty()) {
        if (buffer.back() == '\r') {
            buffer.pop_back();
        }
        process_line(buffer);
    }

    nlohmann::json reconstructed;
    nlohmann::json message_obj;
    message_obj["role"] = "assistant";
    message_obj["content"] = accumulated_content;
    if (!accumulated_reasoning.empty()) {
        message_obj["reasoning_content"] = accumulated_reasoning;
    }
    if (!accumulated_tool_calls.empty()) {
        for (auto& tc : accumulated_tool_calls) {
            std::string args_str = tc["function"]["arguments"].get<std::string>();
            try {
                std::string repaired = ToolRouter::repair_json_backslashes(args_str);
                tc["function"]["arguments"] = repaired;
            } catch (...) {
                nlohmann::json fallback_obj;
                fallback_obj["raw_input"] = args_str;
                tc["function"]["arguments"] = fallback_obj.dump();
            }
        }
        message_obj["tool_calls"] = accumulated_tool_calls;
    }

    nlohmann::json choice_obj;
    choice_obj["index"] = 0;
    choice_obj["message"] = message_obj;
    choice_obj["finish_reason"] = "stop";

    reconstructed["choices"] = nlohmann::json::array({choice_obj});
    reconstructed["object"] = "chat.completion";
    if (accumulated_usage.empty()) {
        int estimated_prompt_tokens = 0;
        for (const auto& msg : messages) {
            if (msg.contains("content") && msg["content"].is_string()) {
                estimated_prompt_tokens += estimate_tokens(msg["content"].get<std::string>());
            }
        }
        accumulated_usage["prompt_tokens"] = estimated_prompt_tokens;
        accumulated_usage["completion_tokens"] = accumulated_content_chunks;
        accumulated_usage["total_tokens"] = estimated_prompt_tokens + accumulated_content_chunks;
    }
    if (!accumulated_usage.empty()) {
        reconstructed["usage"] = accumulated_usage;
    }
    if (!accumulated_timings.empty()) {
        reconstructed["timings"] = accumulated_timings;
    }
    
    return reconstructed;
}