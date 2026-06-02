#include "logger.h"
#include <filesystem>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

// ---------------------------------------------------------------------------
// Logger implementation
// ---------------------------------------------------------------------------

Logger& Logger::get()
{
    static Logger instance;
    return instance;
}

std::string Logger::log_path()
{
    namespace fs = std::filesystem;

    // Use current working directory for simplicity.
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);

    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &time_t);
#else
    localtime_r(&time_t, &tm);
#endif

    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(4) << (tm.tm_year + 1900) << "-";
    oss << std::setfill('0') << std::setw(2) << (tm.tm_mon + 1) << "-";
    oss << std::setfill('0') << std::setw(2) << tm.tm_mday;

    log_dir_ = std::string("logs") + "/" + oss.str();

    // Create directory if needed.
    fs::path dir = fs::path(log_dir_);
    if (!fs::exists(dir))
        fs::create_directories(dir);

    return log_dir_ + "/chat.log";
}

std::ofstream Logger::open_log()
{
    std::string path = log_path();
    return std::ofstream(path, std::ios::out | std::ios::app);
}

void Logger::write_line(std::ofstream& log, const std::string& line)
{
    log << line << "\n";
    log.flush();
}

void Logger::log_message(std::string role, std::string content)
{
    std::lock_guard lock(mutex_);

    std::ofstream log = open_log();
    if (!log.is_open())
        return;

    // Generate ISO 8601 timestamp.
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);

    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &time_t);
#else
    localtime_r(&time_t, &tm);
#endif

    std::ostringstream ts;
    ts << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");

    write_line(log, "[" + ts.str() + "] " + role + ": " + content);
}

void Logger::log_tool_call(std::string tool_name, std::string arguments)
{
    std::lock_guard lock(mutex_);

    std::ofstream log = open_log();
    if (!log.is_open())
        return;

    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);

    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &time_t);
#else
    localtime_r(&time_t, &tm);
#endif

    std::ostringstream ts;
    ts << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");

    write_line(log, "[" + ts.str() + "] TOOL_CALL: " + tool_name + " " + arguments);
}
