#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <string>
#include <string_view>
#include <nlohmann/json.hpp>
#include "httplib.h"

// ---------------------------------------------------------------------------
// HttpForwarder
//
// Thin wrapper around cpp-httplib's Client.  All calls go to the llama-server
// /v1/chat/completions endpoint.  Connection is per-request (no keep-alive
// pool needed at this stage).
//
// Thread safety: HttpForwarder is NOT shared.  Each worker invocation creates
// a local HttpForwarder or calls on a thread that owns one instance.
// ---------------------------------------------------------------------------

class HttpForwarder
{
public:
    HttpForwarder(std::string_view host, int port, std::string_view api_key);

    // Send a chat completion request and return the parsed response JSON.
    // Throws std::runtime_error on HTTP failure or JSON parse error.
    nlohmann::json chat_completion(
        const std::vector<nlohmann::json>& messages,
        const std::vector<nlohmann::json>& tools = {},
        const std::string& model = "qwen3:latest",
        const nlohmann::json& response_format = {});

    // Streaming version — calls on_chunk with each SSE fragment.
    // Returns when [DONE] is received or on error.
    void chat_completion_stream(
        const std::vector<nlohmann::json>& messages,
        const std::vector<nlohmann::json>& tools,
        const std::string& model,
        std::function<void(const std::string& chunk)> on_chunk,
        const nlohmann::json& response_format = {});

private:
    std::string host_;
    int port_;
    std::string api_key_;

    // Build the JSON payload for the /v1/chat/completions endpoint.
    nlohmann::json make_request(
        const std::vector<nlohmann::json>& messages,
        const std::vector<nlohmann::json>& tools,
        const std::string& model,
        const nlohmann::json& response_format = {});

    // Perform the HTTP POST and return the response body as a string.
    std::string post(const std::string& url, const std::string& body);
};

#endif // HTTP_CLIENT_H
