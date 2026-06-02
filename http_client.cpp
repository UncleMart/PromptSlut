#include "http_client.h"
#include "logger.h"
#include <stdexcept>
#include <sstream>
#include <string>
#include <functional>
#include <iostream>

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
    const std::string& model)
{
    std::cout << "[DEBUG CLIENT] inside make_request" << std::endl;
    nlohmann::json req;
    req["model"] = model;
    req["messages"] = messages;
    req["temperature"] = 0.7;
    req["max_tokens"] = 8192;

    if (!tools.empty())
        req["tools"] = tools;

    std::cout << "[DEBUG CLIENT] make_request finished" << std::endl;
    return req;
}

std::string HttpForwarder::post(const std::string& url, const std::string& body)
{
    Logger::get().log_message("debug", "HttpForwarder::post start, url: " + url);
    httplib::Client client("http://" + host_ + ":" + std::to_string(port_));
    client.set_keep_alive(false);
    
    if (!client.is_valid())
        throw std::runtime_error("HttpForwarder: invalid client");
    
    client.set_connection_timeout(5000);
    client.set_read_timeout(60000);
    
    httplib::Headers headers = {
        {"Content-Type", "application/json"},
        {"Authorization", "Bearer " + api_key_}
    };
    
    auto res = client.Post(url.c_str(), headers, body, "application/json");
    
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
    const std::string& model)
{
    std::cout << "[DEBUG CLIENT] inside chat_completion" << std::endl;
    nlohmann::json payload = make_request(messages, tools, model);

    std::cout << "[DEBUG CLIENT] dumping payload..." << std::endl;
    std::string body = payload.dump();
    std::cout << "[DEBUG CLIENT] calling post()..." << std::endl;
    std::string response = post("/v1/chat/completions", body);

    std::cout << "[DEBUG CLIENT] parsing response..." << std::endl;

    // Check if the response is an event stream (which happens on some mobile deployments like Pixel 10!)
    if (response.find("data:") != std::string::npos) {
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
                        }
                    }
                }
            } catch (...) {
                // Ignore parse errors on individual stream lines
            }
        }

        // Reconstruct standard non-streaming response structure
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

void HttpForwarder::chat_completion_stream(
    const std::vector<nlohmann::json>& messages,
    const std::vector<nlohmann::json>& tools,
    const std::string& model,
    std::function<void(const std::string& chunk)> on_chunk)
{
    nlohmann::json payload = make_request(messages, tools, model);
    payload["stream"] = true;
    payload["stream_options"] = nlohmann::json::parse("{\"enable\":true}");
    
    std::string body = payload.dump();
    
    httplib::Client client("http://" + host_ + ":" + std::to_string(port_));
    client.set_keep_alive(false);
    
    if (!client.is_valid())
        throw std::runtime_error("HttpForwarder: invalid client for " + host_ + ":" + std::to_string(port_));
    
    client.set_connection_timeout(5000);
    client.set_read_timeout(60000);
    
    httplib::Headers headers = {
        {"Content-Type", "application/json"},
        {"Accept", "text/event-stream"},
        {"Authorization", "Bearer " + api_key_}
    };
    
    std::string response_body;
    auto res = client.Post("/v1/chat/completions", headers, body, "application/json",
        [&](const char* data, size_t len) -> bool {
            response_body.append(data, len);
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
    
    std::istringstream stream(response_body);
    std::string line;
    while (std::getline(stream, line))
    {
        auto last = line.find_last_not_of(" \r\n");
        if (last != std::string::npos)
            line.erase(last + 1);
        
        if (line == "data: [DONE]")
            break;
        
        if (line.find("data:") == 0)
        {
            std::string data_part = line.substr(5);
            auto first = data_part.find_first_not_of(' ');
            if (first != std::string::npos)
                data_part.erase(0, first);
            
            try
            {
                nlohmann::json delta = nlohmann::json::parse(data_part);
                if (delta.contains("choices") && !delta["choices"].is_null() && !delta["choices"].empty())
                {
                    auto& choices = delta["choices"];
                    if (choices[0].contains("delta") && !choices[0]["delta"].is_null())
                    {
                        auto& delta_obj = choices[0]["delta"];
                        if (delta_obj.contains("content") && !delta_obj["content"].is_null())
                        {
                            std::string content = delta_obj["content"].get<std::string>();
                            if (on_chunk) on_chunk(content);
                        }
                        if (delta_obj.contains("tool_calls") && !delta_obj["tool_calls"].is_null())
                        {
                            for (auto& tc : delta_obj["tool_calls"])
                            {
                                if (tc.contains("function"))
                                {
                                    std::string fname = tc["function"]["name"].get<std::string>();
                                    std::string farg = tc["function"]["arguments"].is_string() ? tc["function"]["arguments"].get<std::string>() : tc["function"]["arguments"].dump();
                                    if (on_chunk) on_chunk("[TOOL_CALL:" + fname + "(" + farg.substr(0, 200) + ")]");
                                }
                            }
                        }
                    }
                }
            }
            catch (...) {}
        }
    }
}
