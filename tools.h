#ifndef TOOLS_H
#define TOOLS_H

#include <winsock2.h>
#include <windows.h>
#include "Tool.h"
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// FileTool — read, write, append, and list files on the filesystem
// ---------------------------------------------------------------------------

class FileTool : public Tool
{
public:
    std::string name() const override { return "file"; }
    std::string description() const override { return "Read, write, append, copy, move, rename, delete, set_workspace, set_cwd, or list files on the filesystem. Use 'append' to build large files in chunks to avoid errors."; }
    std::string execute(const json& arguments) override;
    json schema() override {
        return {
            {"type", "object"},
            {"properties", {
                {"path", {{"type", "string"}, {"description", "Path to the source file, folder, or target workspace directory"}}},
                {"op", {{"type", "string"}, {"enum", {"read", "write", "append", "list", "copy", "move", "rename", "delete", "set_workspace", "set_cwd"}}, {"description", "Operation to perform"}}},
                {"content", {{"type", "string"}, {"description", "Content to write or append (required for write/append)"}}},
                {"destination", {{"type", "string"}, {"description", "Target path (required for copy/move/rename)"}}}
            }},
            {"required", {"path", "op"}}
        };
    }
};



// ---------------------------------------------------------------------------
// ShellTool — execute a single Windows command and capture stdout/stderr
// ---------------------------------------------------------------------------

class ShellTool : public Tool
{
public:
    std::string name() const override { return "shell"; }
    std::string description() const override { return "Execute a shell command."; }
    std::string execute(const json& arguments) override;
    json schema() override {
        return {
            {"type", "object"},
            {"properties", {
                {"cmd", {{"type", "string"}, {"description", "The command to run"}}}
            }},
            {"required", {"cmd"}}
        };
    }
};

// ---------------------------------------------------------------------------
// DiskSearchTool — walk a directory tree and return file paths matching
//                   an optional glob pattern (supports *, ? wildcards)
// ---------------------------------------------------------------------------

class DiskSearchTool : public Tool
{
public:
    std::string name() const override { return "disk_search"; }
    std::string description() const override { return "Search for files matching a pattern."; }
    std::string execute(const json& arguments) override;
    json schema() override {
        return {
            {"type", "object"},
            {"properties", {
                {"path", {{"type", "string"}, {"description", "Root path to search"}}},
                {"pattern", {{"type", "string"}, {"description", "Glob pattern"}}}
            }},
            {"required", {"path", "pattern"}}
        };
    }
};

// ---------------------------------------------------------------------------
// SearchTool — search the web using DuckDuckGo (no API key required)
// ---------------------------------------------------------------------------

class SearchTool : public Tool
{
public:
    std::string name() const override { return "search"; }
    std::string description() const override { return "Search the web for information. Returns a list of titles and URLs."; }
    std::string execute(const json& arguments) override;
    json schema() override {
        return {
            {"type", "object"},
            {"properties", {
                {"query", {{"type", "string"}, {"description", "The search query"}}}
            }},
            {"required", {"query"}}
        };
    }
};

// ---------------------------------------------------------------------------
// FetchTool — fetch a URL and return the HTML content
// ---------------------------------------------------------------------------

class FetchTool : public Tool
{
public:
    std::string name() const override { return "fetch"; }
    std::string description() const override { return "Fetch the HTML content of a given URL."; }
    std::string execute(const json& arguments) override;
    json schema() override {
        return {
            {"type", "object"},
            {"properties", {
                {"url", {{"type", "string"}, {"description", "The URL to fetch"}}}
            }},
            {"required", {"url"}}
        };
    }
};

// ---------------------------------------------------------------------------
// ClockTool — get the current system date and time
// ---------------------------------------------------------------------------

class ClockTool : public Tool
{
public:
    std::string name() const override { return "clock"; }
    std::string description() const override { return "Get the current system date and time."; }
    std::string execute(const json& arguments) override;
    json schema() override {
        return {
            {"type", "object"},
            {"properties", {}}
        };
    }
};

// ---------------------------------------------------------------------------
// ClipboardTool — read from and write to the system clipboard
// ---------------------------------------------------------------------------

class ClipboardTool : public Tool
{
public:
    std::string name() const override { return "clipboard"; }
    std::string description() const override { return "Read from or write to the system clipboard."; }
    std::string execute(const json& arguments) override;
    json schema() override {
        return {
            {"type", "object"},
            {"properties", {
                {"op", {{"type", "string"}, {"enum", {"read", "write"}}, {"description", "Operation to perform"}}},
                {"content", {{"type", "string"}, {"description", "Content to write to the clipboard"}}}
            }},
            {"required", {"op"}}
        };
    }
};

// ---------------------------------------------------------------------------
// GrepTool — search for a pattern in files
// ---------------------------------------------------------------------------

class GrepTool : public Tool
{
public:
    std::string name() const override { return "grep"; }
    std::string description() const override { return "Search for a regex pattern in files within a directory."; }
    std::string execute(const json& arguments) override;
    json schema() override {
        return {
            {"type", "object"},
            {"properties", {
                {"path", {{"type", "string"}, {"description", "Root directory to search"}}},
                {"pattern", {{"type", "string"}, {"description", "Regex pattern to search for"}}},
                {"include", {{"type", "string"}, {"description", "Optional file extension filter (e.g., '*.cpp')"}}}
            }},
            {"required", {"path", "pattern"}}
        };
    }
};

// ---------------------------------------------------------------------------
// EditTool — perform string replacement in a file
// ---------------------------------------------------------------------------

class EditTool : public Tool
{
public:
    std::string name() const override { return "edit"; }
    std::string description() const override { return "Replace a specific string in a file with another string."; }
    std::string execute(const json& arguments) override;
    json schema() override {
        return {
            {"type", "object"},
            {"properties", {
                {"path", {{"type", "string"}, {"description", "Path to the file to edit"}}},
                {"old_string", {{"type", "string"}, {"description", "The exact string to be replaced"}}},
                {"new_string", {{"type", "string"}, {"description", "The replacement string"}}},
                {"replaceAll", {{"type", "boolean"}, {"description", "Whether to replace all occurrences"}}}
            }},
            {"required", {"path", "old_string", "new_string"}}
        };
    }
};

// ---------------------------------------------------------------------------
// RememberTool — save a permanent fact into long-term memory
// ---------------------------------------------------------------------------

class RememberTool : public Tool
{
public:
    std::string name() const override { return "remember"; }
    std::string description() const override { return "Save a permanent personal fact about the user (e.g., likes, dislikes, family, name, occupation) into long-term memory so you remember it in future sessions."; }
    std::string execute(const json& arguments) override;
    json schema() override {
        return {
            {"type", "object"},
            {"properties", {
                {"fact", {{"type", "string"}, {"description", "The permanent personal fact about the user to remember."}}}
            }},
            {"required", {"fact"}}
        };
    }
};

// Workspace directory helpers
void set_workspace_directory(const std::filesystem::path& path);
std::filesystem::path get_workspace_directory();

#endif // TOOLS_H

