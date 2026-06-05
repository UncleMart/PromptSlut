#ifndef KEYFILE_H
#define KEYFILE_H

#include <string>
#include <filesystem>
#include <fstream>
#include <algorithm>

// ---------------------------------------------------------------------------
// KeyFile — simple XOR-encoded storage for API keys next to the executable.
//
// File format (3 lines):
//   Line 1: bearer_key
//   Line 2: serper_key
//   Line 3: (empty / version marker)
//
// Each line is XOR-encoded with a single-byte key (0x53 — 'S').
// This is obfuscation, not encryption — sufficient to prevent casual
// reading but not meant as real security.
//
// The XOR key is chosen so that 0x53 XOR 0x53 == 0 (null byte), which
// means an empty key would produce a file full of 0x53 bytes.  That is
// acceptable — the user would see garbage and know something is wrong.
// ---------------------------------------------------------------------------

static constexpr unsigned char XOR_KEY = 0x53;
static constexpr const char* KEY_FILE_NAME = "promptslut.key";

inline std::string xor_encode(const std::string& plaintext)
{
    std::string out = plaintext;
    for (auto& c : out)
        c = static_cast<char>(static_cast<unsigned char>(c) ^ XOR_KEY);
    return out;
}

inline std::string xor_decode(const std::string& encoded)
{
    // Same operation — XOR is its own inverse.
    return xor_encode(encoded);
}

inline bool save_all_settings(
    const std::string& bearer, const std::string& serper, const std::string& host, int port, const std::string& model, bool matrix,
    bool use_sec, const std::string& sec_host, int sec_port, const std::string& sec_bearer, const std::string& sec_model,
    int rain_theme_idx, bool voice_enabled, const std::string& tts_voice) 
{
    std::string exe_path = std::filesystem::current_path().string() + "/" + KEY_FILE_NAME;
    std::ofstream f(exe_path, std::ios::binary);
    if (!f.is_open()) return false;
    f << xor_encode(bearer) << "\n";
    f << xor_encode(serper) << "\n";
    f << xor_encode(host) << "\n";
    f << xor_encode(std::to_string(port)) << "\n";
    f << xor_encode(model) << "\n";
    f << xor_encode(matrix ? "1" : "0") << "\n";
    f << xor_encode(use_sec ? "1" : "0") << "\n";
    f << xor_encode(sec_host) << "\n";
    f << xor_encode(std::to_string(sec_port)) << "\n";
    f << xor_encode(sec_bearer) << "\n";
    f << xor_encode(sec_model) << "\n";
    f << xor_encode(std::to_string(rain_theme_idx)) << "\n";
    f << xor_encode(voice_enabled ? "1" : "0") << "\n";
    f << xor_encode(tts_voice) << "\n";
    return true;
}

inline bool load_all_settings(
    std::string& bearer, std::string& serper, std::string& host, int& port, std::string& model, bool& matrix,
    bool& use_sec, std::string& sec_host, int& sec_port, std::string& sec_bearer, std::string& sec_model,
    int& rain_theme_idx, bool& voice_enabled, std::string& tts_voice) 
{
    std::string exe_path = std::filesystem::current_path().string() + "/" + KEY_FILE_NAME;
    std::ifstream f(exe_path, std::ios::binary);
    if (!f.is_open()) return false;

    std::string line1, line2, line3, line4, line5, line6, line7, line8, line9, line10, line11, line12, line13, line14;
    if (!std::getline(f, line1) || !std::getline(f, line2)) return false;

    bearer = xor_decode(line1);
    serper = xor_decode(line2);

    if (std::getline(f, line3)) host = xor_decode(line3);
    else host = "127.0.0.1";

    if (std::getline(f, line4)) {
        try { port = std::stoi(xor_decode(line4)); } catch (...) { port = 8080; }
    } else port = 8080;

    if (std::getline(f, line5)) model = xor_decode(line5);
    else model = "qwen3:latest";

    if (std::getline(f, line6)) matrix = (xor_decode(line6) == "1");
    else matrix = true;

    if (std::getline(f, line7)) use_sec = (xor_decode(line7) == "1");
    else use_sec = true; // Enabled by default!

    if (std::getline(f, line8)) sec_host = xor_decode(line8);
    else sec_host = "192.168.1.141";

    if (std::getline(f, line9)) {
        try { sec_port = std::stoi(xor_decode(line9)); } catch (...) { sec_port = 8080; }
    } else sec_port = 8080;

    if (std::getline(f, line10)) sec_bearer = xor_decode(line10);
    else sec_bearer = "key";

    if (std::getline(f, line11)) sec_model = xor_decode(line11);
    else sec_model = "qwen3.5:0.8b";

    if (std::getline(f, line12)) {
        try { rain_theme_idx = std::stoi(xor_decode(line12)); } catch (...) { rain_theme_idx = 0; }
    } else rain_theme_idx = 0;

    if (std::getline(f, line13)) voice_enabled = (xor_decode(line13) == "1");
    else voice_enabled = false;

    if (std::getline(f, line14)) tts_voice = xor_decode(line14);
    else tts_voice = "af_maple";

    return true;
}

inline bool save_keys(const std::string& bearer_key, const std::string& serper_key)
{
    // Resolve path next to the executable.
    std::string exe_path = std::filesystem::current_path().string() + "/" + KEY_FILE_NAME;

    std::ofstream f(exe_path, std::ios::binary);
    if (!f.is_open())
        return false;

    f << xor_encode(bearer_key) << "\n";
    f << xor_encode(serper_key) << "\n";

    return true;
}

inline bool load_keys(std::string& out_bearer, std::string& out_serper)
{
    std::string exe_path = std::filesystem::current_path().string() + "/" + KEY_FILE_NAME;

    std::ifstream f(exe_path, std::ios::binary);
    if (!f.is_open())
        return false;

    std::string line1, line2;
    if (!std::getline(f, line1) || !std::getline(f, line2))
        return false;

    out_bearer = xor_decode(line1);
    out_serper = xor_decode(line2);

    return true;
}

inline std::string get_serper_key()
{
    std::string bearer, serper;
    if (load_keys(bearer, serper) && !serper.empty())
        return serper;
    return ""; // Completely removed fallback hardcoded key!
}

inline std::filesystem::path get_profile_file_path()
{
    wchar_t exe_path[MAX_PATH];
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    std::filesystem::path root_dir = std::filesystem::path(exe_path).parent_path();

    while (root_dir.has_parent_path() &&
           !std::filesystem::exists(root_dir / "CMakeLists.txt") &&
           !std::filesystem::exists(root_dir / "system_prompt.txt")) {
        root_dir = root_dir.parent_path();
    }

    if (std::filesystem::exists(root_dir / "CMakeLists.txt") || std::filesystem::exists(root_dir / "system_prompt.txt")) {
        return root_dir / "promptslut.profile";
    }

    return std::filesystem::current_path() / "promptslut.profile";
}

inline bool save_profile_memory(const std::string& profile_text)
{
    std::string path = get_profile_file_path().string();
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open())
        return false;
    f << profile_text;
    return true;
}

inline bool load_profile_memory(std::string& out_profile_text)
{
    std::string path = get_profile_file_path().string();
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        out_profile_text = "";
        return false;
    }
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    out_profile_text = content;
    return true;
}

#endif // KEYFILE_H
