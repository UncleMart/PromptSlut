#ifndef LOGGER_H
#define LOGGER_H

#include <windows.h>
#include <string>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <mutex>
#include "convert.h"

// ---------------------------------------------------------------------------
// Logger
//
// Writes chat messages to /logs/YYYY-MM-DD/chat.log in UTF-8.
// Every write is atomic (open + write + close) to protect against
// concurrent writes from the worker thread and any future threads.
//
// Thread safety:
//   - All public methods are guarded by a std::mutex.
//   - The lock is held for the minimum time (open/write/close).
//   - No file descriptor pooling — simple and robust for now.
// ---------------------------------------------------------------------------

class Logger
{
public:
    // Get the singleton-like reference (constructed on first use).
    static Logger& get();

    // Log a chat message (user or assistant).
    void log_message(std::string role, std::string content);

    // Log a tool call event.
    void log_tool_call(std::string tool_name, std::string arguments);

private:
    Logger() = default;
    ~Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    // Open today's log file for appending.
    std::ofstream open_log();

    // Generate the log path for today.
    std::string log_path();

    // Write a formatted line to the log.
    void write_line(std::ofstream& log, const std::string& line);

    mutable std::mutex mutex_;
    std::string log_dir_;
};

#endif // LOGGER_H
