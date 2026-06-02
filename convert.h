#ifndef CONVERT_H
#define CONVERT_H

#include <windows.h>
#include <string>
#include <stdexcept>

// ---------------------------------------------------------------------------
// UTF-16 / UTF-8 bidirectional converters
//
// All Win32 API calls use wstring (UTF-16).  llama-server and nlohmann::json
// demand std::string (UTF-8).  These two functions sit at the boundary.
//
// Memory management:
//   • Both functions return std::string / std::wstring by value.
//   • No raw pointers, no global pools – RAII handles lifetime.
//   • On conversion failure a std::runtime_error is thrown (not silently
//     truncated), so the caller sees the problem immediately.
// ---------------------------------------------------------------------------

inline std::string utf16_to_utf8(const wchar_t* wch, size_t len)
{
    if (len == 0 && wch) len = wcslen(wch);
    if (!wch) return {};

    const int required = WideCharToMultiByte(CP_UTF8, 0, wch, (int)len, nullptr, 0, nullptr, nullptr);
    if (required == 0)
        throw std::runtime_error("WideCharToMultiByte failed");

    std::string out;
    out.resize(required);
    int result = WideCharToMultiByte(CP_UTF8, 0, wch, (int)len, &out[0], required, nullptr, nullptr);
    if (result == 0)
        throw std::runtime_error("WideCharToMultiByte write failed");

    return out;
}

inline std::string utf16_to_utf8(const std::wstring& wstr)
{
    return utf16_to_utf8(wstr.c_str(), wstr.length());
}

inline std::wstring utf8_to_utf16(const char* utf8, size_t len)
{
    if (len == 0 && utf8) len = strlen(utf8);
    if (!utf8) return {};

    const int required = MultiByteToWideChar(CP_UTF8, 0, utf8, (int)len, nullptr, 0);
    if (required == 0)
        throw std::runtime_error("MultiByteToWideChar failed");

    std::wstring out;
    out.resize(required);
    int result = MultiByteToWideChar(CP_UTF8, 0, utf8, (int)len, &out[0], required);
    if (result == 0)
        throw std::runtime_error("MultiByteToWideChar write failed");

    return out;
}

inline std::wstring utf8_to_utf16(const std::string& utf8)
{
    return utf8_to_utf16(utf8.c_str(), utf8.length());
}

// Convenience: std::string -> std::string (no-op, for call-site symmetry)
inline std::string utf8_to_utf8(const std::string& s) { return s; }

// Convenience: std::wstring -> std::wstring (no-op, for call-site symmetry)
inline std::wstring utf16_to_utf16(const std::wstring& w) { return w; }

#endif // CONVERT_H
