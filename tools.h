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

// ---------------------------------------------------------------------------
// ScreenshotTool — capture screenshot of specific monitor or window
// ---------------------------------------------------------------------------

class ScreenshotTool : public Tool
{
public:
    std::string name() const override { return "screenshot"; }
    std::string description() const override { return "Capture a screenshot of desktop_1, desktop_2, or a specific open window by matching its title, and automatically attach it as visual context."; }
    std::string execute(const json& arguments) override;
    json schema() override {
        return {
            {"type", "object"},
            {"properties", {
                {"target", {{"type", "string"}, {"description", "Target to capture: 'desktop_1', 'desktop_2', or a specific window title/substring (e.g., 'notepad', 'browser')"}}}
            }}
        };
    }
};

// ---------------------------------------------------------------------------
// OSAutomationTool — simulate keyboard and mouse inputs
// ---------------------------------------------------------------------------

class OSAutomationTool : public Tool
{
public:
    std::string name() const override { return "os_automation"; }
    std::string description() const override { return "Control mouse movements, clicks, drag-and-drop, direct unicode text typing, focusing specific windows by name, and key combinations (e.g. 'enter', 'ctrl+c', 'ctrl+v', 'tab')."; }
    std::string execute(const json& arguments) override;
    json schema() override {
        return {
            {"type", "object"},
            {"properties", {
                {"action", {{"type", "string"}, {"enum", {"move_mouse", "click_mouse", "drag_mouse", "type_text", "press_key", "focus_window"}}, {"description", "The action to simulate"}}},
                {"x", {{"type", "integer"}, {"description", "Absolute screen pixel X coordinate"}}},
                {"y", {{"type", "integer"}, {"description", "Absolute screen pixel Y coordinate"}}},
                {"end_x", {{"type", "integer"}, {"description", "Absolute screen pixel End X coordinate (for drag_mouse)"}}},
                {"end_y", {{"type", "integer"}, {"description", "Absolute screen pixel End Y coordinate (for drag_mouse)"}}},
                {"button", {{"type", "string"}, {"enum", {"left", "right", "middle"}}, {"description", "Mouse button for clicks (default is 'left')"}}},
                {"text", {{"type", "string"}, {"description", "Raw UTF-8 text string to type directly (for type_text)"}}},
                {"key", {{"type", "string"}, {"description", "Special key or keyboard combination (e.g. 'enter', 'backspace', 'ctrl+v', 'ctrl+a', 'tab') to press (for press_key)"}}},
                {"target", {{"type", "string"}, {"description", "Target window title or substring to focus (for focus_window)"}}}
            }},
            {"required", {"action"}}
        };
    }
};

// ---------------------------------------------------------------------------
// RegisterFaceTool — teach the model a face dynamically in real-time
// ---------------------------------------------------------------------------

class RegisterFaceTool : public Tool
{
public:
    std::string name() const override { return "register_face"; }
    std::string description() const override { return "Teach the model a new face dynamically. Saves the currently attached/dropped image or screenshot to the memory directory under the specified identity name, and registers it in real-time so it is recognized automatically on all future turns."; }
    std::string execute(const json& arguments) override;
    json schema() override {
        return {
            {"type", "object"},
            {"properties", {
                {"identity", {{"type", "string"}, {"description", "The name of the person or object in the image to register/remember (e.g., 'Marty', 'Alex', 'My Dog')"}}}
            }},
            {"required", {"identity"}}
        };
    }
};

// ---------------------------------------------------------------------------
// ScheduleReminderTool & CancelReminderTool — Chronos Engine bindings
// ---------------------------------------------------------------------------

class ScheduleReminderTool : public Tool
{
public:
    std::string name() const override { return "schedule_reminder"; }
    std::string description() const override { return "Schedule a time-based reminder or deferred wakeup task inside the Chronos Engine."; }
    std::string execute(const json& arguments) override;
    json schema() override {
        return {
            {"type", "object"},
            {"properties", {
                {"id", {{"type", "string"}, {"description", "Unique alphanumeric ID for the task"}}},
                {"target_time", {{"type", "string"}, {"description", "The exact target execution timestamp in ISO 8601 format (YYYY-MM-DDTHH:MM:SS)"}}},
                {"system_instruction", {{"type", "string"}, {"description", "Direct system prompt context to feed the model when triggered"}}},
                {"user_context", {{"type", "string"}, {"description", "Context/prompt simulating a user reminder trigger"}}},
                {"requires_voice_alert", {{"type", "boolean"}, {"description", "Announce/speak verbally using TTS"}}}
            }},
            {"required", {"id", "target_time", "system_instruction", "user_context"}}
        };
    }
};

class CancelReminderTool : public Tool
{
public:
    std::string name() const override { return "cancel_reminder"; }
    std::string description() const override { return "Cancel a previously scheduled reminder task by its unique task ID."; }
    std::string execute(const json& arguments) override;
    json schema() override {
        return {
            {"type", "object"},
            {"properties", {
                {"id", {{"type", "string"}, {"description", "The unique task ID to cancel"}}}
            }},
            {"required", {"id"}}
        };
    }
};

// Forward declaration
class Worker;

class AddTaskTool : public Tool
{
private:
    Worker* m_worker;
public:
    explicit AddTaskTool(Worker* worker) : m_worker(worker) {}
    std::string name() const override { return "add_task"; }
    std::string description() const override { return "Add a new task to the user's to-do list."; }
    std::string execute(const json& arguments) override;
    json schema() override {
        return {
            {"type", "object"},
            {"properties", {
                {"text", {{"type", "string"}, {"description", "The description/text of the task to add"}}}
            }},
            {"required", {"text"}}
        };
    }
};

class CompleteTaskTool : public Tool
{
private:
    Worker* m_worker;
public:
    explicit CompleteTaskTool(Worker* worker) : m_worker(worker) {}
    std::string name() const override { return "complete_task"; }
    std::string description() const override { return "Mark a task on the user's to-do list as completed using its unique task ID."; }
    std::string execute(const json& arguments) override;
    json schema() override {
        return {
            {"type", "object"},
            {"properties", {
                {"id", {{"type", "string"}, {"description", "The unique ID (UUID) of the task to complete"}}}
            }},
            {"required", {"id"}}
        };
    }
};

class RemoveTaskTool : public Tool
{
private:
    Worker* m_worker;
public:
    explicit RemoveTaskTool(Worker* worker) : m_worker(worker) {}
    std::string name() const override { return "remove_task"; }
    std::string description() const override { return "Remove/delete a task from the user's to-do list using its unique task ID."; }
    std::string execute(const json& arguments) override;
    json schema() override {
        return {
            {"type", "object"},
            {"properties", {
                {"id", {{"type", "string"}, {"description", "The unique ID (UUID) of the task to delete/remove"}}}
            }},
            {"required", {"id"}}
        };
    }
};

// Workspace directory helpers
void set_workspace_directory(const std::filesystem::path& path);
std::filesystem::path get_workspace_directory();

#endif // TOOLS_H

