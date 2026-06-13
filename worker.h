#ifndef WORKER_H
#define WORKER_H

#include <QObject>
#include <QString>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <thread>
#include <atomic>
#include <nlohmann/json.hpp>
#include "http_client.h"
#include "Tool.h"

// ---------------------------------------------------------------------------
// Worker
//
// Single background thread that owns a request queue.  The main thread
// pushes work items via push_request().  The worker thread drains them
// and calls callbacks with results.
//
// Conversation orchestration (full loop):
//   1. Send messages to llama-server with tool definitions.
//   2. If response has tool_calls: dispatch them, append results to
//      messages, send again (repeat).
//   3. If response has content (no tool_calls): stream to UI.
// ---------------------------------------------------------------------------

struct Request
{
    std::vector<nlohmann::json> messages;
    std::vector<nlohmann::json> tools;
    std::string host;
    int port = 8080;
    std::string api_key;
    std::string model;
    int context_limit = 32000;
    int max_tool_calls = 20;
    std::string session_id;
    std::function<void(const std::string& err)> on_error;
    std::function<void(const std::string& chunk)> on_stream_chunk;
    std::function<void(const std::string& tool_name, const std::string& result)> on_tool_result;
    std::function<void(const std::string& reasoning)> on_reasoning;
    std::function<void(const std::string& tool_name, const std::string& call_id)> on_tool_started;
    std::function<void(const std::vector<nlohmann::json>& updated_messages)> on_complete;
    std::function<void(double elapsed, int prompt_t, int completion_t, int total_t)> on_stats;
};

class Worker : public QObject
{
    Q_OBJECT
public:
    explicit Worker(QObject* parent = nullptr);
    ~Worker() override;

    void set_tool_registry(const std::vector<nlohmann::json>& tool_defs);
    void register_tool(std::shared_ptr<Tool> tool);

    bool push_request(
        std::vector<nlohmann::json> messages,
        const std::string& host,
        int port,
        const std::string& api_key,
        const std::string& model,
        std::function<void(const std::string& chunk)> on_stream_chunk,
        std::function<void(const std::string& tool_name, const std::string& result)> on_tool_result,
        std::function<void(const std::string&)> on_error,
        std::function<void(const std::string& reasoning)> on_reasoning = nullptr,
        std::function<void(const std::string& tool_name, const std::string& call_id)> on_tool_started = nullptr,
        std::function<void(const std::vector<nlohmann::json>&)> on_complete = nullptr,
        std::function<void(double elapsed, int prompt_t, int completion_t, int total_t)> on_stats = nullptr,
        bool include_tools = true,
        int context_limit = 32000,
        const std::string& session_id = "",
        int max_tool_calls = 20);

    void set_stop();
    void stop() { stop_current_request_ = true; }

    std::string current_session_id() const { return current_session_id_; }
    void emit_task_update(const std::string& action, const std::string& data);

signals:
    void request_task_update(const QString& session_id, const QString& action, const QString& data);

private:
    void worker_loop();
    void process_request(const Request& req);

    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cond_;
    std::deque<Request> queue_;
    bool stop_ = false;
    std::atomic<bool> stop_current_request_{false};
    std::vector<nlohmann::json> tool_defs_;
    ToolRegistry tool_registry_;
    std::string current_session_id_;
};

#endif // WORKER_H
