#include "tools.h"
#include "convert.h"
#include "keyfile.h"
#include "httplib.h"
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
    std::string path = args["path"].get<std::string>();

    if (op == "read") {
        std::ifstream f(path);
        if (!f.is_open()) return "Error: cannot open file for reading: " + path;
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    } else if (op == "write") {
        if (!args.contains("content") || !args["content"].is_string()) {
            return "Error: 'content' string parameter is required for write operation.";
        }
        std::ofstream f(path);
        if (!f.is_open()) return "Error: cannot open file for writing: " + path;
        f << args["content"].get<std::string>();
        return "Success: wrote to " + path;
    } else if (op == "append") {
        if (!args.contains("content") || !args["content"].is_string()) {
            return "Error: 'content' string parameter is required for append operation.";
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
        return result.empty() ? "Directory is empty." : result;
    }
    return "Error: unknown file op: " + op;
}

std::string ShellTool::execute(const nlohmann::json& args) {
    if (!args.contains("cmd") || !args["cmd"].is_string()) {
        return "Error: missing or invalid 'cmd' parameter.";
    }

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

    if (output.empty()) {
        return "Command completed with exit code: " + std::to_string(exit_code);
    }
    return output;
}

std::string DiskSearchTool::execute(const nlohmann::json& args) {
    std::string path = args.value("path", ".");
    std::string pattern = args.value("pattern", "*");
    std::string results;
    std::error_code ec;

    if (!std::filesystem::exists(path, ec)) {
        return "Error: Path does not exist: " + path;
    }

    try {
        auto it = std::filesystem::recursive_directory_iterator(path, std::filesystem::directory_options::skip_permission_denied, ec);
        if (ec) return "Error: Failed to open directory: " + ec.message();
        
        for (const auto& entry : it) {
            std::error_code entry_ec;
            if (entry.is_regular_file(entry_ec) && matches_spec_utf8(entry.path().filename().string(), pattern)) {
                results += entry.path().string() + "\n";
            }
        }
    } catch (const std::exception& e) {
        return std::string("Error during search: ") + e.what();
    }

    return results.empty() ? "No files found." : results;
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
    std::string path = args.value("path", ".");
    std::string pattern_str = args.at("pattern").get<std::string>();
    std::string include = args.value("include", "");

    try {
        std::regex pattern(pattern_str);
        std::string results;
        std::error_code ec;

        if (!std::filesystem::exists(path, ec)) return "Error: Path does not exist: " + path;

        for (const auto& entry : std::filesystem::recursive_directory_iterator(path, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (!entry.is_regular_file()) continue;

            if (!include.empty()) {
                std::string filename = entry.path().filename().string();
                if (!matches_spec_utf8(filename, include)) continue;
            }

            std::ifstream f(entry.path());
            if (!f.is_open()) continue;

            std::string line;
            int line_num = 1;
            while (std::getline(f, line)) {
                if (std::regex_search(line, pattern)) {
                    results += entry.path().string() + ":" + std::to_string(line_num) + ": " + line + "\n";
                }
                line_num++;
            }
        }
        return results.empty() ? "No matches found." : results;
    } catch (const std::regex_error& e) {
        return "Error: Invalid regex pattern: " + std::string(e.what());
    } catch (const std::exception& e) {
        return "Error during grep: " + std::string(e.what());
    }
}

std::string EditTool::execute(const nlohmann::json& args) {
    std::string path = args.at("path").get<std::string>();
    std::string old_str = args.at("old_string").get<std::string>();
    std::string new_str = args.at("new_string").get<std::string>();
    bool replace_all = args.value("replaceAll", false);

    if (old_str.empty()) return "Error: old_string cannot be empty.";

    std::ifstream f_in(path, std::ios::binary);
    if (!f_in.is_open()) return "Error: cannot open file for reading: " + path;
    
    std::string content((std::istreambuf_iterator<char>(f_in)), std::istreambuf_iterator<char>());
    f_in.close();

    size_t pos = content.find(old_str);
    if (pos == std::string::npos) return "Error: old_string not found in file.";

    if (replace_all) {
        size_t last_pos = 0;
        while ((pos = content.find(old_str, last_pos)) != std::string::npos) {
            content.replace(pos, old_str.length(), new_str);
            last_pos = pos + new_str.length();
        }
    } else {
        content.replace(pos, old_str.length(), new_str);
    }

    std::ofstream f_out(path, std::ios::binary);
    if (!f_out.is_open()) return "Error: cannot open file for writing: " + path;
    f_out << content;
    f_out.close();

    return "Success: updated " + path;
}
