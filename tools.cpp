#include "tools.h"
#include "convert.h"
#include "keyfile.h"
#include "httplib.h"
#include "qt_ui.h"
#include <QApplication>
#include <QScreen>
#include <QPixmap>
#include <QGuiApplication>
#include <QBuffer>
#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <shlwapi.h>
#include <iostream>
#include <memory>
#include <regex>
#include <algorithm>
#include <thread>
#include <chrono>
#include <mutex>

static bool matches_spec_w(const wchar_t* filename, const wchar_t* pattern) {
    return PathMatchSpecW(filename, pattern) != FALSE;
}

static bool matches_spec_utf8(const std::string& filename, const std::string& pattern) {
    std::wstring wfilename = utf8_to_utf16(filename);
    std::wstring wpattern = utf8_to_utf16(pattern);
    return matches_spec_w(wfilename.c_str(), wpattern.c_str());
}

static std::string url_decode(const std::string& str) {
    std::string result;
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '%' && i + 2 < str.length()) {
            std::string hex = str.substr(i + 1, 2);
            try {
                char decoded = static_cast<char>(std::stoi(hex, nullptr, 16));
                result += decoded;
                i += 2;
            } catch (...) {
                result += '%';
            }
        } else if (str[i] == '+') {
            result += ' ';
        } else {
            result += str[i];
        }
    }
    return result;
}

static std::filesystem::path g_workspace_dir;
static std::mutex g_workspace_mutex;

void set_workspace_directory(const std::filesystem::path& path) {
    std::lock_guard<std::mutex> lock(g_workspace_mutex);
    g_workspace_dir = path;
}

std::filesystem::path get_workspace_directory() {
    std::lock_guard<std::mutex> lock(g_workspace_mutex);
    if (g_workspace_dir.empty()) {
        wchar_t exe_path[MAX_PATH];
        GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
        std::filesystem::path root_dir = std::filesystem::path(exe_path).parent_path();

        while (root_dir.has_parent_path() &&
               !std::filesystem::exists(root_dir / "CMakeLists.txt") &&
               !std::filesystem::exists(root_dir / "system_prompt.txt")) {
            root_dir = root_dir.parent_path();
        }

        if (std::filesystem::exists(root_dir / "CMakeLists.txt") || std::filesystem::exists(root_dir / "system_prompt.txt")) {
            g_workspace_dir = root_dir;
        } else {
            g_workspace_dir = std::filesystem::current_path();
        }
    }
    return g_workspace_dir;
}

static std::filesystem::path resolve_path(const std::string& input_path) {
    std::filesystem::path p(utf8_to_utf16(input_path));
    if (p.is_absolute()) {
        return p;
    }
    return get_workspace_directory() / p;
}

static std::string normalize_newlines(const std::string& str) {
    std::string res;
    res.reserve(str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '\r') {
            if (i + 1 < str.size() && str[i+1] == '\n') {
                res += '\n';
                i++;
            } else {
                res += '\n';
            }
        } else {
            res += str[i];
        }
    }
    return res;
}

static std::string truncate_output(const std::string& str, size_t max_len = 30000) {
    if (str.length() <= max_len) return str;
    return str.substr(0, max_len) + "\n\n...(truncated " + std::to_string(str.length() - max_len) + " characters for context efficiency)...";
}

static void prepare_shell_path() {
    wchar_t path_buf[32767];
    DWORD len = GetEnvironmentVariableW(L"PATH", path_buf, 32767);
    if (len > 0) {
        std::wstring path_str(path_buf, len);
        
        std::wstring msys_ucrt = L"C:\\msys64\\ucrt64\\bin";
        if (!std::filesystem::exists(msys_ucrt)) {
            msys_ucrt = L"C:\\msys64.bak\\ucrt64\\bin";
        }
        
        std::wstring msys_usr = L"C:\\msys64\\usr\\bin";
        if (!std::filesystem::exists(msys_usr)) {
            msys_usr = L"C:\\msys64.bak\\usr\\bin";
        }

        std::wstring new_path;
        if (std::filesystem::exists(msys_ucrt)) {
            new_path += msys_ucrt + L";";
        }
        if (std::filesystem::exists(msys_usr)) {
            new_path += msys_usr + L";";
        }
        
        if (!new_path.empty() && path_str.find(msys_ucrt) == std::wstring::npos) {
            new_path += path_str;
            SetEnvironmentVariableW(L"PATH", new_path.c_str());
        }
    }
}

std::vector<std::shared_ptr<Tool>> ToolRegistry::all() const {
    return tools_;
}

void ToolRegistry::register_tool(std::shared_ptr<Tool> tool) {
    tools_.push_back(tool);
}

Tool* ToolRegistry::by_name(const std::string& name) const {
    for (auto& tool : tools_) {
        if (tool->name() == name) return tool.get();
    }
    return nullptr;
}

nlohmann::json ToolRegistry::to_json() const {
    nlohmann::json defs = nlohmann::json::array();
    for (auto& tool : tools_) {
        nlohmann::json tool_def;
        tool_def["type"] = "function";
        nlohmann::json func;
        func["name"] = tool->name();
        func["description"] = tool->description();
        func["parameters"] = tool->schema();
        tool_def["function"] = func;
        defs.push_back(tool_def);
    }
    return defs;
}

std::string SearchTool::execute(const nlohmann::json& args) {
    if (!args.contains("query") || !args["query"].is_string()) {
        return "Error: missing or invalid 'query' parameter.";
    }

    std::string key = get_serper_key();
    if (key.empty()) {
        return "Error: No Serper API Key configured. Please open Settings (⚙️) and enter your Serper.dev API Key to enable live Web Search.";
    }

    std::string query = args["query"].get<std::string>();
    
    httplib::Client client("https://google.serper.dev");
    httplib::Headers headers;
    headers.emplace("X-API-KEY", key);
    headers.emplace("Content-Type", "application/json");

    nlohmann::json body;
    body["q"] = query;

    auto res = client.Post("/search?q=" + query, headers, body.dump(), "application/json");

    if (!res) return "Error: failed to connect to Serper.dev API";
    if (res->status != 200) return "Error: Serper.dev API returned HTTP " + std::to_string(res->status) + "\nBody: " + res->body;

    try {
        auto j = nlohmann::json::parse(res->body);
        if (!j.contains("organic") || !j["organic"].is_array()) {
            return "No search results found.";
        }

        std::string results;
        int count = 0;
        for (auto& item : j["organic"]) {
            if (item.contains("title") && item.contains("link")) {
                results += "Title: " + item["title"].get<std::string>() + "\nURL: " + item["link"].get<std::string>() + "\n\n";
                count++;
            }
            if (count >= 10) break;
        }
        return results.empty() ? "No search results found." : results;
    } catch (const std::exception& e) {
        return std::string("Error parsing Serper API results: ") + e.what();
    }
}

std::string FileTool::execute(const nlohmann::json& args) {
    if (!args.contains("op") || !args["op"].is_string()) {
        return "Error: missing or invalid 'op' parameter.";
    }
    if (!args.contains("path") || !args["path"].is_string()) {
        return "Error: missing or invalid 'path' parameter.";
    }

    std::string op = args["op"].get<std::string>();
    std::string path = resolve_path(args["path"].get<std::string>()).string();

    if (op == "read") {
        std::ifstream f(path);
        if (!f.is_open()) return "Error: cannot open file for reading: " + path;
        std::stringstream ss;
        ss << f.rdbuf();
        return truncate_output(ss.str());
    } else if (op == "write") {
        if (!args.contains("content") || !args["content"].is_string()) {
            return "Error: 'content' string parameter is required for write operation.";
        }
        std::filesystem::path p(utf8_to_utf16(path));
        std::error_code ec;
        if (p.has_parent_path() && !std::filesystem::exists(p.parent_path(), ec)) {
            std::filesystem::create_directories(p.parent_path(), ec);
        }
        std::ofstream f(path);
        if (!f.is_open()) return "Error: cannot open file for writing: " + path;
        f << args["content"].get<std::string>();
        return "Success: wrote to " + path;
    } else if (op == "append") {
        if (!args.contains("content") || !args["content"].is_string()) {
            return "Error: 'content' string parameter is required for append operation.";
        }
        std::filesystem::path p(utf8_to_utf16(path));
        std::error_code ec;
        if (p.has_parent_path() && !std::filesystem::exists(p.parent_path(), ec)) {
            std::filesystem::create_directories(p.parent_path(), ec);
        }
        std::ofstream f(path, std::ios::app);
        if (!f.is_open()) return "Error: cannot open file for appending: " + path;
        f << args["content"].get<std::string>();
        return "Success: appended to " + path;
    } else if (op == "list") {
        std::error_code ec;
        if (!std::filesystem::is_directory(path, ec)) {
            return "Error: Path is not a directory: " + path;
        }
        std::string result;
        for (const auto& entry : std::filesystem::directory_iterator(path, ec)) {
            result += entry.path().filename().string();
            if (entry.is_directory()) result += "/";
            result += "\n";
        }
        if (ec) return "Error listing directory: " + ec.message();
        return truncate_output(result.empty() ? "Directory is empty." : result);
    } else if (op == "copy") {
        if (!args.contains("destination") || !args["destination"].is_string()) {
            return "Error: 'destination' string parameter is required for copy operation.";
        }
        std::string dest = resolve_path(args["destination"].get<std::string>()).string();
        std::error_code ec;
        std::filesystem::path dest_path(utf8_to_utf16(dest));
        if (dest_path.has_parent_path() && !std::filesystem::exists(dest_path.parent_path(), ec)) {
            std::filesystem::create_directories(dest_path.parent_path(), ec);
        }
        std::filesystem::copy(path, dest, std::filesystem::copy_options::overwrite_existing | std::filesystem::copy_options::recursive, ec);
        if (ec) return "Error copying: " + ec.message();
        return "Success: copied " + path + " to " + dest;
    } else if (op == "move") {
        if (!args.contains("destination") || !args["destination"].is_string()) {
            return "Error: 'destination' string parameter is required for move operation.";
        }
        std::string dest = resolve_path(args["destination"].get<std::string>()).string();
        std::error_code ec;
        std::filesystem::path dest_path(utf8_to_utf16(dest));
        if (dest_path.has_parent_path() && !std::filesystem::exists(dest_path.parent_path(), ec)) {
            std::filesystem::create_directories(dest_path.parent_path(), ec);
        }
        std::filesystem::rename(path, dest, ec);
        if (ec) return "Error moving: " + ec.message();
        return "Success: moved " + path + " to " + dest;
    } else if (op == "rename") {
        if (!args.contains("destination") || !args["destination"].is_string()) {
            return "Error: 'destination' string parameter is required for rename operation.";
        }
        std::string dest = resolve_path(args["destination"].get<std::string>()).string();
        std::error_code ec;
        std::filesystem::rename(path, dest, ec);
        if (ec) return "Error renaming: " + ec.message();
        return "Success: renamed " + path + " to " + dest;
    } else if (op == "delete" || op == "remove") {
        std::error_code ec;
        bool removed = std::filesystem::remove_all(path, ec) > 0;
        if (ec) return "Error deleting: " + ec.message();
        return removed ? "Success: deleted " + path : "File/directory did not exist: " + path;
    } else if (op == "set_workspace" || op == "set_cwd") {
        std::error_code ec;
        std::string raw_path = args["path"].get<std::string>();
        std::filesystem::path absolute_ws = std::filesystem::absolute(resolve_path(raw_path), ec);
        if (ec) return "Error resolving workspace path: " + ec.message();
        set_workspace_directory(absolute_ws);
        return "Success: Set working workspace directory to " + absolute_ws.string();
    }
    return "Error: unknown file op: " + op;
}

std::string ShellTool::execute(const nlohmann::json& args) {
    if (!args.contains("cmd") || !args["cmd"].is_string()) {
        return "Error: missing or invalid 'cmd' parameter.";
    }

    prepare_shell_path();

    std::string cmd = args["cmd"].get<std::string>();
    std::wstring wcmd = utf8_to_utf16("cmd /c " + cmd);

    HANDLE hChildStd_OUT_Rd = NULL;
    HANDLE hChildStd_OUT_Wr = NULL;

    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&hChildStd_OUT_Rd, &hChildStd_OUT_Wr, &saAttr, 0))
        return "Error: failed to create pipe";

    if (!SetHandleInformation(hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(hChildStd_OUT_Rd);
        CloseHandle(hChildStd_OUT_Wr);
        return "Error: failed to set handle info";
    }

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hChildStd_OUT_Wr;
    si.hStdError = hChildStd_OUT_Wr;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};

    if (!CreateProcessW(nullptr, &wcmd[0], nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(hChildStd_OUT_Rd);
        CloseHandle(hChildStd_OUT_Wr);
        return "Error: failed to execute command";
    }

    CloseHandle(hChildStd_OUT_Wr);

    std::string output;
    char buf[4096];
    DWORD dwRead;
    while (ReadFile(hChildStd_OUT_Rd, buf, sizeof(buf) - 1, &dwRead, NULL) && dwRead > 0) {
        buf[dwRead] = '\0';
        output += buf;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code;
    GetExitCodeProcess(pi.hProcess, &exit_code);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hChildStd_OUT_Rd);

    std::string result = output;
    if (exit_code != 0) {
        if (!result.empty() && result.back() != '\n') result += "\n";
        result += "(Command failed with exit code: " + std::to_string(exit_code) + ")";
    } else if (result.empty()) {
        result = "Command completed successfully (exit code: 0).";
    }
    return truncate_output(result);
}

std::string DiskSearchTool::execute(const nlohmann::json& args) {
    std::string path = resolve_path(args.value("path", ".")).string();
    std::string pattern = args.value("pattern", "*");
    std::string results;
    std::error_code ec;

    if (!std::filesystem::exists(path, ec)) {
        return "Error: Path does not exist: " + path;
    }

    try {
        auto it = std::filesystem::recursive_directory_iterator(path, std::filesystem::directory_options::skip_permission_denied, ec);
        if (ec) return "Error: Failed to open directory: " + ec.message();
        
        while (it != std::filesystem::recursive_directory_iterator()) {
            try {
                const auto& entry = *it;
                std::error_code entry_ec;
                if (entry.is_regular_file(entry_ec) && matches_spec_utf8(entry.path().filename().string(), pattern)) {
                    results += entry.path().string() + "\n";
                }
            } catch (...) {
                // Ignore errors reading a single entry
            }
            
            try {
                ++it;
            } catch (...) {
                it.pop();
            }
        }
    } catch (const std::exception& e) {
        return std::string("Error during search: ") + e.what();
    }

    return truncate_output(results.empty() ? "No files found." : results);
}

std::string FetchTool::execute(const nlohmann::json& args) {
    if (!args.contains("url") || !args["url"].is_string()) {
        return "Error: missing or invalid 'url' parameter.";
    }

    std::string url = args["url"].get<std::string>();
    if (url.find("://") == std::string::npos) {
        url = "https://" + url;
    }

    // Use Jina Reader to render JavaScript and convert to Markdown.
    // This bypasses the "JavaScript required" issue and cleans up the HTML.
    httplib::Client jina_client("https://r.jina.ai");
    httplib::Headers headers;
    headers.emplace("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");

    auto res_jina = jina_client.Get(("/" + url).c_str(), headers);

    if (!res_jina) return "Error: failed to connect to Jina Reader proxy";
    if (res_jina->status != 200) return "Error: Jina Reader returned HTTP " + std::to_string(res_jina->status);

    std::string content = res_jina->body;
    return content.length() > 10000 ? content.substr(0, 10000) + "\n...(truncated)" : content;
}

std::string ClockTool::execute(const nlohmann::json& args) {
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm now_tm;
    localtime_s(&now_tm, &now_c);
    char buf[100];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &now_tm);
    return std::string(buf);
}

std::string ClipboardTool::execute(const nlohmann::json& args) {
    if (!args.contains("op") || !args["op"].is_string()) {
        return "Error: missing or invalid 'op' parameter.";
    }
    std::string op = args["op"].get<std::string>();

    if (op == "read") {
        if (!OpenClipboard(nullptr)) return "Error: could not open clipboard";
        HANDLE hData = GetClipboardData(CF_UNICODETEXT);
        if (hData == nullptr) {
            CloseClipboard();
            return "Clipboard is empty or does not contain text.";
        }
        wchar_t* pText = static_cast<wchar_t*>(GlobalLock(hData));
        std::wstring wstr = pText ? pText : L"";
        GlobalUnlock(hData);
        CloseClipboard();
        return utf16_to_utf8(wstr);
    } else if (op == "write") {
        if (!args.contains("content") || !args["content"].is_string()) {
            return "Error: 'content' string parameter is required for write operation.";
        }
        std::string content = args["content"].get<std::string>();
        std::wstring wcontent = utf8_to_utf16(content);
        
        if (!OpenClipboard(nullptr)) return "Error: could not open clipboard";
        EmptyClipboard();
        
        size_t size = (wcontent.length() + 1) * sizeof(wchar_t);
        HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, size);
        if (!hGlobal) {
            CloseClipboard();
            return "Error: GlobalAlloc failed";
        }
        
        memcpy(GlobalLock(hGlobal), wcontent.c_str(), size);
        GlobalUnlock(hGlobal);
        
        if (SetClipboardData(CF_UNICODETEXT, hGlobal) == nullptr) {
            GlobalFree(hGlobal);
            CloseClipboard();
            return "Error: SetClipboardData failed";
        }
        
        CloseClipboard();
        return "Success: wrote to clipboard";
    }
    return "Error: unknown clipboard op: " + op;
}

std::string GrepTool::execute(const nlohmann::json& args) {
    std::string path = resolve_path(args.value("path", ".")).string();
    std::string pattern_str = args.at("pattern").get<std::string>();
    std::string include = args.value("include", "");

    try {
        std::regex pattern(pattern_str);
        std::string results;
        std::error_code ec;

        if (!std::filesystem::exists(path, ec)) return "Error: Path does not exist: " + path;

        auto it = std::filesystem::recursive_directory_iterator(path, std::filesystem::directory_options::skip_permission_denied, ec);
        if (ec) return "Error: Failed to open directory: " + ec.message();

        while (it != std::filesystem::recursive_directory_iterator()) {
            try {
                const auto& entry = *it;
                std::error_code entry_ec;
                if (entry.is_regular_file(entry_ec)) {
                    bool matches_filter = true;
                    if (!include.empty()) {
                        std::string filename = entry.path().filename().string();
                        matches_filter = matches_spec_utf8(filename, include);
                    }
                    
                    if (matches_filter) {
                        std::ifstream f(entry.path());
                        if (f.is_open()) {
                            std::string line;
                            int line_num = 1;
                            while (std::getline(f, line)) {
                                if (std::regex_search(line, pattern)) {
                                    results += entry.path().string() + ":" + std::to_string(line_num) + ": " + line + "\n";
                                }
                                line_num++;
                            }
                        }
                    }
                }
            } catch (...) {
                // Ignore error on single entry
            }

            try {
                ++it;
            } catch (...) {
                it.pop();
            }
        }
        return truncate_output(results.empty() ? "No matches found." : results);
    } catch (const std::regex_error& e) {
        return "Error: Invalid regex pattern: " + std::string(e.what());
    } catch (const std::exception& e) {
        return "Error during grep: " + std::string(e.what());
    }
}

std::string EditTool::execute(const nlohmann::json& args) {
    std::string path = resolve_path(args.at("path").get<std::string>()).string();
    std::string old_str = args.at("old_string").get<std::string>();
    std::string new_str = args.at("new_string").get<std::string>();
    bool replace_all = args.value("replaceAll", false);

    if (old_str.empty()) return "Error: old_string cannot be empty.";

    std::ifstream f_in(path, std::ios::binary);
    if (!f_in.is_open()) return "Error: cannot open file for reading: " + path;
    
    std::string content((std::istreambuf_iterator<char>(f_in)), std::istreambuf_iterator<char>());
    f_in.close();

    bool has_crlf = (content.find("\r\n") != std::string::npos);
    std::string norm_content = normalize_newlines(content);
    std::string norm_old_str = normalize_newlines(old_str);
    std::string norm_new_str = normalize_newlines(new_str);

    size_t pos = norm_content.find(norm_old_str);
    if (pos == std::string::npos) return "Error: old_string not found in file.";

    if (replace_all) {
        size_t last_pos = 0;
        while ((pos = norm_content.find(norm_old_str, last_pos)) != std::string::npos) {
            norm_content.replace(pos, norm_old_str.length(), norm_new_str);
            last_pos = pos + norm_new_str.length();
        }
    } else {
        norm_content.replace(pos, norm_old_str.length(), norm_new_str);
    }

    // Convert back to original line ending format if it had CRLF
    std::string final_content;
    if (has_crlf) {
        final_content.reserve(norm_content.size() * 11 / 10);
        for (char c : norm_content) {
            if (c == '\n') {
                final_content += "\r\n";
            } else {
                final_content += c;
            }
        }
    } else {
        final_content = norm_content;
    }

    std::ofstream f_out(path, std::ios::binary);
    if (!f_out.is_open()) return "Error: cannot open file for writing: " + path;
    f_out << final_content;
    f_out.close();

    return "Success: updated " + path;
}

std::string RememberTool::execute(const nlohmann::json& args) {
    if (!args.contains("fact") || !args["fact"].is_string()) {
        return "Error: missing 'fact' parameter.";
    }
    std::string fact = args["fact"].get<std::string>();

    std::string profile;
    load_profile_memory(profile);

    // Strip whitespaces & trailing/leading junk from fact
    fact.erase(0, fact.find_first_not_of(" \t\r\n\"'"));
    fact.erase(fact.find_last_not_of(" \t\r\n\"'") + 1);

    if (fact.empty()) {
        return "Error: fact parameter cannot be empty.";
    }

    if (!profile.empty() && profile.back() != '\n') {
        profile += "\n";
    }
    profile += "- " + fact + "\n";

    save_profile_memory(profile);

    return "Success: Fact successfully saved to permanent long-term memory: \"" + fact + "\"";
}

// ---------------------------------------------------------------------------
// ScreenshotTool Implementation
// ---------------------------------------------------------------------------

struct WindowSearch {
    std::string substring;
    HWND result_hwnd = nullptr;
};

static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    WindowSearch* search = reinterpret_cast<WindowSearch*>(lParam);
    char title[512];
    if (GetWindowTextA(hwnd, title, sizeof(title)) > 0) {
        std::string title_str(title);
        std::string t_lower = title_str;
        std::string s_lower = search->substring;
        std::transform(t_lower.begin(), t_lower.end(), t_lower.begin(), ::tolower);
        std::transform(s_lower.begin(), s_lower.end(), s_lower.begin(), ::tolower);

        if (t_lower.find(s_lower) != std::string::npos) {
            if (IsWindowVisible(hwnd)) {
                search->result_hwnd = hwnd;
                return FALSE; // Stop enumerating
            }
        }
    }
    return TRUE; // Continue enumerating
}

static HWND GetWindowBySubstring(const std::string& substring) {
    WindowSearch search;
    search.substring = substring;
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&search));
    return search.result_hwnd;
}

std::string ScreenshotTool::execute(const json& arguments) {
    std::string target = "desktop_1";
    if (arguments.contains("target") && arguments["target"].is_string()) {
        target = arguments["target"].get<std::string>();
    }

    std::string base64_image;
    bool success = false;
    std::string error_msg;

    QMetaObject::invokeMethod(qApp, [&]() {
        try {
            QList<QScreen*> screens = QGuiApplication::screens();
            QPixmap pixmap;

            if (target == "desktop_1") {
                if (!screens.isEmpty()) {
                    pixmap = screens[0]->grabWindow(0);
                    success = true;
                } else {
                    error_msg = "No screens found";
                }
            } else if (target == "desktop_2") {
                if (screens.size() > 1) {
                    pixmap = screens[1]->grabWindow(0);
                    success = true;
                } else if (!screens.isEmpty()) {
                    pixmap = screens[0]->grabWindow(0); // fallback
                    success = true;
                } else {
                    error_msg = "No screens found";
                }
            } else {
                HWND hwnd = FindWindowA(NULL, target.c_str());
                if (!hwnd) {
                    hwnd = GetWindowBySubstring(target);
                }

                if (hwnd) {
                    RECT rect;
                    if (GetWindowRect(hwnd, &rect)) {
                        int w = rect.right - rect.left;
                        int h = rect.bottom - rect.top;
                        if (w > 0 && h > 0) {
                            QScreen* screen = QGuiApplication::primaryScreen();
                            if (screen) {
                                pixmap = screen->grabWindow(reinterpret_cast<WId>(hwnd));
                                success = true;
                            } else {
                                error_msg = "Primary screen not found";
                            }
                        } else {
                            error_msg = "Window has invalid/zero dimensions";
                        }
                    } else {
                        error_msg = "Failed to get window coordinates";
                    }
                } else {
                    error_msg = "Window with title/substring '" + target + "' not found. Try 'desktop_1' or check open window names.";
                }
            }

            if (success && !pixmap.isNull()) {
                std::string screenshot_path = resolve_path("screenshot.png").string();
                pixmap.save(QString::fromStdString(screenshot_path), "PNG");

                QByteArray bytes;
                QBuffer buffer(&bytes);
                buffer.open(QIODevice::WriteOnly);
                pixmap.save(&buffer, "PNG");
                base64_image = bytes.toBase64().toStdString();

                if (QtUiApp::s_instance) {
                    QtUiApp::s_instance->m_pending_image_base64 = base64_image;
                    QtUiApp::s_instance->m_pending_image_mime = "image/png";
                    QtUiApp::s_instance->m_pending_image_name = "screenshot.png";
                    QtUiApp::s_instance->m_pending_image_has_match = false;
                }
            } else if (pixmap.isNull() && error_msg.empty()) {
                error_msg = "Failed to capture image data (null pixmap)";
            }
        } catch (const std::exception& e) {
            error_msg = std::string("Exception during capture: ") + e.what();
        } catch (...) {
            error_msg = "Unknown error during capture";
        }
    }, Qt::BlockingQueuedConnection);

    if (!success || base64_image.empty()) {
        return "Error taking screenshot: " + error_msg;
    }

    return "Success: Screenshot of '" + target + "' captured successfully, saved to workspace as 'screenshot.png', and automatically attached as visual input for your next turn.";
}

// ---------------------------------------------------------------------------
// OSAutomationTool Implementation
// ---------------------------------------------------------------------------

static WORD GetVirtualKeyCode(const std::string& key) {
    std::string k = key;
    std::transform(k.begin(), k.end(), k.begin(), ::tolower);

    if (k == "enter" || k == "return") return VK_RETURN;
    if (k == "tab") return VK_TAB;
    if (k == "backspace" || k == "back") return VK_BACK;
    if (k == "escape" || k == "esc") return VK_ESCAPE;
    if (k == "space") return VK_SPACE;
    if (k == "up") return VK_UP;
    if (k == "down") return VK_DOWN;
    if (k == "left") return VK_LEFT;
    if (k == "right") return VK_RIGHT;
    if (k == "delete" || k == "del") return VK_DELETE;
    if (k == "ctrl" || k == "control") return VK_CONTROL;
    if (k == "shift") return VK_SHIFT;
    if (k == "alt" || k == "menu") return VK_MENU;
    if (k == "win" || k == "lwin") return VK_LWIN;
    if (k == "f1") return VK_F1;
    if (k == "f2") return VK_F2;
    if (k == "f3") return VK_F3;
    if (k == "f4") return VK_F4;
    if (k == "f5") return VK_F5;
    if (k == "f6") return VK_F6;
    if (k == "f7") return VK_F7;
    if (k == "f8") return VK_F8;
    if (k == "f9") return VK_F9;
    if (k == "f10") return VK_F10;
    if (k == "f11") return VK_F11;
    if (k == "f12") return VK_F12;
    
    if (k.size() == 1) {
        char c = k[0];
        if (c >= 'a' && c <= 'z') return c - 'a' + 'A';
        if (c >= '0' && c <= '9') return c;
    }

    return 0;
}

static void SimulateMouseMove(int x, int y) {
    INPUT input = {0};
    input.type = INPUT_MOUSE;
    
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    input.mi.dx = ((x - vx) * 65536) / vw;
    input.mi.dy = ((y - vy) * 65536) / vh;
    input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    SendInput(1, &input, sizeof(INPUT));
}

static void SimulateMouseClick(int x, int y, const std::string& button) {
    SimulateMouseMove(x, y);
    Sleep(50);

    INPUT inputs[2] = {0};
    inputs[0].type = INPUT_MOUSE;
    inputs[1].type = INPUT_MOUSE;

    if (button == "right") {
        inputs[0].mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
        inputs[1].mi.dwFlags = MOUSEEVENTF_RIGHTUP;
    } else if (button == "middle") {
        inputs[0].mi.dwFlags = MOUSEEVENTF_MIDDLEDOWN;
        inputs[1].mi.dwFlags = MOUSEEVENTF_MIDDLEUP;
    } else {
        inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    }

    SendInput(2, inputs, sizeof(INPUT));
}

static void SimulateMouseDrag(int start_x, int start_y, int end_x, int end_y, const std::string& button) {
    SimulateMouseMove(start_x, start_y);
    Sleep(50);

    INPUT down_input = {0};
    down_input.type = INPUT_MOUSE;
    if (button == "right") {
        down_input.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
    } else {
        down_input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    }
    SendInput(1, &down_input, sizeof(INPUT));
    Sleep(50);

    SimulateMouseMove(end_x, end_y);
    Sleep(50);

    INPUT up_input = {0};
    up_input.type = INPUT_MOUSE;
    if (button == "right") {
        up_input.mi.dwFlags = MOUSEEVENTF_RIGHTUP;
    } else {
        up_input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    }
    SendInput(1, &up_input, sizeof(INPUT));
}

static void SimulateTypeText(const std::wstring& wtext) {
    for (wchar_t wc : wtext) {
        INPUT inputs[2] = {0};
        
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wScan = wc;
        inputs[0].ki.dwFlags = KEYEVENTF_UNICODE;

        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wScan = wc;
        inputs[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;

        SendInput(2, inputs, sizeof(INPUT));
        Sleep(10);
    }
}

static void SimulateKeyPress(const std::string& key_combo) {
    std::vector<std::string> keys;
    std::stringstream ss(key_combo);
    std::string token;
    while (std::getline(ss, token, '+')) {
        token.erase(0, token.find_first_not_of(" \t\r\n"));
        token.erase(token.find_last_not_of(" \t\r\n") + 1);
        if (!token.empty()) {
            keys.push_back(token);
        }
    }

    std::vector<WORD> vk_codes;
    for (const auto& key : keys) {
        WORD vk = GetVirtualKeyCode(key);
        if (vk != 0) {
            vk_codes.push_back(vk);
        }
    }

    if (vk_codes.empty()) return;

    std::vector<INPUT> inputs_down(vk_codes.size(), {0});
    for (size_t i = 0; i < vk_codes.size(); ++i) {
        inputs_down[i].type = INPUT_KEYBOARD;
        inputs_down[i].ki.wVk = vk_codes[i];
    }
    SendInput(static_cast<UINT>(inputs_down.size()), inputs_down.data(), sizeof(INPUT));
    
    Sleep(20);

    std::vector<INPUT> inputs_up(vk_codes.size(), {0});
    for (size_t i = 0; i < vk_codes.size(); ++i) {
        size_t idx = vk_codes.size() - 1 - i;
        inputs_up[i].type = INPUT_KEYBOARD;
        inputs_up[i].ki.wVk = vk_codes[idx];
        inputs_up[i].ki.dwFlags = KEYEVENTF_KEYUP;
    }
    SendInput(static_cast<UINT>(inputs_up.size()), inputs_up.data(), sizeof(INPUT));
}

std::string OSAutomationTool::execute(const json& arguments) {
    if (!arguments.contains("action") || !arguments["action"].is_string()) {
        return "Error: missing 'action' parameter.";
    }
    std::string action = arguments["action"].get<std::string>();

    try {
        if (action == "move_mouse") {
            if (!arguments.contains("x") || !arguments["x"].is_number() ||
                !arguments.contains("y") || !arguments["y"].is_number()) {
                return "Error: 'move_mouse' requires both 'x' and 'y' integer arguments.";
            }
            int x = arguments["x"].get<int>();
            int y = arguments["y"].get<int>();
            SimulateMouseMove(x, y);
            return "Success: Moved mouse cursor to absolute screen coordinates (" + std::to_string(x) + ", " + std::to_string(y) + ")";
        }
        else if (action == "click_mouse") {
            if (!arguments.contains("x") || !arguments["x"].is_number() ||
                !arguments.contains("y") || !arguments["y"].is_number()) {
                return "Error: 'click_mouse' requires both 'x' and 'y' integer arguments.";
            }
            int x = arguments["x"].get<int>();
            int y = arguments["y"].get<int>();
            std::string button = "left";
            if (arguments.contains("button") && arguments["button"].is_string()) {
                button = arguments["button"].get<std::string>();
            }
            SimulateMouseClick(x, y, button);
            return "Success: Simulated mouse click with '" + button + "' button at (" + std::to_string(x) + ", " + std::to_string(y) + ")";
        }
        else if (action == "drag_mouse") {
            if (!arguments.contains("x") || !arguments["x"].is_number() ||
                !arguments.contains("y") || !arguments["y"].is_number() ||
                !arguments.contains("end_x") || !arguments["end_x"].is_number() ||
                !arguments.contains("end_y") || !arguments["end_y"].is_number()) {
                return "Error: 'drag_mouse' requires 'x', 'y', 'end_x', and 'end_y' integer arguments.";
            }
            int x = arguments["x"].get<int>();
            int y = arguments["y"].get<int>();
            int end_x = arguments["end_x"].get<int>();
            int end_y = arguments["end_y"].get<int>();
            std::string button = "left";
            if (arguments.contains("button") && arguments["button"].is_string()) {
                button = arguments["button"].get<std::string>();
            }
            SimulateMouseDrag(x, y, end_x, end_y, button);
            return "Success: Simulated mouse drag from (" + std::to_string(x) + ", " + std::to_string(y) + ") to (" + std::to_string(end_x) + ", " + std::to_string(end_y) + ")";
        }
        else if (action == "type_text") {
            if (!arguments.contains("text") || !arguments["text"].is_string()) {
                return "Error: 'type_text' requires a 'text' string argument.";
            }
            std::string text = arguments["text"].get<std::string>();
            std::wstring wtext = QString::fromStdString(text).toStdWString();
            SimulateTypeText(wtext);
            return "Success: Typed text: \"" + text + "\"";
        }
        else if (action == "press_key") {
            if (!arguments.contains("key") || !arguments["key"].is_string()) {
                return "Error: 'press_key' requires a 'key' string argument (e.g. 'enter', 'ctrl+v').";
            }
            std::string key = arguments["key"].get<std::string>();
            SimulateKeyPress(key);
            return "Success: Pressed keyboard key/combo: \"" + key + "\"";
        }
        else if (action == "focus_window") {
            if (!arguments.contains("target") || !arguments["target"].is_string()) {
                return "Error: 'focus_window' requires a 'target' string argument (title or substring of the window to focus).";
            }
            std::string target = arguments["target"].get<std::string>();
            HWND hwnd = FindWindowA(NULL, target.c_str());
            if (!hwnd) {
                hwnd = GetWindowBySubstring(target);
            }

            if (hwnd) {
                if (IsIconic(hwnd)) {
                    ShowWindow(hwnd, SW_RESTORE);
                }
                SetForegroundWindow(hwnd);
                return "Success: Brought window matching '" + target + "' to foreground and focus.";
            } else {
                return "Error: Window matching '" + target + "' not found.";
            }
        }
        else {
            return "Error: Unknown action '" + action + "'";
        }
    } catch (const std::exception& e) {
        return "Error in OS Automation: " + std::string(e.what());
    } catch (...) {
        return "Unknown error in OS Automation.";
    }
}

// ---------------------------------------------------------------------------
// RegisterFaceTool Implementation
// ---------------------------------------------------------------------------

std::string RegisterFaceTool::execute(const json& arguments) {
    if (!arguments.contains("identity") || !arguments["identity"].is_string()) {
        return "Error: missing 'identity' parameter.";
    }
    std::string identity = arguments["identity"].get<std::string>();

    // Strip whitespace and trailing/leading junk from identity
    identity.erase(0, identity.find_first_not_of(" \t\r\n\"'"));
    identity.erase(identity.find_last_not_of(" \t\r\n\"'") + 1);

    if (identity.empty()) {
        return "Error: 'identity' parameter cannot be empty.";
    }

    std::string src_path;
    std::string base64_data;

    if (QtUiApp::s_instance) {
        src_path = QtUiApp::s_instance->m_pending_image_file_path;
        base64_data = QtUiApp::s_instance->m_pending_image_base64;
    }

    if (src_path.empty() && base64_data.empty()) {
        return "Error: No active or pending image found in the current context to register. Please attach or drag-and-drop an image first, then tell me who is in it.";
    }

    std::error_code ec;
    std::string target_dir = resolve_path("memory/" + identity).string();
    std::filesystem::create_directories(target_dir, ec);
    if (ec) {
        return "Error creating directory for visual memory: " + ec.message();
    }

    bool success = false;
    std::string saved_file_path;

    if (!src_path.empty() && std::filesystem::exists(src_path)) {
        // Copy the original file to preserve exact format and fidelity
        std::string filename = std::filesystem::path(src_path).filename().string();
        std::string dest_path = target_dir + "/" + filename;
        std::filesystem::copy_file(src_path, dest_path, std::filesystem::copy_options::overwrite_existing, ec);
        if (!ec) {
            success = true;
            saved_file_path = dest_path;
        } else {
            return "Error copying image file to reference memory: " + ec.message();
        }
    } else if (!base64_data.empty()) {
        // Decode and save from raw Base64 (for screenshots/clipboard pastes)
        QByteArray decoded = QByteArray::fromBase64(QByteArray::fromStdString(base64_data));
        std::string dest_path = target_dir + "/face.png";
        std::ofstream out(dest_path, std::ios::binary);
        if (out.is_open()) {
            out.write(decoded.constData(), decoded.size());
            out.close();
            success = true;
            saved_file_path = dest_path;
        } else {
            return "Error saving screenshot data to reference memory.";
        }
    }

    if (success) {
        // Hot reload the visual memory system on the GUI thread
        if (QtUiApp::s_instance) {
            QMetaObject::invokeMethod(QtUiApp::s_instance, []() {
                QtUiApp::s_instance->loadVisualMemoryReferenceHashes();
            }, Qt::BlockingQueuedConnection);

            // Clear the pending path/file to complete the learning registration
            QtUiApp::s_instance->m_pending_image_file_path.clear();
        }
        return "Success: Successfully learned the face of '" + identity + "'. The reference image has been copied to reference memory at '" + saved_file_path + "', compiled into perceptual hashes, and registered in real-time. I will now recognize " + identity + " automatically on all future images!";
    }

    return "Error: Failed to register face.";
}

// ---------------------------------------------------------------------------
// ScheduleReminderTool & CancelReminderTool Implementation
// ---------------------------------------------------------------------------

std::string ScheduleReminderTool::execute(const json& arguments) {
    if (!arguments.contains("id") || !arguments["id"].is_string() ||
        !arguments.contains("target_time") || !arguments["target_time"].is_string() ||
        !arguments.contains("system_instruction") || !arguments["system_instruction"].is_string() ||
        !arguments.contains("user_context") || !arguments["user_context"].is_string()) {
        return "Error: missing required parameters. 'id', 'target_time', 'system_instruction', and 'user_context' are all required.";
    }

    std::string id = arguments["id"].get<std::string>();
    std::string target_time_str = arguments["target_time"].get<std::string>();
    std::string system_instruction = arguments["system_instruction"].get<std::string>();
    std::string user_context = arguments["user_context"].get<std::string>();
    bool requires_voice = false;
    if (arguments.contains("requires_voice_alert") && arguments["requires_voice_alert"].is_boolean()) {
        requires_voice = arguments["requires_voice_alert"].get<bool>();
    }

    QDateTime targetTime = QDateTime::fromString(QString::fromStdString(target_time_str), Qt::ISODate);
    if (!targetTime.isValid()) {
        return "Error: 'target_time' has an invalid ISO 8601 format. Please use YYYY-MM-DDTHH:MM:SS format.";
    }

    if (QtUiApp::s_instance && QtUiApp::s_instance->m_chronos_engine) {
        ChronosTask task;
        task.id = QString::fromStdString(id);
        task.targetTime = targetTime;
        task.systemInstruction = QString::fromStdString(system_instruction);
        task.userContext = QString::fromStdString(user_context);
        task.requiresVoiceAlert = requires_voice;

        QtUiApp::s_instance->m_chronos_engine->scheduleTask(task);
        return "Success: Task '" + id + "' scheduled successfully inside Chronos Engine for execution at " + target_time_str;
    }

    return "Error: Chronos Engine is not active.";
}

std::string CancelReminderTool::execute(const json& arguments) {
    if (!arguments.contains("id") || !arguments["id"].is_string()) {
        return "Error: missing 'id' parameter.";
    }
    std::string id = arguments["id"].get<std::string>();

    if (QtUiApp::s_instance && QtUiApp::s_instance->m_chronos_engine) {
        bool cancelled = QtUiApp::s_instance->m_chronos_engine->cancelTask(QString::fromStdString(id));
        if (cancelled) {
            return "Success: Cancelled task '" + id + "' from Chronos Engine.";
        } else {
            return "Error: Task '" + id + "' not found in Chronos Engine.";
        }
    }

    return "Error: Chronos Engine is not active.";
}
